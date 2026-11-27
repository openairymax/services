// SPDX-License-Identifier: AGPL-3.0-or-later OR Apache-2.0
/*
 * Copyright (c) 2025-2026 SPHARX Ltd. All Rights Reserved.
 *
 * macro_d.c —— Micro-Supervisor 的故障裁决守护
 *
 * 独占打开 /dev/airy_superv，把一个 eventfd 绑给内核作为唤醒通道；每次
 * 唤醒后按 24 字节明细逐条排空故障环并给出裁决。事件源只有该 eventfd，
 * tick 只负责补排空、丢失计数与看门狗上报，空闲时不产生周期负载。
 * SIGHUP 重新校验整份配置：非法则整次拒绝，设备变更则先接新设备再退
 * 旧设备。
 */
#define _GNU_SOURCE

#include <errno.h>
#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sysexits.h>
#include <sys/eventfd.h>
#include <sys/ioctl.h>
#include <unistd.h>

#include <linux/airymax/error.h>

#include <airy/daemon.h>
#include <airy/uevent.h>

#include "macro_d.h"

#define MACRO_CONF	"/etc/agentrt/macro-superv.conf"

struct macro_ctx {
	struct macro_cfg	cfg;
	char			cfg_path[MACRO_PATH_MAX];
	int			dev_fd;
	int			efd;
	uint64_t		wakes;
	uint64_t		faults;
	uint64_t		lost;
	uint64_t		uev_lost;
	uint64_t		verdicts[AIRY_ADJUD_NR];
	struct airy_uev		enf;
};

static int macro_dev_open(int *dev_fd, int *efd, const char *path)
{
	__s32 arg;
	int fd, e, saved;

	fd = open(path, O_RDONLY | O_NONBLOCK | O_CLOEXEC);
	if (fd < 0)
		return -AIRY_EIO;
	e = eventfd(0, EFD_CLOEXEC | EFD_NONBLOCK);
	if (e < 0) {
		saved = errno;
		close(fd);
		errno = saved;
		return -AIRY_EIO;
	}
	arg = e;
	if (ioctl(fd, AIRY_SUPERV_IOC_BIND_EFD, &arg)) {
		saved = errno;
		close(e);
		close(fd);
		errno = saved;
		return -AIRY_EIO;
	}
	*dev_fd = fd;
	*efd = e;
	return 0;
}

static void macro_dev_close(int dev_fd, int efd)
{
	/* 先关设备：release 路径会解绑 eventfd，避免留悬空通知上下文 */
	close(dev_fd);
	close(efd);
}

static void macro_judge(struct macro_ctx *c,
			const struct airy_fault_event *ev)
{
	struct airy_user_superv_enforce ue;
	enum airy_adjud act = macro_cfg_act(&c->cfg, ev->fault_code);

	c->faults++;
	if ((unsigned int)act < AIRY_ADJUD_NR)
		c->verdicts[act]++;
	fprintf(stderr,
		"macro_d: agent=%u ring=%u fault=%s(0x%04x) "
		"ts=%lluns verdict=%s\n",
		ev->agent_id, ev->ring_id, macro_fault(ev->fault_code),
		ev->fault_code, (unsigned long long)ev->timestamp_ns,
		macro_verdict(act));
	ue.agent_id = ev->agent_id;
	ue.action = (__u32)act;
	ue.reason = ev->fault_code;
	if (airy_uev_emit(&c->enf, &ue, sizeof(ue)))
		c->uev_lost++;
}

static void macro_lost(struct macro_ctx *c)
{
	__u32 lost;

	if (ioctl(c->dev_fd, AIRY_SUPERV_IOC_LOST, &lost))
		return;
	if (lost > c->lost) {
		fprintf(stderr, "macro_d: 内核丢弃 %u 条故障明细\n",
			(unsigned int)(lost - c->lost));
		c->lost = lost;
	}
}

static void macro_drain(struct macro_ctx *c)
{
	struct airy_fault_event ev;
	ssize_t n;

	for (;;) {
		n = read(c->dev_fd, &ev, sizeof(ev));
		if (n < 0) {
			if (errno != EAGAIN)
				fprintf(stderr, "macro_d: 读取故障明细失败: %s\n",
					strerror(errno));
			break;
		}
		if (n == 0)
			break;
		if ((size_t)n != sizeof(ev)) {
			fprintf(stderr, "macro_d: 故障明细长度异常 %zd\n", n);
			break;
		}
		macro_judge(c, &ev);
	}
	macro_lost(c);
}

static int on_input(struct airy_dmn *dmn, int fd, unsigned int events,
		    void *data)
{
	struct macro_ctx *c = data;
	uint64_t count;

	(void)dmn;
	(void)fd;
	if (events & (AIRY_DMN_ERR | AIRY_DMN_HUP)) {
		fprintf(stderr, "macro_d: 监督通道已断开\n");
		return -AIRY_EIO;
	}
	if (!(events & AIRY_DMN_IN))
		return 0;
	/* 唤醒计数只用来确认唤醒来源，明细一律从设备排空 */
	if (read(c->efd, &count, sizeof(count)) == (ssize_t)sizeof(count))
		c->wakes += count;
	macro_drain(c);
	return 0;
}

static int on_tick(struct airy_dmn *dmn, void *data)
{
	struct macro_ctx *c = data;
	char msg[224];

	macro_drain(c);
	airy_dmn_beat(dmn);
	snprintf(msg, sizeof(msg),
		 "WATCHDOG=1\nSTATUS=faults=%llu warn=%llu degrade=%llu "
		 "pause=%llu terminate=%llu lost=%llu uev_lost=%llu\n",
		 (unsigned long long)c->faults,
		 (unsigned long long)c->verdicts[AIRY_ADJUD_WARN],
		 (unsigned long long)c->verdicts[AIRY_ADJUD_DEGRADE],
		 (unsigned long long)c->verdicts[AIRY_ADJUD_PAUSE],
		 (unsigned long long)c->verdicts[AIRY_ADJUD_TERMINATE],
		 (unsigned long long)c->lost,
		 (unsigned long long)c->uev_lost);
	airy_dmn_notify(msg);
	return 0;
}

