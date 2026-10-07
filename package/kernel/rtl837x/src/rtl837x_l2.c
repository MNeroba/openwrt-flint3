// SPDX-License-Identifier: GPL-2.0-only
/*
 * RTL8372N BPDU multicast entry.
 *
 * Register fields and the static link-local multicast layout are based on
 * the public RTLPlayground references pinned in PROVENANCE.md. The operation
 * below is an original, narrowly scoped table transaction; it is not copied
 * from the firmware implementation. The CPU-only route still requires BE9300
 * validation, including the RTL8_4 reason and Linux bridge reception.
 */
#include <linux/bitfield.h>
#include <linux/errno.h>
#include <linux/if_vlan.h>
#include <linux/kernel.h>
#include <linux/lockdep.h>
#include <linux/string.h>

#include "rtl837x.h"

#define RTL837X_BPDU_VID_MIN 1
#define RTL837X_BPDU_VID_MAX (VLAN_VID_MASK - 1)
#define RTL837X_BPDU_MAC_WORD0 0xc2000000
#define RTL837X_BPDU_MAC_WORD1 0x00000180
#define RTL837X_L2_VID_MASK GENMASK(27, 16)
#define RTL837X_L2_IVL BIT(29)
#define RTL837X_L2_MC_LOW_PORT_MASK GENMASK(31, 30)
#define RTL837X_L2_MC_HIGH_PORT_MASK GENMASK(7, 0)

static int rtl837x_l2_write_words_locked(struct rtl837x_priv *priv,
					 const u32 words[3])
{
	static const u32 write_regs[] = {
		RTL837X_TABLE_WRITE_DATA0,
		RTL837X_L2_TABLE_WRITE_DATA1,
		RTL837X_L2_TABLE_WRITE_DATA2,
	};
	u32 command, status;
	unsigned int i;
	int ret;

	lockdep_assert_held(&priv->table_lock);

	ret = rtl837x_table_wait_idle(priv);
	if (ret)
		return ret;

	for (i = 0; i < ARRAY_SIZE(write_regs); i++) {
		ret = rtl837x_reg_write(priv, write_regs[i], words[i]);
		if (ret)
			return ret;
	}

	command = FIELD_PREP(RTL837X_TABLE_TARGET, RTL837X_L2_TABLE) |
		  RTL837X_TABLE_WRITE | RTL837X_TABLE_EXECUTE;
	ret = rtl837x_reg_write(priv, RTL837X_TABLE_CTRL, command);
	if (ret)
		return ret;

	ret = rtl837x_table_wait_idle(priv);
	if (ret)
		return ret;

	ret = rtl837x_reg_read(priv, RTL837X_L2_LOOKUP_STATUS, &status);
	if (ret)
		return ret;

	return status & RTL837X_L2_LOOKUP_HIT ? 0 : -ENOSPC;
}

static int rtl837x_l2_lookup_locked(struct rtl837x_priv *priv,
				    const u32 key[3], u32 result[3])
{
	static const u32 write_regs[] = {
		RTL837X_TABLE_WRITE_DATA0,
		RTL837X_L2_TABLE_WRITE_DATA1,
		RTL837X_L2_TABLE_WRITE_DATA2,
	};
	static const u32 read_regs[] = {
		RTL837X_TABLE_READ_DATA0,
		RTL837X_L2_TABLE_READ_DATA1,
		RTL837X_L2_TABLE_READ_DATA2,
	};
	u32 command, status;
	unsigned int i;
	int ret;

	lockdep_assert_held(&priv->table_lock);

	ret = rtl837x_table_wait_idle(priv);
	if (ret)
		return ret;

	ret = rtl837x_reg_bits_write(priv, RTL837X_L2_LOOKUP_STATUS,
				     RTL837X_L2_LOOKUP_METHOD_MASK, 0);
	if (ret)
		return ret;

	for (i = 0; i < ARRAY_SIZE(write_regs); i++) {
		ret = rtl837x_reg_write(priv, write_regs[i], key[i]);
		if (ret)
			return ret;
	}

	command = FIELD_PREP(RTL837X_TABLE_TARGET, RTL837X_L2_TABLE) |
		  RTL837X_TABLE_EXECUTE;
	ret = rtl837x_reg_write(priv, RTL837X_TABLE_CTRL, command);
	if (ret)
		return ret;

	ret = rtl837x_table_wait_idle(priv);
	if (ret)
		return ret;

	ret = rtl837x_reg_read(priv, RTL837X_L2_LOOKUP_STATUS, &status);
	if (ret)
		return ret;
	if (!(status & RTL837X_L2_LOOKUP_HIT))
		return -ENOENT;

	for (i = 0; i < ARRAY_SIZE(read_regs); i++) {
		ret = rtl837x_reg_read(priv, read_regs[i], &result[i]);
		if (ret)
			return ret;
	}

	return 0;
}

