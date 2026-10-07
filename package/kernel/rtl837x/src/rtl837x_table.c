// SPDX-License-Identifier: GPL-2.0-only
/*
 * RTL8372N VLAN table transactions.
 * Register layout: RTLPlayground (MIT), f0aea3dcac056e3274fd39e1c76a7117471c37da,
 * rtl837x_regs.h and rtl837x_port.c::vlan_get/vlan_create.
 * See PROVENANCE.md for source limits and unresolved upper VLAN fields.
 */
#include <linux/bitfield.h>
#include <linux/errno.h>
#include <linux/if_vlan.h>
#include <linux/iopoll.h>
#include <linux/lockdep.h>

#include "rtl837x.h"

int rtl837x_table_wait_idle(struct rtl837x_priv *priv)
{
	u32 command;

	lockdep_assert_held(&priv->table_lock);

	return regmap_read_poll_timeout(priv->map, RTL837X_TABLE_CTRL, command,
					!(command & RTL837X_TABLE_EXECUTE),
					10, 1000);
}

static int rtl837x_vlan_read_locked(struct rtl837x_priv *priv, u16 vid,
				    u32 *entry)
{
	u32 command;
	int ret;

	lockdep_assert_held(&priv->table_lock);

	ret = rtl837x_table_wait_idle(priv);
	if (ret)
		return ret;

	command = FIELD_PREP(RTL837X_TABLE_ADDRESS, vid) |
		  FIELD_PREP(RTL837X_TABLE_TARGET, RTL837X_TABLE_VLAN) |
		  RTL837X_TABLE_EXECUTE;
	ret = rtl837x_reg_write(priv, RTL837X_TABLE_CTRL, command);
	if (ret)
		return ret;

	ret = rtl837x_table_wait_idle(priv);
	if (ret)
		return ret;

	return rtl837x_reg_read(priv, RTL837X_TABLE_READ_DATA0, entry);
}

int rtl837x_vlan_set_port_masks(u32 entry, u16 members, u16 untagged,
				u32 *result)
{
	if (!result)
		return -EINVAL;
	if (members > FIELD_MAX(RTL837X_VLAN_MEMBER_MASK) ||
	    untagged > FIELD_MAX(RTL837X_VLAN_UNTAG_MASK))
		return -ERANGE;
	if (untagged & ~members)
		return -EINVAL;

	/* Preserve opaque upper fields; do not infer entry validity from bit 25. */
	*result = (entry & ~(RTL837X_VLAN_MEMBER_MASK | RTL837X_VLAN_UNTAG_MASK)) |
		  FIELD_PREP(RTL837X_VLAN_MEMBER_MASK, members) |
		  FIELD_PREP(RTL837X_VLAN_UNTAG_MASK, untagged);
	return 0;
}

int rtl837x_vlan_read(struct rtl837x_priv *priv, u16 vid, u32 *entry)
{
	u32 value;
	int ret;

	if (!entry || !vid || vid >= VLAN_VID_MASK)
		return -EINVAL;

	mutex_lock(&priv->table_lock);
	ret = rtl837x_vlan_read_locked(priv, vid, &value);
	if (!ret)
		*entry = value;
	mutex_unlock(&priv->table_lock);

	return ret;
}

int rtl837x_vlan_write(struct rtl837x_priv *priv, u16 vid, u32 entry)
{
	u16 members = FIELD_GET(RTL837X_VLAN_MEMBER_MASK, entry);
	u16 untagged = FIELD_GET(RTL837X_VLAN_UNTAG_MASK, entry);
	u32 command, readback;
	int ret;

	if (!vid || vid >= VLAN_VID_MASK || (untagged & ~members))
		return -EINVAL;

	/* The table engine owns staging and readback registers, not just CTRL. */
	mutex_lock(&priv->table_lock);
	ret = rtl837x_table_wait_idle(priv);
	if (ret)
		goto out;

	ret = rtl837x_reg_write(priv, RTL837X_TABLE_WRITE_DATA0, entry);
	if (ret)
		goto out;

	command = FIELD_PREP(RTL837X_TABLE_ADDRESS, vid) |
		  FIELD_PREP(RTL837X_TABLE_TARGET, RTL837X_TABLE_VLAN) |
		  RTL837X_TABLE_WRITE | RTL837X_TABLE_EXECUTE;
	ret = rtl837x_reg_write(priv, RTL837X_TABLE_CTRL, command);
	if (ret)
		goto out;

	ret = rtl837x_table_wait_idle(priv);
	if (ret)
		goto out;

	/* Read the same VID while still holding the transaction lock. */
	ret = rtl837x_vlan_read_locked(priv, vid, &readback);
	if (ret)
		goto out;
	if (readback != entry) {
		dev_err(priv->dev, "VLAN %u readback mismatch: wrote %#x, read %#x\n",
			vid, entry, readback);
		ret = -EIO;
	}
out:
	mutex_unlock(&priv->table_lock);
	return ret;
}
