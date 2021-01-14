//
// This module allows one to drop TCP connections. It can be
// usefull for killing TIME-WAIT sockets.
//
// Original idea was taken from linux-tcp-drop project
// written by Roman Arutyunyan (https://github.com/arut).
//
// Ilya V. Matveychikov, 2018
//

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/version.h>
#include <linux/proc_fs.h>
#include <linux/ctype.h>
#include <linux/inet.h>

#include <net/tcp.h>
#include <net/net_namespace.h>
#include <net/netns/generic.h>

#include <net/inet_hashtables.h>
#include <net/inet6_hashtables.h>

#ifndef CONFIG_IPV6
# define in6_pton(args...) 0
# define inet6_lookup(args...) NULL
#endif

#define DTS_PDE_NAME "tcpdropsock"

struct dts_data {
	uint32_t len;
	uint32_t available;
	char data[0];
};

struct dts_pernet {
	struct net *net;
	struct proc_dir_entry *pde;
};

struct dts_inet {
	int ipv6;
	const char *p;
	uint16_t port;
	uint32_t addr[ 4 ];
};

static void dts_kill(struct net *net, const struct dts_inet *src, const struct dts_inet *dst)
{
	struct sock *sk = NULL;

	if (!src->ipv6) {
		sk = inet_lookup(net, &tcp_hashinfo,
#if LINUX_VERSION_CODE >= KERNEL_VERSION(4, 6, 0)
			NULL, 0,
#endif
			(__be32)dst->addr[0], htons(dst->port),
			(__be32)src->addr[0], htons(src->port), 0);
		if (!sk) return;
	} else {
		sk = inet6_lookup(net, &tcp_hashinfo,
#if LINUX_VERSION_CODE >= KERNEL_VERSION(4, 6, 0)
			NULL, 0,
#endif
			(const struct in6_addr *)dst->addr, htons(dst->port),
			(const struct in6_addr *)src->addr, htons(src->port), 0);
		if (!sk) return;
	}

	printk("DTS: killing sk:%p (%s -> %s) state %d\n", sk, src->p, dst->p, sk->sk_state);

	if (sk->sk_state == TCP_TIME_WAIT) {
#if LINUX_VERSION_CODE >= KERNEL_VERSION(4, 3, 0)
		inet_twsk_deschedule_put(inet_twsk(sk));
#else
		inet_twsk_deschedule(inet_twsk(sk), &tcp_death_row);
		inet_twsk_put(inet_twsk(sk));
#endif
	} else {
		tcp_done(sk);
		sock_put(sk);
	}
}

static int dts_pton(struct dts_inet *in)
{
	char *p, *end;
	if (in4_pton(in->p, -1, (void *)in->addr, -1, (const char **)&end)) {
		in->ipv6 = 0;
	} else if (in6_pton(in->p, -1, (void *)in->addr, -1, (const char **)&end)) {
		in->ipv6 = 1;
	} else return -EINVAL;

	p = (end += 1);
	while (*p && isdigit(*p)) p++;
	*p = 0; // kstrtoXX requires 0 at the end

	return kstrtou16(end, 10, &in->port);
}

static void dts_process(struct dts_pernet *dts, struct dts_data *d)
{
	char *p = d->data;
	struct dts_inet src, dst;

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wmisleading-indentation"
	while (*p && p < d->data + d->len) {
		while (*p && isspace(*p)) p++; if (!*p) return; // skip spaces
		src.p = p;
		while (*p && !isspace(*p)) p++; if (!*p) return; // skip non-spaces

		while (*p && isspace(*p)) p++; if (!*p) return; // skip spaces
		dst.p = p;
		while (*p && !isspace(*p)) p++; if (!*p) return; // skip non-spaces

		if ((dts_pton(&src) || dts_pton(&dst)) || (src.ipv6 != dst.ipv6))
			break;

		dts_kill(dts->net, &src, &dst), p++;
	}
#pragma GCC diagnostic pop
}

static int dts_proc_open(struct inode *inode, struct file *file)
{
	struct dts_data *d = kzalloc(PAGE_SIZE, GFP_KERNEL);
	if (!d) return -ENOMEM;
	d->available = PAGE_SIZE - (sizeof(*d)+1);
	file->private_data = d;
	return 0;
}

