// SPDX-License-Identifier: AGPL-3.0-or-later OR Apache-2.0
/*
 * Copyright (c) 2025-2026 SPHARX Ltd. All Rights Reserved.
 *
 * cfg_sys.c —— 配置落值与变更监听
 *
 * 落值只写内核已注册的 kernel.airy.* sysctl：节点不存在视为本内核
 * 未启用该项而跳过，存在但写入失败或回读不一致则整次落值判失败。
 * 静默吞掉不一致等于让「配置已生效」变成一句无法验证的断言。
 *
 * 监听覆盖 /etc/agentrt 与 conf.d 两级目录，事件只置位待重载标志，
 * 由主循环合并处理，避免一次写入触发多轮装载。
 *
 * SSoT: docs-linux/20-modules/11-unified-config.md §3.2、§4.1
 */
#define _GNU_SOURCE

#include <errno.h>
#include <fcntl.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/inotify.h>
#include <unistd.h>

#include <linux/airymax/error.h>

#include "cfg_int.h"

struct cfg_sys {
	const char *path;
	size_t off;
};

static const struct cfg_sys g_cfg_sys[] = {
	{ "/proc/sys/kernel/airy/sched_rt_runtime_us",
	  offsetof(struct airy_cfg, sched_rt) },
	{ "/proc/sys/kernel/airy/log_min_level",
	  offsetof(struct airy_cfg, log_level) },
	{ "/proc/sys/kernel/airy/ipc_ring_size",
	  offsetof(struct airy_cfg, ipc_ring) },
	{ "/proc/sys/kernel/airy/superv_heartbeat_interval",
	  offsetof(struct airy_cfg, beat_iv) },
	{ "/proc/sys/kernel/airy/cap_max_agents",
	  offsetof(struct airy_cfg, cap_max) },
	{ "/proc/sys/kernel/airy/cap_epoch_bits",
	  offsetof(struct airy_cfg, cap_epoch) },
	{ "/proc/sys/kernel/airy/cap_randtag_bits",
	  offsetof(struct airy_cfg, cap_tag) },
	{ "/proc/sys/kernel/airy/ipc_fastpath_budget_ns",
	  offsetof(struct airy_cfg, ipc_fast) },
};

#define CFG_SYS_NR (sizeof(g_cfg_sys) / sizeof(g_cfg_sys[0]))

static int write_val(const char *path, int val)
{
	char buf[32];
	int n;
	int fd;

	n = snprintf(buf, sizeof(buf), "%d\n", val);
	fd = open(path, O_WRONLY | O_CLOEXEC);
	if (fd < 0)
		return errno == ENOENT ? 1 : -AIRY_EIO;
	if (write(fd, buf, (size_t)n) != n) {
		close(fd);
		return -AIRY_EIO;
	}
	close(fd);
	return AIRY_EOK;
}

static int read_val(const char *path, int *out)
{
	char buf[32];
	ssize_t n;
	long v;
	int fd;

	fd = open(path, O_RDONLY | O_CLOEXEC);
	if (fd < 0)
		return -AIRY_EIO;
	n = read(fd, buf, sizeof(buf) - 1);
	close(fd);
	if (n <= 0)
		return -AIRY_EIO;
	buf[n] = '\0';
	v = strtol(buf, NULL, 10);
	*out = (int)v;
	return AIRY_EOK;
}

int cfg_apply(const struct airy_cfg *cfg, char *err, size_t len)
{
	size_t i;

	for (i = 0; i < CFG_SYS_NR; i++) {
		int val = *(const int *)((const char *)cfg + g_cfg_sys[i].off);
		int back = 0;
		int rc;

		rc = write_val(g_cfg_sys[i].path, val);
		if (rc == 1)
			continue;
		if (rc) {
			snprintf(err, len, "%s: 写入失败", g_cfg_sys[i].path);
			return rc;
		}
		if (read_val(g_cfg_sys[i].path, &back) || back != val) {
			snprintf(err, len, "%s: 回读 %d，期望 %d",
				 g_cfg_sys[i].path, back, val);
			return -AIRY_EIO;
		}
	}

	return AIRY_EOK;
}

static int add_watch(int fd, const char *path)
{
	uint32_t mask = IN_CLOSE_WRITE | IN_MOVED_TO | IN_DELETE |
			IN_CREATE | IN_ATTRIB;

	return inotify_add_watch(fd, path, mask) < 0 ? -errno : 0;
}

int cfg_drop(int fd, const char *root)
{
	char dir[AIRY_CFG_PATHMAX];

	snprintf(dir, sizeof(dir), "%s/%s", root, AIRY_CFG_DROPIN);
	/* 覆盖目录可选：缺失时由后续事件补挂，不改变调用方结论 */
	return add_watch(fd, dir);
}

int cfg_watch(const char *root, char *err, size_t len)
{
	int fd;
	int rc;

	fd = inotify_init1(IN_NONBLOCK | IN_CLOEXEC);
	if (fd < 0) {
		snprintf(err, len, "inotify: %s", strerror(errno));
		return -AIRY_EIO;
	}

	/* 配置根必须可监听：监听不到就无法宣称热加载成立 */
	rc = add_watch(fd, root);
	if (rc) {
		snprintf(err, len, "监听 %s 失败: %s", root, strerror(-rc));
		close(fd);
		return -AIRY_EIO;
	}

	cfg_drop(fd, root);
	return fd;
}

int cfg_drain(int fd)
{
	char buf[4096] __attribute__((aligned(__alignof__(struct inotify_event))));
	ssize_t n;
	int seen = 0;

	while ((n = read(fd, buf, sizeof(buf))) > 0)
		seen += (int)n;
	return seen;
}
