// SPDX-License-Identifier: GPL-2.0-only
/*
 * Host regression for the actual rtl837x_stp.c flush helper. The scripted
 * transport deliberately retains command fields after BUSY clears and injects
 * I/O failures. Kernel polling time, ASIC semantics and entry removal are not
 * simulated or qualified here. Run with l2-flush-regression.sh.
 */
#include <errno.h>
#include <stdbool.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef uint32_t u32;
#define BIT(n) (UINT32_C(1) << (n))
#define GENMASK(h, l) ((UINT32_MAX << (l)) & (UINT32_MAX >> (31 - (h))))
#include "../src/rtl837x_regmap.h"

#define CHECK(expr) do { \
	if (!(expr)) { \
		fprintf(stderr, "FAIL line %d: %s\n", __LINE__, #expr); \
		exit(EXIT_FAILURE); \
	} \
} while (0)

struct device { unsigned int success_logs; };
struct mutex { bool held; };
struct regmap {
	struct mutex *lock;
	u32 config, submitted;
	u32 idle[4], active[4];
	unsigned int idle_len, active_len, idle_next, active_next;
	unsigned int ctrl_reads, config_reads, updates, submissions;
	unsigned int fail_ctrl_read, fail_config_read, bad_config_read;
	unsigned int fail_update;
	bool apply_failed_update, fail_submit, observed_busy;
	int table_error;
};
enum rtl837x_cist_state {
	RTL837X_CIST_DISABLED, RTL837X_CIST_BLOCKING,
	RTL837X_CIST_LEARNING, RTL837X_CIST_FORWARDING,
};
#define RTL8372N_NUM_PORTS 9
struct rtl837x_priv {
	struct device *dev;
	struct regmap *map;
	struct mutex table_lock;
};

static void mutex_lock(struct mutex *lock)
{
	CHECK(!lock->held);
	lock->held = true;
}

static void mutex_unlock(struct mutex *lock)
{
	CHECK(lock->held);
	lock->held = false;
}

static void test_log(struct device *dev, const char *fmt, ...)
{
	char output[512];
	va_list args;

	va_start(args, fmt);
	vsnprintf(output, sizeof(output), fmt, args);
	va_end(args);
	if (strstr(output, "P1-B dynamic L2 fast-age completed"))
		dev->success_logs++;
}
#define dev_info test_log
#define dev_err test_log

static int regmap_read(struct regmap *map, u32 reg, u32 *value)
{
	u32 *sequence;
	unsigned int *next, length;

	CHECK(map->lock->held);
	if (reg == RTL837X_L2_TBL_FLUSH_CONFIG) {
		if (++map->config_reads == map->fail_config_read)
			return -EIO;
		*value = map->config;
		if (map->config_reads == map->bad_config_read)
			*value ^= 1;
		return 0;
	}
	CHECK(reg == RTL837X_L2_TBL_FLUSH_CTRL);
	if (++map->ctrl_reads == map->fail_ctrl_read)
		return -EIO;
	sequence = map->submissions ? map->active : map->idle;
	next = map->submissions ? &map->active_next : &map->idle_next;
	length = map->submissions ? map->active_len : map->idle_len;
	CHECK(length);
	*value = sequence[*next < length ? (*next)++ : length - 1];
	map->observed_busy = !!(*value & RTL837X_L2_TBL_FLUSH_BUSY);
	return 0;
}

static int regmap_write(struct regmap *map, u32 reg, u32 value)
{
	CHECK(map->lock->held && !map->observed_busy);
	/* Also accept the previous helper's whole-config write for regression replay. */
	if (reg == RTL837X_L2_TBL_FLUSH_CONFIG) {
		map->updates++;
		map->config = value;
		return 0;
	}
	CHECK(reg == RTL837X_L2_TBL_FLUSH_CTRL);
	map->submissions++;
	map->submitted = value;
	CHECK(!(map->config & RTL837X_L2_TBL_FLUSH_CONFIG_MASK));
	return map->fail_submit ? -EIO : 0;
}