static int on_start(struct airy_dmn *dmn, void *data)
{
	struct macro_ctx *c = data;
	int ret;

	(void)dmn;
	ret = airy_uev_open(&c->enf, AIRY_UEVENT_SUPERV_ENFORCE,
			    AIRY_UEVENT_FMT_SUPERV_ENFORCE);
	if (ret) {
		fprintf(stderr, "macro_d: user_events 注册失败: %s\n",
			strerror(-ret));
		return ret;
	}
	macro_drain(c);
	airy_dmn_notify("READY=1\nSTATUS=supervising\n");
	return 0;
}

static int on_reload(struct airy_dmn *dmn, void *data)
{
	struct macro_ctx *c = data;
	struct macro_cfg cfg;
	struct airy_dmn_src src;
	char err[192];
	int dev_fd, efd, ret;

	ret = macro_cfg_load(c->cfg_path, &cfg, err, sizeof(err));
	if (ret) {
		fprintf(stderr, "macro_d: 配置未生效: %s\n", err);
		return 0;
	}
	if (strcmp(cfg.dev, c->cfg.dev) != 0) {
		ret = macro_dev_open(&dev_fd, &efd, cfg.dev);
		if (ret)
			goto reject;
		memset(&src, 0, sizeof(src));
		src.fd = efd;
		src.events = AIRY_DMN_IN;
		src.recv = on_input;
		src.data = c;
		ret = airy_dmn_add(dmn, &src);
		if (ret) {
			macro_dev_close(dev_fd, efd);
			goto reject;
		}
		macro_drain(c);
		airy_dmn_del(dmn, c->efd);
		macro_dev_close(c->dev_fd, c->efd);
		c->dev_fd = dev_fd;
		c->efd = efd;
		c->lost = 0;
	}
	c->cfg = cfg;
	airy_dmn_notify("STATUS=reloaded\n");
	return 0;

reject:
	fprintf(stderr, "macro_d: 新配置不可用，保留旧配置\n");
	return 0;
}

static int on_stop(struct airy_dmn *dmn, void *data)
{
	struct macro_ctx *c = data;

	(void)dmn;
	/* 描述符归 main 所有：框架不关闭外部 fd，这里再关一次会与收尾重复 */
	macro_drain(c);
	airy_uev_close(&c->enf);
	airy_dmn_notify("STOPPING=1\n");
	return 0;
}

static int usage(const char *argv0)
{
	printf("用法: %s [--config=路径]\n", argv0);
	return 0;
}

int main(int argc, char **argv)
{
	struct airy_dmn_ops ops;
	struct airy_dmn_src src;
	struct airy_dmn_cfg dcfg;
	struct airy_dmn *dmn;
	struct macro_ctx ctx;
	const char *path = MACRO_CONF;
	char err[192];
	int ret;
	int i;

	for (i = 1; i < argc; i++) {
		if (strncmp(argv[i], "--config=", 9) == 0) {
			path = argv[i] + 9;
		} else if (strcmp(argv[i], "--config") == 0 && i + 1 < argc) {
			path = argv[++i];
		} else if (strcmp(argv[i], "--help") == 0) {
			return usage(argv[0]);
		} else {
			fprintf(stderr, "macro_d: 未知参数 %s\n", argv[i]);
			return EX_USAGE;
		}
	}
	if (strlen(path) >= sizeof(ctx.cfg_path)) {
		fprintf(stderr, "macro_d: 配置路径过长\n");
		return EX_CONFIG;
	}

	memset(&ctx, 0, sizeof(ctx));
	strcpy(ctx.cfg_path, path);
	ret = macro_cfg_load(path, &ctx.cfg, err, sizeof(err));
	if (ret) {
		fprintf(stderr, "macro_d: %s\n", err);
		return EX_CONFIG;
	}
	ret = macro_dev_open(&ctx.dev_fd, &ctx.efd, ctx.cfg.dev);
	if (ret) {
		fprintf(stderr, "macro_d: 无法接入 %s: %s\n", ctx.cfg.dev,
			strerror(errno));
		return EX_OSERR;
	}

	memset(&dcfg, 0, sizeof(dcfg));
	dcfg.name = "macro_d";
	dcfg.config = path;
	dcfg.tick_ms = 1000;
	dcfg.flags = AIRY_DMN_F_SIG;
	dmn = airy_dmn_new(&dcfg);
	if (!dmn) {
		macro_dev_close(ctx.dev_fd, ctx.efd);
		return EX_OSERR;
	}

	memset(&ops, 0, sizeof(ops));
	ops.start = on_start;
	ops.tick = on_tick;
	ops.reload = on_reload;
	ops.stop = on_stop;
	ops.data = &ctx;
	ret = airy_dmn_reg(dmn, &ops);
	if (ret)
		goto out;

	memset(&src, 0, sizeof(src));
	src.fd = ctx.efd;
	src.events = AIRY_DMN_IN;
	src.recv = on_input;
	src.data = &ctx;
	ret = airy_dmn_add(dmn, &src);
	if (ret)
		goto out;

	ret = airy_dmn_run(dmn);
	if (ret)
		fprintf(stderr, "macro_d: 主循环异常 %d\n", ret);

out:
	airy_dmn_free(dmn);
	macro_dev_close(ctx.dev_fd, ctx.efd);
	return ret ? EX_SOFTWARE : 0;
}
