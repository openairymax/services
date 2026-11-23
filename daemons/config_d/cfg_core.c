// SPDX-License-Identifier: AGPL-3.0-or-later OR Apache-2.0
/*
 * Copyright (c) 2025-2026 SPHARX Ltd. All Rights Reserved.
 *
 * cfg_core.c —— 配置模型、取值校验与生效快照发布
 *
 * 快照发布采用「原子指针 + 引用计数」：读者在锁内取到当前快照并加
 * 引用，锁外读取；写者换指针后释放自己那一份引用。旧快照只在引用
 * 归零时回收，读者永远不会看到半新半旧的配置。
 *
 * SSoT: docs-linux/20-modules/11-unified-config.md §3.2、§4
 */
#define _POSIX_C_SOURCE 200809L

#include <pthread.h>
#include <stdarg.h>
#include <stdatomic.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include <linux/airymax/error.h>

#include "cfg_int.h"

#define AIRY_CFG_LEVELS 8

struct cfg_snap {
	struct airy_cfg cfg;
	atomic_uint refs;
};

static pthread_mutex_t g_cfg_lock = PTHREAD_MUTEX_INITIALIZER;
static struct cfg_snap *g_cfg_live;

/* [SS] 语义映射表的唯一实现：内核 sysctl 名与 INI 键一一对应 */
const struct cfg_key g_cfg_keys[] = {
	{ .sec = "main", .key = "version",
	  .off = offsetof(struct airy_cfg, version), .gate = true,
	  .frozen = true },
	{ .sec = "log", .key = "min_level",
	  .off = offsetof(struct airy_cfg, log_level),
	  .min = 0, .max = AIRY_CFG_LEVELS - 1 },
	{ .sec = "log", .key = "ring_capacity",
	  .off = offsetof(struct airy_cfg, log_ring),
	  .min = 256, .max = 1048576 },
	{ .sec = "ipc", .key = "ring_size",
	  .off = offsetof(struct airy_cfg, ipc_ring), .min = 16, .max = 65536 },
	{ .sec = "ipc", .key = "fastpath_budget_ns",
	  .off = offsetof(struct airy_cfg, ipc_fast), .min = 5, .max = 50 },
	{ .sec = "sched", .key = "rt_runtime_us",
	  .off = offsetof(struct airy_cfg, sched_rt),
	  .min = 0, .max = 1000000 },
	{ .sec = "sched", .key = "deadline_period_us",
	  .off = offsetof(struct airy_cfg, sched_dl),
	  .min = 1000, .max = 1000000 },
	{ .sec = "superv", .key = "heartbeat_interval",
	  .off = offsetof(struct airy_cfg, beat_iv), .min = 1, .max = 300 },
	{ .sec = "superv", .key = "heartbeat_timeout",
	  .off = offsetof(struct airy_cfg, beat_to), .min = 2, .max = 3600 },
	{ .sec = "cap", .key = "max_agents",
	  .off = offsetof(struct airy_cfg, cap_max), .min = 1, .max = 1024,
	  .frozen = true },
	{ .sec = "cap", .key = "epoch_bits",
	  .off = offsetof(struct airy_cfg, cap_epoch), .min = 8, .max = 32,
	  .set = (1ULL << 8) | (1ULL << 16) | (1ULL << 32) },
	{ .sec = "cap", .key = "randtag_bits",
	  .off = offsetof(struct airy_cfg, cap_tag), .min = 16, .max = 32,
	  .set = (1ULL << 16) | (1ULL << 32) },
	{ .sec = "cap", .key = "forge_threshold",
	  .off = offsetof(struct airy_cfg, cap_forge), .min = 10, .max = 10000 },
};

const size_t g_cfg_keynr = sizeof(g_cfg_keys) / sizeof(g_cfg_keys[0]);

bool cfg_key_ok(const struct cfg_key *k, int v)
{
	if (k->set)
		return v >= 0 && v < 64 && (k->set & (1ULL << v)) != 0;
	return v >= k->min && v <= k->max;
}

