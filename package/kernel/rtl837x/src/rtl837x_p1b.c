// SPDX-License-Identifier: GPL-2.0-only
/*
 * Opt-in bench controls for the P1-B primitives, not bridge offload.
 * Root-only debugfs access additionally requires initial-namespace NET_ADMIN.
 * RTNL serializes overrides/restoration with DSA administrative transitions.
 * All register facts and table operations live in the existing sourced helpers.
 */
#include <linux/capability.h>
#include <linux/debugfs.h>
#include <linux/errno.h>
#include <linux/if_bridge.h>
#include <linux/module.h>
#include <linux/rtnetlink.h>
#include <linux/seq_file.h>
#include <linux/slab.h>
#include <linux/string.h>
#include <linux/uaccess.h>
#include <linux/user_namespace.h>

#include "rtl837x.h"

static struct dsa_port *rtl837x_p1b_port(struct rtl837x_priv *priv, unsigned int port)
{
	if (port < 4 || port > 7 || !dsa_is_user_port(priv->ds, port))
		return NULL;
	return dsa_to_port(priv->ds, port);
}

static int rtl837x_p1b_restore(struct rtl837x_priv *priv, unsigned int port)
{
	struct dsa_port *dp = rtl837x_p1b_port(priv, port);
	enum rtl837x_cist_state state;
	int ret;

	ASSERT_RTNL();
	if (!dp || !dp->user)
		return -EINVAL;
	if (!(priv->p1b_overrides & BIT(port)))
		return 0;

	/* Use current DSA state, not a snapshot predating a later down/up. */
	switch (dp->stp_state) {
	case BR_STATE_DISABLED:
		state = RTL837X_CIST_DISABLED;
		break;
	case BR_STATE_LISTENING:
	case BR_STATE_BLOCKING:
		state = RTL837X_CIST_BLOCKING;
		break;
	case BR_STATE_LEARNING:
		state = RTL837X_CIST_LEARNING;
		break;
	case BR_STATE_FORWARDING:
		state = RTL837X_CIST_FORWARDING;
		break;
	default:
		return -EINVAL;
	}
	ret = rtl837x_stp_set_state(priv, port, state);
	if (!ret) {
		priv->p1b_overrides &= ~BIT(port);
		dev_info(priv->dev, "P1-B bench port %u restored to DSA state %u\n",
			 port, dp->stp_state);
	}
	return ret;
}

static int rtl837x_p1b_disable(struct rtl837x_priv *priv)
{
	int port, ret, first_error = 0;

	ASSERT_RTNL();
	for (port = 4; port <= 7; port++) {
		if (!(priv->p1b_overrides & BIT(port)))
			continue;
		ret = rtl837x_p1b_restore(priv, port);
		if (ret && !first_error)
			first_error = ret;
	}
	/* A failed restore keeps the controls enabled for an explicit retry. */
	if (!first_error)
		priv->p1b_enabled = false;
	return first_error;
}

static int rtl837x_p1b_set_state(struct rtl837x_priv *priv, unsigned int port,
				 const char *name)
{
	struct dsa_port *dp = rtl837x_p1b_port(priv, port);
	enum rtl837x_cist_state state;
	int ret;

	ASSERT_RTNL();
	if (!dp || !dp->user)
		return -EINVAL;
	if (!strcmp(name, "restore"))
		return rtl837x_p1b_restore(priv, port);
	if (dp->bridge || netdev_has_any_upper_dev(dp->user))
		return -EBUSY;
	if (!netif_running(dp->user))
		return -ENETDOWN;

	if (!strcmp(name, "disabled"))
		state = RTL837X_CIST_DISABLED;
	else if (!strcmp(name, "blocking") || !strcmp(name, "listening"))
		state = RTL837X_CIST_BLOCKING;
	else if (!strcmp(name, "learning"))
		state = RTL837X_CIST_LEARNING;
	else if (!strcmp(name, "forwarding"))
		state = RTL837X_CIST_FORWARDING;
	else
		return -EINVAL;

	/* Track even an uncertain write so disable/remove attempts restoration. */
	priv->p1b_overrides |= BIT(port);
	ret = rtl837x_stp_set_state(priv, port, state);
	if (!ret)
		dev_info(priv->dev,
			 "P1-B bench CIST port %u overridden as %s; Linux state unchanged\n",
			 port, name);
	return ret;
}

static ssize_t rtl837x_p1b_control_write(struct file *file, const char __user *user,
					 size_t count, loff_t *position)
{
	struct rtl837x_priv *priv = file->private_data;
	struct dsa_port *dp;
	char op[16], arg[16], state[16], extra;
	unsigned int port;
	char *input;
	bool enable;
	int fields, ret;

	if (!ns_capable(&init_user_ns, CAP_NET_ADMIN))
		return -EPERM;
	if (!count || count > 64 || *position)
		return -EINVAL;
	input = memdup_user_nul(user, count);
	if (IS_ERR(input))
		return PTR_ERR(input);
	if (memchr(input, '\0', count)) {
		kfree(input);
		return -EINVAL;
	}
	fields = sscanf(input, "%15s %15s %15s %c", op, arg, state, &extra);
	kfree(input);
	if (fields < 2 || fields > 3)
		return -EINVAL;

	rtnl_lock();
	if (!strcmp(op, "enable") && fields == 2) {
		ret = kstrtobool(arg, &enable);
		if (!ret) {
			if (enable)
				priv->p1b_enabled = true;
			else
				ret = rtl837x_p1b_disable(priv);
		}
		goto out;
	}
	if (!priv->p1b_enabled) {
		ret = -EACCES;
		goto out;
	}
	ret = kstrtouint(arg, 10, &port);
	if (ret)
		goto out;
	if (!strcmp(op, "stp") && fields == 3) {
		ret = rtl837x_p1b_set_state(priv, port, state);
	} else if (!strcmp(op, "flush") && fields == 2) {
		dp = rtl837x_p1b_port(priv, port);
		if (!dp || !dp->user)
			ret = -EINVAL;
		else if (dp->bridge || netdev_has_any_upper_dev(dp->user))
			ret = -EBUSY;
		else if (!netif_running(dp->user))
			ret = -ENETDOWN;
		else
			ret = rtl837x_l2_flush_port(priv, port);
	} else {
		ret = -EINVAL;
	}
out:
	rtnl_unlock();
	if (ret)
		return ret;
	*position += count;
	return count;
}

