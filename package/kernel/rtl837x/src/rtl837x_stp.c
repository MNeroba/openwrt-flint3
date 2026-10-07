// SPDX-License-Identifier: GPL-2.0-only
/*
 * RTL8372N CIST state and dynamic L2 flush primitives.
 *
 * Register locations and the flush command sequence were compared with
 * RTLPlayground at f0aea3dcac056e3274fd39e1c76a7117471c37da. That repository
 * declares MIT; its STP implementation is marked public domain. No source
 * implementation block is copied here. See PROVENANCE.md for the limits of
 * this comparison and the still-open hardware validation.
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

int rtl837x_l2_flush_port(struct rtl837x_priv *priv, int port)
{
	u32 old_config, command;
	int ret, restore_ret;

	if (!priv || port < 0 || port >= RTL8372N_NUM_PORTS)
		return -EINVAL;

	/* The flush and indirect table engines share L2 state. Serialize them. */
	mutex_lock(&priv->table_lock);
	ret = rtl837x_table_wait_idle(priv);
	if (ret)
		goto out_unlock;
	ret = regmap_read_poll_timeout(priv->map, RTL837X_L2_TBL_FLUSH_CTRL,
				       command,
				       !command,
				       10, 1000000);
	if (ret)
		goto out_unlock;

	ret = rtl837x_reg_read(priv, RTL837X_L2_TBL_FLUSH_CONFIG, &old_config);
	if (ret)
		goto out_unlock;

	/* Public reference documents zero as port-based, dynamic-only flushing. */
	ret = rtl837x_reg_write(priv, RTL837X_L2_TBL_FLUSH_CONFIG,
				RTL837X_L2_TBL_FLUSH_DYNAMIC_PORT_MODE);
	if (ret) {
		restore_ret = rtl837x_reg_write(priv, RTL837X_L2_TBL_FLUSH_CONFIG, old_config);
		if (restore_ret)
			dev_err(priv->dev,
				"failed to restore L2 flush config: %d (original setup error %d)\n",
				restore_ret, ret);
		goto out_unlock;
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

	/* Wait for the complete control word to clear. */
	ret = regmap_read_poll_timeout(priv->map, RTL837X_L2_TBL_FLUSH_CTRL,
				       command,
				       !command,
				       10, 1000000);
	if (ret) {
		/* The engine may still be active, so keep its configuration stable. */
		dev_err(priv->dev,
			"dynamic L2 flush did not complete on port %d: %d; "
			"flush config remains in dynamic port mode\n",
			port, ret);
		goto out_unlock;
	}

	restore_ret = rtl837x_reg_write(priv, RTL837X_L2_TBL_FLUSH_CONFIG,
					old_config);
	if (!ret && restore_ret)
		ret = restore_ret;
	if (!ret)
		dev_info(priv->dev,
			 "P1-B dynamic L2 fast-age completed on port %d\n", port);
out_unlock:
	mutex_unlock(&priv->table_lock);
	return ret;
}