void cfg_default(struct airy_cfg *cfg)
{
	memset(cfg, 0, sizeof(*cfg));
	cfg->version = AIRY_CFG_VERSION;
	cfg->log_level = 1;
	cfg->log_ring = 4096;
	cfg->ipc_ring = 1024;
	cfg->ipc_fast = 10;
	cfg->sched_rt = 950000;
	cfg->sched_dl = 50000;
	cfg->beat_iv = 1;
	cfg->beat_to = 30;
	cfg->cap_max = 1024;
	cfg->cap_epoch = 16;
	cfg->cap_tag = 32;
	cfg->cap_forge = 100;
}

int cfg_valid(const struct airy_cfg *cfg)
{
	size_t i;

	for (i = 0; i < g_cfg_keynr; i++) {
		const struct cfg_key *k = &g_cfg_keys[i];
		int v = *(const int *)((const char *)cfg + k->off);

		if (!k->gate && !cfg_key_ok(k, v))
			return -AIRY_ECFGSCHEMA;
	}

	return AIRY_EOK;
}

bool cfg_same(const struct airy_cfg *a, const struct airy_cfg *b)
{
	return memcmp(a, b, sizeof(*a)) == 0;
}

int cfg_publish(const struct airy_cfg *cfg)
{
	struct cfg_snap *snap;
	struct cfg_snap *old;

	snap = calloc(1, sizeof(*snap));
	if (!snap)
		return -AIRY_ENOMEM;

	snap->cfg = *cfg;
	atomic_init(&snap->refs, 1);

	pthread_mutex_lock(&g_cfg_lock);
	old = g_cfg_live;
	g_cfg_live = snap;
	pthread_mutex_unlock(&g_cfg_lock);

	cfg_put((const struct cfg_snap *)old);
	return AIRY_EOK;
}

const struct cfg_snap *cfg_hold(void)
{
	struct cfg_snap *snap;

	pthread_mutex_lock(&g_cfg_lock);
	snap = g_cfg_live;
	if (snap)
		atomic_fetch_add_explicit(&snap->refs, 1, memory_order_relaxed);
	pthread_mutex_unlock(&g_cfg_lock);

	return snap;
}

const struct airy_cfg *cfg_view(const struct cfg_snap *snap)
{
	return snap ? &snap->cfg : NULL;
}

void cfg_put(const struct cfg_snap *snap)
{
	struct cfg_snap *s = (struct cfg_snap *)snap;

	if (!s)
		return;
	if (atomic_fetch_sub_explicit(&s->refs, 1, memory_order_acq_rel) == 1)
		free(s);
}

void cfg_log(const char *level, const char *fmt, ...)
{
	struct timespec ts;
	struct tm tm;
	char stamp[32];
	va_list ap;

	clock_gettime(CLOCK_REALTIME, &ts);
	localtime_r(&ts.tv_sec, &tm);
	strftime(stamp, sizeof(stamp), "%Y-%m-%dT%H:%M:%S", &tm);

	fprintf(stderr, "%s.%03ld config_d[%s] ", stamp, ts.tv_nsec / 1000000,
		level);
	va_start(ap, fmt);
	vfprintf(stderr, fmt, ap);
	va_end(ap);
	fputc('\n', stderr);
}

void cfg_dump(const struct airy_cfg *cfg)
{
	printf("[main]\nversion = %d\n", cfg->version);
	printf("[log]\nmin_level = %d\nring_capacity = %d\n",
	       cfg->log_level, cfg->log_ring);
	printf("[ipc]\nring_size = %d\nfastpath_budget_ns = %d\n",
	       cfg->ipc_ring, cfg->ipc_fast);
	printf("[sched]\nrt_runtime_us = %d\ndeadline_period_us = %d\n",
	       cfg->sched_rt, cfg->sched_dl);
	printf("[superv]\nheartbeat_interval = %d\nheartbeat_timeout = %d\n",
	       cfg->beat_iv, cfg->beat_to);
	printf("[cap]\nmax_agents = %d\nepoch_bits = %d\n"
	       "randtag_bits = %d\nforge_threshold = %d\n",
	       cfg->cap_max, cfg->cap_epoch, cfg->cap_tag, cfg->cap_forge);
}
