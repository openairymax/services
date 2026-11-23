// SPDX-License-Identifier: AGPL-3.0-or-later OR Apache-2.0
/*
 * Copyright (c) 2025-2026 SPHARX Ltd. All Rights Reserved.
 *
 * logd_cfg_test.c —— logger.conf 严格装载的单测（TAP）
 *
 * 覆盖：内置默认值、出厂配置与默认值逐键一致、合法覆盖、未知分组与
 * 未知键、分组前的键、非法尺寸/级别/相对路径、阈值同时为 0、缺文件。
 */
#define _POSIX_C_SOURCE 200809L

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <linux/airymax/error.h>

#include "logd.h"

#ifndef LOGD_SHIPPED_CONF
#define LOGD_SHIPPED_CONF ""
#endif

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

static char *prep(void)
{
	char *dir = strdup("/tmp/logd-cfg-XXXXXX");

	if (!dir)
		return NULL;
	if (!mkdtemp(dir)) {
		free(dir);
		return NULL;
	}
	return dir;
}

static int write_cfg(const char *dir, const char *text)
{
	char path[LOGD_PATH_MAX];
	FILE *fp;

	snprintf(path, sizeof(path), "%s/logger.conf", dir);
	fp = fopen(path, "w");
	if (!fp)
		return -1;
	fputs(text, fp);
	fclose(fp);
	return 0;
}

static int load_text(const char *dir, const char *text, struct logd_cfg *cfg,
		     char *err, size_t len)
{
	char path[LOGD_PATH_MAX];

	if (write_cfg(dir, text))
		return -AIRY_EIO;
	snprintf(path, sizeof(path), "%s/logger.conf", dir);
	return logd_cfg_load(path, cfg, err, len);
}

static int same(const struct logd_cfg *a, const struct logd_cfg *b)
{
	return strcmp(a->dev, b->dev) == 0 &&
	       strcmp(a->path, b->path) == 0 &&
	       a->level == b->level &&
	       a->flush_ms == b->flush_ms &&
	       a->max_bytes == b->max_bytes &&
	       a->max_age_s == b->max_age_s &&
	       a->keep == b->keep;
}

int main(void)
{
	struct logd_cfg def, cfg;
	char err[192];
	char path[LOGD_PATH_MAX];
	char *dir;
	int ret;

	ret = logd_cfg_def(&def);
	ok(ret == 0, "默认值可生成");
	ok(strcmp(def.dev, "/dev/airy_log") == 0, "默认设备为 /dev/airy_log");
	ok(strcmp(def.path, "/var/log/agentrt/airy.log") == 0,
	   "默认落盘路径");
	ok(def.level == AIRY_LOG_INFO, "默认级别 INFO");
	ok(def.flush_ms == 200, "默认 flush_ms");
	ok(def.max_bytes == 100ULL * 1024ULL * 1024ULL, "默认 max_bytes 100M");
	ok(def.max_age_s == 86400, "默认 max_age_s");
	ok(def.keep == 16, "默认 keep");

	/* 出厂配置与内置默认值漂移时，装机后的实际行为取决于读的是哪一份 */
	if (LOGD_SHIPPED_CONF[0]) {
		ret = logd_cfg_load(LOGD_SHIPPED_CONF, &cfg, err, sizeof(err));
		ok(ret == 0, "出厂 logger.conf 可装载");
		ok(same(&def, &cfg), "出厂 logger.conf 与内置默认值逐键一致");
	}

	dir = prep();
	if (!dir) {
		printf("Bail out! 无法创建临时目录\n");
		return 1;
	}

	ret = load_text(dir,
			"[logger]\n"
			"device = /dev/test_log\n"
			"path = /tmp/airy-test.log\n"
			"level = debug\n"
			"flush_ms = 0\n"
			"max_bytes = 1M\n"
			"max_age_s = 0\n"
			"keep = 3\n",
			&cfg, err, sizeof(err));
	ok(ret == 0, "合法覆盖可装载");
	ok(strcmp(cfg.dev, "/dev/test_log") == 0, "设备覆盖生效");
	ok(strcmp(cfg.path, "/tmp/airy-test.log") == 0, "路径覆盖生效");
	ok(cfg.level == AIRY_LOG_DEBUG, "级别按名字解析");
	ok(cfg.flush_ms == 0, "flush_ms 可为 0");
	ok(cfg.max_bytes == 1024ULL * 1024ULL, "尺寸后缀 K/M/G 生效");
	ok(cfg.max_age_s == 0, "max_age_s 可为 0");
	ok(cfg.keep == 3, "keep 覆盖生效");

	ret = load_text(dir, "[other]\nlevel = info\n", &cfg, err, sizeof(err));
	ok(ret == -AIRY_EINVAL && err[0], "未知分组被拒且可归因");

	ret = load_text(dir, "[logger]\nbogus = 1\n", &cfg, err, sizeof(err));
	ok(ret == -AIRY_EINVAL && err[0], "未知键被拒且可归因");

	ret = load_text(dir, "level = info\n", &cfg, err, sizeof(err));
	ok(ret == -AIRY_EINVAL, "分组之前的键被拒");

	ret = load_text(dir, "[logger]\nmax_bytes = 12X\n", &cfg, err,
			sizeof(err));
	ok(ret == -AIRY_EINVAL, "非法尺寸后缀被拒");

	ret = load_text(dir, "[logger]\nlevel = loud\n", &cfg, err,
			sizeof(err));
	ok(ret == -AIRY_EINVAL, "非法级别被拒");

	ret = load_text(dir, "[logger]\npath = relative.log\n", &cfg, err,
			sizeof(err));
	ok(ret == -AIRY_EINVAL, "相对落盘路径被拒");

	ret = load_text(dir,
			"[logger]\nmax_bytes = 0\nmax_age_s = 0\n",
			&cfg, err, sizeof(err));
	ok(ret == -AIRY_EINVAL, "轮转阈值同时为 0 被拒");

	snprintf(path, sizeof(path), "%s/absent.conf", dir);
	ret = logd_cfg_load(path, &cfg, err, sizeof(err));
	ok(ret == -AIRY_ENOENT, "缺失配置返回 ENOENT");

	snprintf(path, sizeof(path), "%s/logger.conf", dir);
	unlink(path);
	rmdir(dir);
	free(dir);

	if (g_bad)
		printf("# %u/%u 项失败\n", g_bad, g_nr);
	return g_bad ? 1 : 0;
}