static int regmap_update_bits(struct regmap *map, u32 reg, u32 mask, u32 value)
{
	bool failed;

	CHECK(map->lock->held && !map->observed_busy);
	CHECK(reg == RTL837X_L2_TBL_FLUSH_CONFIG);
	CHECK(mask == RTL837X_L2_TBL_FLUSH_CONFIG_MASK);
	failed = ++map->updates == map->fail_update;
	if (!failed || map->apply_failed_update)
		map->config = (map->config & ~mask) | (value & mask);
	return failed ? -EIO : 0;
}

/* Bounded scripted reads, using the driver's real condition expression. */
#define regmap_read_poll_timeout(map, reg, value, cond, delay, timeout) ({ \
	int poll_ret = -ETIMEDOUT; \
	unsigned int poll_step; \
	CHECK((delay) == 10 && (timeout) == 1000000); \
	for (poll_step = 0; poll_step < 8; poll_step++) { \
		poll_ret = regmap_read((map), (reg), &(value)); \
		if (poll_ret || (cond)) \
			break; \
		poll_ret = -ETIMEDOUT; \
	} \
	poll_ret; \
})

static int rtl837x_table_wait_idle(struct rtl837x_priv *priv)
{
	CHECK(priv->table_lock.held);
	return priv->map->table_error;
}

/* CIST is outside this regression's scope; supply its link dependencies. */
static int rtl837x_reg_bits_write(struct rtl837x_priv *priv, u32 reg,
				 u32 mask, u32 value)
{
	(void)priv; (void)reg; (void)mask; (void)value;
	return -EOPNOTSUPP;
}

static int rtl837x_reg_bits_read(struct rtl837x_priv *priv, u32 reg,
				u32 mask, u32 *value)
{
	(void)priv; (void)reg; (void)mask; (void)value;
	return -EOPNOTSUPP;
}

#define rtl837x_reg_read(priv, reg, value) regmap_read((priv)->map, reg, value)
#define rtl837x_reg_write(priv, reg, value) regmap_write((priv)->map, reg, value)
/* Replace only kernel infrastructure; compile the production implementation. */
#define __RTL837X_H__
#include "../src/rtl837x_stp.c"

static struct device test_device;
static struct regmap test_map;
static struct rtl837x_priv test_priv;
static const u32 original_config = 0xa5a00005;
static unsigned int cases;

static void reset(void)
{
	memset(&test_device, 0, sizeof(test_device));
	memset(&test_map, 0, sizeof(test_map));
	memset(&test_priv, 0, sizeof(test_priv));
	test_priv.dev = &test_device;
	test_priv.map = &test_map;
	test_map.lock = &test_priv.table_lock;
	test_map.config = original_config;
	test_map.idle[0] = BIT(7);
	test_map.active[0] = BIT(4);
	test_map.idle_len = test_map.active_len = 1;
}

static void result(const char *name, int expected)
{
	int ret = rtl837x_l2_flush_port(&test_priv, 4);

	if (ret != expected)
		fprintf(stderr, "%s: expected %d, got %d\n", name, expected, ret);
	CHECK(ret == expected);
	CHECK(!test_priv.table_lock.held);
	CHECK(test_device.success_logs == (expected == 0 ? 1U : 0U));
	cases++;
	printf("PASS %s\n", name);
}

