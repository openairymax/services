// SPDX-License-Identifier: AGPL-3.0-or-later OR Apache-2.0
/*
 * Copyright (c) 2025-2026 SPHARX Ltd. All Rights Reserved.
 *
 * config_d_test.c —— 配置守护装载与快照语义的单测（TAP 13）
 *
 * 覆盖：默认值、三类文件合成、conf.d 字典序覆盖、无前缀文件忽略、
 * 版本闸门、未知键、越界、缺主配置、快照新旧隔离。
 */
#define _POSIX_C_SOURCE 200809L

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include <linux/airymax/error.h>

#include "cfg.h"

static unsigned int g_nr;
static unsigned int g_bad;

static void ok(int cond, const char *name)
{
	g_nr++;
	if (cond) {
		printf("ok %u - %s\n", g_nr, name);
		return;
	}
	g_bad++;
	printf("not ok %u - %s\n", g_nr, name);
}

static int wf(const char *dir, const char *name, const char *text)
{
	char path[AIRY_CFG_PATHMAX];
	FILE *fp;

	snprintf(path, sizeof(path), "%s/%s", dir, name);
	fp = fopen(path, "w");
	if (!fp)
		return -1;
	fputs(text, fp);
	fclose(fp);
	return 0;
}

static char *prep(void)
{
	char *dir = strdup("/tmp/cfgd-XXXXXX");

	if (!dir)
		return NULL;
	if (!mkdtemp(dir)) {
		free(dir);
		return NULL;
	}
	return dir;
}

static void scrub(const char *dir)
{
	static const char *const files[] = {
		"agentrt.conf", "config.conf", "conf.d/10-a.conf",
		"conf.d/20-b.conf", "conf.d/local.conf",
	};
	char path[AIRY_CFG_PATHMAX];
	size_t i;

	for (i = 0; i < sizeof(files) / sizeof(files[0]); i++) {
		snprintf(path, sizeof(path), "%s/%s", dir, files[i]);
		unlink(path);
	}
	snprintf(path, sizeof(path), "%s/conf.d", dir);
	rmdir(path);
	rmdir(dir);
}

static char *dropdir(const char *dir)
{
	char *p = malloc(AIRY_CFG_PATHMAX);

	if (p) {
		snprintf(p, AIRY_CFG_PATHMAX, "%s/conf.d", dir);
		mkdir(p, 0755);
	}
	return p;
}

static void test_default(void)
{
	struct airy_cfg cfg;

	cfg_default(&cfg);
	ok(cfg.version == 1 && cfg.log_level == 1 && cfg.log_ring == 4096,
	   "默认值符合 SSoT");
	ok(cfg.ipc_ring == 1024 && cfg.ipc_fast == 10 && cfg.sched_rt == 950000,
	   "IPC 与调度默认值");
	ok(cfg.cap_max == 1024 && cfg.cap_epoch == 16 && cfg.cap_tag == 32,
	   "能力面默认值");
	ok(cfg_valid(&cfg) == AIRY_EOK, "默认值通过校验");
}

static void test_full(void)
{
	static const char *const text =
		"# 全局配置\n"
		"[main]\n"
		"version = 1\n"
		"\n"
		"[log]\n"
		"min_level = 6 # info\n"
		"ring_capacity = 8192\n"
		"\n"
		"[ipc]\n"
		"ring_size = 2048\n"
		"fastpath_budget_ns = 25\n"
		"\n"
		"[sched]\n"
		"rt_runtime_us = 900000\n"
		"deadline_period_us = 40000\n"
		"\n"
		"[superv]\n"
		"heartbeat_interval = 2\n"
		"heartbeat_timeout = 60\n"
		"\n"
		"[cap]\n"
		"max_agents = 512\n"
		"epoch_bits = 32\n"
		"randtag_bits = 16\n"
		"forge_threshold = 200\n";
	struct airy_cfg cfg;
	char err[256] = "";
	char *dir = prep();
	int rc;

	if (!dir) {
		ok(0, "临时目录可用");
		return;
	}
	wf(dir, "agentrt.conf", text);
	rc = cfg_load(dir, &cfg, err, sizeof(err));
	ok(rc == 0, "完整配置装载成功");
	ok(cfg.log_level == 6 && cfg.log_ring == 8192, "日志段落值");
	ok(cfg.ipc_ring == 2048 && cfg.ipc_fast == 25, "IPC 段落值");
	ok(cfg.sched_rt == 900000 && cfg.sched_dl == 40000, "调度段落值");
	ok(cfg.beat_iv == 2 && cfg.beat_to == 60, "监管段落值");
	ok(cfg.cap_max == 512 && cfg.cap_epoch == 32 && cfg.cap_tag == 16,
	   "能力段落值");
	scrub(dir);
	free(dir);
}