static ssize_t dts_proc_write(struct file *file, const char __user *buf, size_t size, loff_t *pos)
{
	struct dts_data *d = file->private_data;

	if (d->len + size > d->available) {
		size_t new_available = d->available + roundup(size, PAGE_SIZE);
		struct dts_data *dnew = krealloc(d, new_available + (sizeof(*d)+1), GFP_KERNEL);
		if (!dnew) {
			kfree(d), file->private_data = NULL;
			return -ENOMEM;
		}
		(d = dnew)->available = new_available;
		file->private_data = d; // update
	}

	if (copy_from_user(d->data + d->len, buf, size))
		return -EFAULT;
	d->data[(d->len += size)] = 0;

	return size;
}

static int dts_proc_release(struct inode *inode, struct file *file)
{
	struct dts_data *d = file->private_data;
	if (d) {
#if LINUX_VERSION_CODE >= KERNEL_VERSION(3, 10, 0)
		dts_process(PDE_DATA(file_inode(file)), d);
#else
		dts_process(PDE(file->f_path.dentry->d_inode)->data, d);
#endif
		kfree(d), file->private_data = NULL;
	} return 0;
}

#if LINUX_VERSION_CODE >= KERNEL_VERSION(5, 6, 0)
static const struct proc_ops dts_proc_fops = {
	.proc_open = dts_proc_open,
	.proc_write = dts_proc_write,
	.proc_release = dts_proc_release,
};
#else
static const struct file_operations dts_proc_fops = {
	.owner = THIS_MODULE,
	.open = dts_proc_open,
	.write = dts_proc_write,
	.release = dts_proc_release,
};
#endif

static int dts_pernet_id = 0;

#if LINUX_VERSION_CODE >= KERNEL_VERSION(2, 6, 33)
static int dts_pernet_init(struct net *net)
{
	struct dts_pernet *dts = net_generic(net, dts_pernet_id);
	dts->net = net;
	dts->pde = proc_create_data(DTS_PDE_NAME, 0600, net->proc_net, &dts_proc_fops, dts);
	return !dts->pde;
}
static void dts_pernet_exit(struct net* net)
{
	struct dts_pernet *dts = net_generic(net, dts_pernet_id);
	BUG_ON(!dts->pde);
	remove_proc_entry(DTS_PDE_NAME, net->proc_net);
}
#else
static int dts_pernet_init(struct net *net)
{
	struct dts_pernet *dts = NULL;

	dts = kzalloc(sizeof(*dts), GFP_KERNEL);
	if (!dts) return -ENOMEM;

	dts->net = net;
	dts->pde = proc_create_data(DTS_PDE_NAME, 0600, net->proc_net, &dts_proc_fops, dts);
	if (!dts->pde) {
		kfree(dts);
		return -ENOMEM;
	}

	if (net_assign_generic(net, dts_pernet_id, dts)) {
		kfree(dts);
		return -ENOMEM;
	}

	return 0;
}
static void dts_pernet_exit(struct net* net)
{
	struct dts_pernet *dts = net_generic(net, dts_pernet_id);
	net_assign_generic(net, dts_pernet_id, NULL);
	BUG_ON(!dts->pde);
	remove_proc_entry(DTS_PDE_NAME, net->proc_net);
	kfree(dts);
}
#endif

static struct pernet_operations dts_pernet_ops = {
	.init = dts_pernet_init,
	.exit = dts_pernet_exit,
#if LINUX_VERSION_CODE >= KERNEL_VERSION(2, 6, 33)
	.id = &dts_pernet_id,
	.size = sizeof(struct dts_pernet),
#endif
};

static inline int dts_register_pernet(void)
{
#if LINUX_VERSION_CODE >= KERNEL_VERSION(2, 6, 33)
	return register_pernet_subsys(&dts_pernet_ops);
#else
	return register_pernet_gen_subsys(&dts_pernet_id, &dts_pernet_ops);
#endif
}

static inline void dts_unregister_pernet(void)
{
#if LINUX_VERSION_CODE >= KERNEL_VERSION(2, 6, 33)
	unregister_pernet_subsys(&dts_pernet_ops);
#else
	unregister_pernet_gen_subsys(dts_pernet_id, &dts_pernet_ops);
#endif
}

////////////////////////////////////////////////////////////////////////////////

int init_module(void)
{
	int res = 0;

	res = dts_register_pernet();
	if (res) return res;

	return 0;
}

void cleanup_module(void)
{
	dts_unregister_pernet();
}

MODULE_AUTHOR("Ilya V. Matveychikov <matvejchikov@gmail.com>");
MODULE_LICENSE("GPL");