int main(void)
{
	int port;

	reset(); test_map.idle[0] = 0;
	result("initially zero control retains port mask after completion", 0);
	CHECK(test_map.config == original_config && test_map.updates == 2);

	reset(); result("retained port masks at idle/completion", 0);
	CHECK(test_map.submitted == (RTL837X_L2_TBL_FLUSH_START | BIT(4)));
	CHECK(test_map.config == original_config && test_map.updates == 2);

	reset(); test_map.active[0] |= RTL837X_L2_TBL_FLUSH_START;
	result("non-status command fields do not cause timeout", 0);

	reset(); test_map.idle[0] |= RTL837X_L2_TBL_FLUSH_BUSY;
	test_map.idle[1] = BIT(7); test_map.idle_len = 2;
	test_map.active[0] |= RTL837X_L2_TBL_FLUSH_BUSY;
	test_map.active[1] = BIT(4); test_map.active_len = 2;
	result("wait for BUSY clear before mutation and restoration", 0);
	CHECK(test_map.ctrl_reads == 4 && test_map.config == original_config);

	for (port = 7; port >= 4; port--) {
		reset(); test_map.idle[0] = BIT(port + 1);
		test_map.active[0] = BIT(port);
		CHECK(rtl837x_l2_flush_port(&test_priv, port) == 0);
		CHECK(test_map.submitted == (RTL837X_L2_TBL_FLUSH_START | BIT(port)));
		CHECK(test_map.config == original_config && !test_priv.table_lock.held);
	}
	cases++; puts("PASS ports 7/6/5/4 with retained command masks");

	reset(); test_map.idle[0] |= RTL837X_L2_TBL_FLUSH_BUSY;
	result("real pre-command BUSY timeout performs no writes", -ETIMEDOUT);
	CHECK(!test_map.updates && !test_map.submissions);

	reset(); test_map.active[0] |= RTL837X_L2_TBL_FLUSH_BUSY;
	result("real completion BUSY timeout leaves mode stable", -ETIMEDOUT);
	CHECK(test_map.updates == 1 && test_map.config == (original_config & ~7U));

	reset(); test_map.fail_ctrl_read = 1;
	result("pre-command status I/O failure performs no writes", -EIO);
	CHECK(!test_map.updates && !test_map.submissions);

	reset(); test_map.fail_ctrl_read = 2;
	result("completion I/O failure leaves mode stable", -EIO);
	CHECK(test_map.updates == 1 && test_map.config == (original_config & ~7U));

	reset(); test_map.fail_config_read = 1;
	result("original config read failure performs no writes", -EIO);
	CHECK(!test_map.updates && !test_map.submissions);

	reset(); test_map.fail_update = 1;
	result("mode setup error restores original fields", -EIO);
	CHECK(!test_map.submissions && test_map.config == original_config);

	reset(); test_map.fail_update = 1; test_map.apply_failed_update = true;
	result("possibly applied mode write error restores original fields", -EIO);
	CHECK(!test_map.submissions && test_map.config == original_config);

	reset(); test_map.fail_config_read = 2;
	result("mode readback I/O failure submits no command", -EIO);
	CHECK(!test_map.submissions && test_map.config == original_config);

	reset(); test_map.bad_config_read = 2;
	result("mode mismatch submits no command", -EIO);
	CHECK(!test_map.submissions && test_map.config == original_config);

	reset(); test_map.fail_submit = true;
	result("possibly posted command error leaves mode stable", -EIO);
	CHECK(test_map.updates == 1 && test_map.config == (original_config & ~7U));

	reset(); test_map.fail_update = 2;
	result("restore write failure is not logged as success", -EIO);

	reset(); test_map.fail_config_read = 3;
	result("restore read failure is not logged as success", -EIO);

	reset(); test_map.bad_config_read = 3;
	result("restore mismatch is not logged as success", -EIO);

	reset(); test_map.table_error = -ETIMEDOUT;
	result("shared table timeout performs no flush writes", -ETIMEDOUT);
	CHECK(!test_map.ctrl_reads && !test_map.updates && !test_map.submissions);

	reset(); CHECK(rtl837x_l2_flush_port(NULL, 4) == -EINVAL);
	CHECK(rtl837x_l2_flush_port(&test_priv, -1) == -EINVAL);
	CHECK(rtl837x_l2_flush_port(&test_priv, RTL8372N_NUM_PORTS) == -EINVAL);
	CHECK(!test_map.ctrl_reads && !test_map.updates && !test_priv.table_lock.held);
	cases++; puts("PASS invalid arguments perform no operations");

	printf("%u regression cases PASS (host register model, not hardware)\n", cases);
	return 0;
}