static void test_drop(void)
{
	struct airy_cfg cfg;
	char err[256] = "";
	char *dir = prep();
	char *dd;

	if (!dir) {
		ok(0, "临时目录可用");
		return;
	}
	dd = dropdir(dir);
	wf(dir, "agentrt.conf", "[main]\nversion = 1\n[log]\nmin_level = 6\n");
	wf(dir, "conf.d/10-a.conf", "[log]\nmin_level = 3\n");
	wf(dir, "conf.d/20-b.conf", "[log]\nmin_level = 4\n");
	wf(dir, "conf.d/local.conf", "[log]\nmin_level = 0\n");
	ok(dd && cfg_load(dir, &cfg, err, sizeof(err)) == 0,
	   "带覆盖文件装载成功");
	ok(cfg.log_level == 4, "conf.d 按字典序后者覆盖");
	free(dd);
	scrub(dir);
	free(dir);
}

static void test_version(void)
{
	struct airy_cfg cfg;
	char err[256] = "";
	char *dir = prep();

	if (!dir) {
		ok(0, "临时目录可用");
		return;
	}
	wf(dir, "agentrt.conf", "[main]\nversion = 2\n");
	ok(cfg_load(dir, &cfg, err, sizeof(err)) == -AIRY_ECFGVERSION,
	   "版本不匹配返回 -101");
	scrub(dir);
	free(dir);
}

static void test_schema(void)
{
	struct airy_cfg cfg;
	char err[256] = "";
	char *dir = prep();

	if (!dir) {
		ok(0, "临时目录可用");
		return;
	}
	wf(dir, "agentrt.conf", "[main]\nversion = 1\n[log]\nnot_a_key = 1\n");
	ok(cfg_load(dir, &cfg, err, sizeof(err)) == -AIRY_ECFGSCHEMA,
	   "未知键判 schema 非法");
	wf(dir, "agentrt.conf", "[main]\nversion = 1\n[log]\nmin_level = 9\n");
	ok(cfg_load(dir, &cfg, err, sizeof(err)) == -AIRY_ECFGSCHEMA,
	   "越界取值判 schema 非法");
	wf(dir, "agentrt.conf",
	   "[main]\nversion = 1\n[cap]\nepoch_bits = 24\n");
	ok(cfg_load(dir, &cfg, err, sizeof(err)) == -AIRY_ECFGSCHEMA,
	   "离散取值只接受 8/16/32");
	scrub(dir);
	free(dir);
}

static void test_missing(void)
{
	struct airy_cfg cfg;
	char err[256] = "";

	ok(cfg_load("/tmp/cfgd-absent-root", &cfg, err, sizeof(err)) ==
	   -AIRY_ECFGIO, "缺主配置返回 -105");
}

static void test_snap(void)
{
	struct airy_cfg a;
	struct airy_cfg b;
	const struct cfg_snap *old;
	const struct cfg_snap *cur;

	cfg_default(&a);
	b = a;
	b.log_level = 5;

	cfg_publish(&a);
	old = cfg_hold();
	cfg_publish(&b);
	cur = cfg_hold();

	ok(cfg_view(old) && cfg_view(old)->log_level == 1,
	   "旧快照对持有者保持不变");
	ok(cfg_view(cur) && cfg_view(cur)->log_level == 5,
	   "新快照立即对所有新读者可见");
	ok(!cfg_same(cfg_view(old), cfg_view(cur)), "新旧快照可区分");
	cfg_put(cur);
	cfg_put(old);
}

int main(void)
{
	test_default();
	test_full();
	test_drop();
	test_version();
	test_schema();
	test_missing();
	test_snap();

	printf("1..%u\n", g_nr);
	return g_bad ? 1 : 0;
}