/*
 * Route the 802.1D bridge group for one VLAN to the external CPU port only.
 * VID 0 is not a usable key for this IVL lookup path. The caller currently
 * installs only VID 1 because general VLAN/PVID offload is not implemented.
 */
int rtl837x_bpdu_route_set(struct rtl837x_priv *priv, u16 vid, int cpu_port)
{
	u32 entry[3], key[3], readback[3];
	u32 old_method;
	int ret, restore_ret;
	bool method_changed = false;

	if (!priv || vid < RTL837X_BPDU_VID_MIN ||
	    vid > RTL837X_BPDU_VID_MAX || cpu_port < 0 ||
	    cpu_port >= RTL8372N_NUM_PORTS)
		return -EINVAL;

	/*
	 * RTLPlayground's public table layout stores MAC 01:80:c2:00:00:00,
	 * VID/IVL and the multicast port mask in these words. The external CPU
	 * port is selected from DSA topology rather than assuming another board's
	 * embedded CPU-port number.
	 */
	entry[0] = RTL837X_BPDU_MAC_WORD0;
	entry[1] = RTL837X_BPDU_MAC_WORD1 |
		   FIELD_PREP(RTL837X_L2_VID_MASK, vid) |
		   RTL837X_L2_IVL |
		   FIELD_PREP(RTL837X_L2_MC_LOW_PORT_MASK,
			      BIT(cpu_port) & GENMASK(1, 0));
	entry[2] = (BIT(cpu_port) >> 2) & RTL837X_L2_MC_HIGH_PORT_MASK;

	/* Lookup keys include MAC, VID and IVL, but not the multicast port mask. */
	key[0] = entry[0];
	key[1] = entry[1] & ~RTL837X_L2_MC_LOW_PORT_MASK;
	key[2] = 0;

	mutex_lock(&priv->table_lock);
	ret = rtl837x_reg_bits_read(priv, RTL837X_L2_LOOKUP_STATUS,
				    RTL837X_L2_LOOKUP_METHOD_MASK,
				    &old_method);
	if (ret)
		goto out_unlock;

	ret = rtl837x_l2_write_words_locked(priv, entry);
	if (ret)
		goto out_unlock;

	ret = rtl837x_reg_bits_write(priv, RTL837X_L2_LOOKUP_STATUS,
				     RTL837X_L2_LOOKUP_METHOD_MASK, 0);
	if (ret)
		goto out_unlock;
	method_changed = true;

	ret = rtl837x_l2_lookup_locked(priv, key, readback);
	if (!ret && memcmp(entry, readback, sizeof(entry))) {
		dev_err(priv->dev,
			"BPDU L2 entry readback mismatch for VID %u and CPU port %d\n",
			vid, cpu_port);
		ret = -EIO;
	}
	if (!ret)
		dev_info(priv->dev,
			 "P1-B BPDU multicast entry VID %u routed to CPU port %d (readback PASS)\n",
			vid, cpu_port);

	if (method_changed) {
		restore_ret = rtl837x_reg_bits_write(priv,
						     RTL837X_L2_LOOKUP_STATUS,
						     RTL837X_L2_LOOKUP_METHOD_MASK,
						     old_method);
		if (restore_ret) {
			dev_err(priv->dev,
				"failed to restore L2 lookup method: %d (operation error %d)\n",
				restore_ret, ret);
			if (!ret)
				ret = restore_ret;
		}
	}

out_unlock:
	mutex_unlock(&priv->table_lock);
	return ret;
}
