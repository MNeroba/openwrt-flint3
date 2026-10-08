// SPDX-License-Identifier: GPL-2.0-only
/*
 * RTL8372N CIST state and dynamic L2 flush primitives.
 *
 * Register locations and the flush command sequence were compared with
 * RTLPlayground at f0aea3dcac056e3274fd39e1c76a7117471c37da. That repository
 * declares MIT; its STP implementation is marked public domain. No source
 * implementation block is copied here. See PROVENANCE.md for the limits of
 * this comparison and the still-open hardware validation.
 * Flush BUSY bit 17 and mode bits 2:0 were compared with the public ZTE
 * RTL8372N source at 07f8687248578d4be6931c665ff5d08bb6cc3d9d.
 */
#include <linux/device.h>
#include <linux/errno.h>
#include <linux/iopoll.h>

#include "rtl837x.h"

int rtl837x_stp_set_state(struct rtl837x_priv *priv, int port,
			  enum rtl837x_cist_state state)
{
	u32 mask, value;
	int ret;

	if (!priv || port < 0 || port >= RTL8372N_NUM_PORTS ||
	    state < RTL837X_CIST_DISABLED || state > RTL837X_CIST_FORWARDING)
		return -EINVAL;

	mask = RTL837X_STP_PORT_STATE_MASK << (port * 2);
	ret = rtl837x_reg_bits_write(priv, RTL837X_STP_STATE, mask, state);
	if (ret)
		return ret;

	ret = rtl837x_reg_bits_read(priv, RTL837X_STP_STATE, mask, &value);
	if (ret)
		return ret;
	if (value != state) {
		dev_err(priv->dev,
			"CIST state readback mismatch on port %d: got %u, expected %u\n",
			port, value, state);
		return -EIO;
	}

	dev_info(priv->dev, "P1-B CIST port %d state %u read back\n",
		 port, value);
	return 0;
}

static int rtl837x_l2_flush_wait_idle(struct rtl837x_priv *priv, int port,
				      const char *stage, u32 *command)
{
	int ret;

	/* The port mask and START field are not completion status. */
	ret = regmap_read_poll_timeout(priv->map, RTL837X_L2_TBL_FLUSH_CTRL,
				       *command,
				       !(*command & RTL837X_L2_TBL_FLUSH_BUSY),
				       10, 1000000);
	if (ret == -ETIMEDOUT)
		dev_err(priv->dev,
			"dynamic L2 flush busy timeout on port %d %s: ctrl=%#010x (BUSY bit 17 set)\n",
			port, stage, *command);
	else if (ret)
		dev_err(priv->dev,
			"failed to read dynamic L2 flush status on port %d %s: %d\n",
			port, stage, ret);

	return ret;
}

int rtl837x_l2_flush_port(struct rtl837x_priv *priv, int port)
{
	u32 old_config, mode_config = 0, restored_config = 0;
	u32 before, command;
	int ret, restore_ret;

	if (!priv || port < 0 || port >= RTL8372N_NUM_PORTS)
		return -EINVAL;

	/* The flush and indirect table engines share L2 state. Serialize them. */
	mutex_lock(&priv->table_lock);
	ret = rtl837x_table_wait_idle(priv);
	if (ret)
		goto out_unlock;
	ret = rtl837x_l2_flush_wait_idle(priv, port, "before command", &before);
	if (ret)
		goto out_unlock;

	ret = rtl837x_reg_read(priv, RTL837X_L2_TBL_FLUSH_CONFIG, &old_config);
	if (ret)
		goto out_unlock;

	/* Public reference documents zero as port-based, dynamic-only flushing. */
	ret = regmap_update_bits(priv->map, RTL837X_L2_TBL_FLUSH_CONFIG,
				 RTL837X_L2_TBL_FLUSH_CONFIG_MASK,
				 RTL837X_L2_TBL_FLUSH_DYNAMIC_PORT_MODE);
	if (ret)
		goto restore_config;
	ret = rtl837x_reg_read(priv, RTL837X_L2_TBL_FLUSH_CONFIG, &mode_config);
	if (ret)
		goto restore_config;
	if ((mode_config & RTL837X_L2_TBL_FLUSH_CONFIG_MASK) !=
	    RTL837X_L2_TBL_FLUSH_DYNAMIC_PORT_MODE) {
		dev_err(priv->dev,
			"dynamic L2 flush mode mismatch on port %d: config=%#010x\n",
			port, mode_config);
		ret = -EIO;
		goto restore_config;
	}

	command = RTL837X_L2_TBL_FLUSH_START | BIT(port);
	ret = rtl837x_reg_write(priv, RTL837X_L2_TBL_FLUSH_CTRL, command);
	if (ret) {
		dev_err(priv->dev,
			"failed to submit dynamic L2 flush on port %d: %d; "
			"flush config remains in dynamic port mode\n",
			port, ret);
		goto out_unlock;
	}

	ret = rtl837x_l2_flush_wait_idle(priv, port, "after command", &command);
	if (ret) {
		/* The engine may still be active, so keep its configuration stable. */
		dev_err(priv->dev,
			"dynamic L2 flush completion unconfirmed on port %d: %d; "
			"flush config remains in dynamic port mode\n",
			port, ret);
		goto out_unlock;
	}

restore_config:
	/* No command was submitted, or BUSY cleared: restoring mode is safe. */
	restore_ret = regmap_update_bits(priv->map, RTL837X_L2_TBL_FLUSH_CONFIG,
					 RTL837X_L2_TBL_FLUSH_CONFIG_MASK,
					 old_config & RTL837X_L2_TBL_FLUSH_CONFIG_MASK);
	if (!restore_ret)
		restore_ret = rtl837x_reg_read(priv, RTL837X_L2_TBL_FLUSH_CONFIG,
					       &restored_config);
	if (!restore_ret && (restored_config & RTL837X_L2_TBL_FLUSH_CONFIG_MASK) !=
	    (old_config & RTL837X_L2_TBL_FLUSH_CONFIG_MASK)) {
		dev_err(priv->dev,
			"L2 flush config restore mismatch on port %d: got %#010x, expected mode %#x\n",
			port, restored_config, old_config & RTL837X_L2_TBL_FLUSH_CONFIG_MASK);
		restore_ret = -EIO;
	}
	if (restore_ret)
		dev_err(priv->dev, "failed to restore L2 flush config on port %d: %d\n",
			port, restore_ret);
	if (!ret && restore_ret)
		ret = restore_ret;
	if (!ret)
		dev_info(priv->dev,
			 "P1-B dynamic L2 fast-age completed on port %d: ctrl before=%#010x after=%#010x; config before=%#010x dynamic=%#010x restored=%#010x (readback PASS)\n",
			 port, before, command, old_config, mode_config, restored_config);
out_unlock:
	mutex_unlock(&priv->table_lock);
	return ret;
}