static const struct file_operations rtl837x_p1b_control_fops = {
	.owner = THIS_MODULE,
	.open = simple_open,
	.write = rtl837x_p1b_control_write,
	.llseek = no_llseek,
};

static int rtl837x_p1b_status_show(struct seq_file *seq, void *unused)
{
	struct rtl837x_priv *priv = seq->private;
	u32 cist, ctrl, config, isolation, limit, entry[3];
	unsigned int cpu_mask, port, cpu;
	bool matches;
	int ret;

	if (!ns_capable(&init_user_ns, CAP_NET_ADMIN))
		return -EPERM;
	rtnl_lock();
	seq_printf(seq, "enabled=%u overrides=%#x\n",
		   priv->p1b_enabled, priv->p1b_overrides);
	seq_puts(seq, "control: enable 1|0; stp PORT disabled|blocking|listening|learning|forwarding|restore; flush PORT\n");
	if (!priv->p1b_enabled) {
		ret = 0;
		goto out;
	}
	/* Snapshot shares the transaction lock with table and flush operations. */
	mutex_lock(&priv->table_lock);
	ret = rtl837x_reg_read(priv, RTL837X_STP_STATE, &cist);
	if (!ret)
		ret = rtl837x_reg_read(priv, RTL837X_L2_TBL_FLUSH_CTRL, &ctrl);
	if (!ret)
		ret = rtl837x_reg_read(priv, RTL837X_L2_TBL_FLUSH_CONFIG, &config);
	if (ret)
		goto out_table;
	seq_printf(seq, "cist=%#010x flush_ctrl=%#010x flush_config=%#010x\n",
		   cist, ctrl, config);
	for (port = 0; port < RTL8372N_NUM_PORTS; port++) {
		struct dsa_port *dp = dsa_to_port(priv->ds, port);
		const char *name = "-";

		ret = rtl837x_reg_read(priv, RTL837X_PORT_ISOLATION_REG(port), &isolation);
		if (!ret)
			ret = rtl837x_reg_read(priv, RTL837X_L2_LEARN_LIMIT_REG(port), &limit);
		if (ret)
			break;
		if (dsa_is_user_port(priv->ds, port) && dp->user)
			name = dp->user->name;
		seq_printf(seq,
			   "port=%u netdev=%s cist=%u isolation=%#010x learn_limit=%#010x\n",
			   port, name, (cist >> (port * 2)) & 3, isolation, limit);
	}
out_table:
	mutex_unlock(&priv->table_lock);
	if (ret)
		goto out;
	cpu_mask = dsa_cpu_ports(priv->ds);
	if (hweight32(cpu_mask) != 1) {
		ret = -EOPNOTSUPP;
		goto out;
	}
	cpu = __ffs(cpu_mask);
	ret = rtl837x_bpdu_route_read(priv, 1, cpu, entry, &matches);
	if (!ret)
		seq_printf(seq, "bpdu vid=1 cpu=%u words=%#010x,%#010x,%#010x match=%u\n",
			   cpu, entry[0], entry[1], entry[2], matches);
out:
	if (ret)
		seq_printf(seq, "snapshot_error=%d; snapshot incomplete\n", ret);
	rtnl_unlock();
	/* Keep the error in the snapshot rather than discarding preceding words. */
	return 0;
}
DEFINE_SHOW_ATTRIBUTE(rtl837x_p1b_status);

void rtl837x_p1b_init(struct rtl837x_priv *priv)
{
	char name[80];
	struct dentry *control, *status;

	snprintf(name, sizeof(name), "rtl8372n-%s", dev_name(priv->dev));
	priv->p1b_debugfs = debugfs_create_dir(name, NULL);
	if (IS_ERR_OR_NULL(priv->p1b_debugfs)) {
		priv->p1b_debugfs = NULL;
		return;
	}
	/* Use protected debugfs operations, not debugfs_create_file_unsafe(). */
	control = debugfs_create_file("control", 0200, priv->p1b_debugfs, priv,
				      &rtl837x_p1b_control_fops);
	status = debugfs_create_file("status", 0400, priv->p1b_debugfs, priv,
				     &rtl837x_p1b_status_fops);
	if (IS_ERR_OR_NULL(control) || IS_ERR_OR_NULL(status)) {
		/* A privileged opener may already have used the control file. */
		rtl837x_p1b_remove(priv);
	}
}

void rtl837x_p1b_remove(struct rtl837x_priv *priv)
{
	int ret;

	/* Drain file operations before unregistering DSA or releasing priv. */
	debugfs_remove_recursive(priv->p1b_debugfs);
	priv->p1b_debugfs = NULL;
	rtnl_lock();
	ret = rtl837x_p1b_disable(priv);
	rtnl_unlock();
	if (ret)
		dev_warn(priv->dev, "P1-B bench state restore failed during teardown: %d\n",
			 ret);
}
