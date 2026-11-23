// SPDX-License-Identifier: AGPL-3.0-or-later OR Apache-2.0
/*
 * Copyright (c) 2025-2026 SPHARX Ltd. All Rights Reserved.
 *
 * macro_cfg_test.c —— macro-superv.conf 严格装载的单测（TAP）
 *
 * 覆盖：内置默认值、出厂配置与默认值逐键一致、合法覆盖、未知分组与
 * 未知键、分组前的键、非法动作名、相对设备路径、行内注释、缺文件，
 * 以及故障码到裁决动作的查询。
 */
#define _POSIX_C_SOURCE 200809L

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <linux/airymax/error.h>

#include "macro_d.h"

#ifndef MACRO_SHIPPED_CONF
#define MACRO_SHIPPED_CONF ""
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
	char *dir = strdup("/tmp/macro-cfg-XXXXXX");

	if (!dir)
		return NULL;
	if (!mkdtemp(dir)) {
		free(dir);
		return NULL;
	}
	return dir;
}

static int load_text(const char *dir, const char *text, struct macro_cfg *cfg,
		     char *err, size_t len)
{
	char path[MACRO_PATH_MAX];
	FILE *fp;

	snprintf(path, sizeof(path), "%s/macro-superv.conf", dir);
	fp = fopen(path, "w");
	if (!fp)
		return -AIRY_EIO;
	fputs(text, fp);
	fclose(fp);
	return macro_cfg_load(path, cfg, err, len);
}

static int same(const struct macro_cfg *a, const struct macro_cfg *b)
{
	size_t i;

	if (strcmp(a->dev, b->dev) != 0)
		return 0;
	for (i = 0; i < MACRO_FAULT_NR; i++) {
		if (a->act[i] != b->act[i])
			return 0;
	}
	return 1;
}

int main(void)
{
	struct macro_cfg def, cfg;
	char err[192];
	char path[MACRO_PATH_MAX];
	char *dir;
	int ret;

	ret = macro_cfg_def(&def);
	ok(ret == 0, "默认值可生成");
	ok(strcmp(def.dev, "/dev/airy_superv") == 0,
	   "默认设备为 /dev/airy_superv");
	ok(macro_cfg_act(&def, AIRY_FAULT_CAP_FORGED) ==
	   AIRY_ADJUD_TERMINATE, "CAP_FORGED 默认终止");
	ok(macro_cfg_act(&def, AIRY_FAULT_CAP_LEAK) ==
	   AIRY_ADJUD_TERMINATE, "CAP_LEAK 默认终止");
	ok(macro_cfg_act(&def, AIRY_FAULT_RING_CORRUPT) ==
	   AIRY_ADJUD_PAUSE, "RING_CORRUPT 默认暂停");
	ok(macro_cfg_act(&def, AIRY_FAULT_TIMEOUT) ==
	   AIRY_ADJUD_DEGRADE, "TIMEOUT 默认降级");
	ok(macro_cfg_act(&def, AIRY_FAULT_ABNORMAL_CAP) ==
	   AIRY_ADJUD_WARN, "ABNORMAL_CAP 默认告警");
	ok(macro_cfg_act(&def, AIRY_FAULT_VM_FAULT) ==
	   AIRY_ADJUD_WARN, "VM_FAULT 默认告警");
	ok(macro_cfg_act(&def, 0x7fff) == AIRY_ADJUD_WARN,
	   "未知故障码退回告警");
	ok(strcmp(macro_fault(AIRY_FAULT_TIMEOUT), "TIMEOUT") == 0,
	   "故障码有稳定日志名");
	ok(strcmp(macro_verdict(AIRY_ADJUD_PAUSE), "PAUSE") == 0,
	   "裁决动作有稳定日志名");

	/* 出厂配置与内置默认值漂移时，装机后的实际行为取决于读的是哪一份 */
	if (MACRO_SHIPPED_CONF[0]) {
		ret = macro_cfg_load(MACRO_SHIPPED_CONF, &cfg, err, sizeof(err));
		ok(ret == 0, "出厂 macro-superv.conf 可装载");
		ok(same(&def, &cfg), "出厂配置与内置默认值逐键一致");
	}

	dir = prep();
	if (!dir) {
		printf("Bail out! 无法创建临时目录\n");
		return 1;
	}

	ret = load_text(dir,
			"[superv]\n"
			"device = /dev/test_superv\n"
			"[policy]\n"
			"timeout = terminate\n"
			"vm_fault = pause\n",
			&cfg, err, sizeof(err));
	ok(ret == 0, "合法覆盖可装载");
	ok(strcmp(cfg.dev, "/dev/test_superv") == 0, "设备覆盖生效");
	ok(macro_cfg_act(&cfg, AIRY_FAULT_TIMEOUT) ==
	   AIRY_ADJUD_TERMINATE, "裁决动作覆盖生效");
	ok(macro_cfg_act(&cfg, AIRY_FAULT_VM_FAULT) ==
	   AIRY_ADJUD_PAUSE, "第二处覆盖生效");
	ok(macro_cfg_act(&cfg, AIRY_FAULT_CAP_LEAK) ==
	   AIRY_ADJUD_TERMINATE, "未覆盖项保留默认值");

	ret = load_text(dir,
			"[superv]\ndevice = /dev/x # 行内注释\n",
			&cfg, err, sizeof(err));
	ok(ret == 0 && strcmp(cfg.dev, "/dev/x") == 0,
	   "行内注释不影响取值");

	ret = load_text(dir, "[other]\ndevice = /dev/x\n", &cfg, err,
			sizeof(err));
	ok(ret == -AIRY_EINVAL && err[0], "未知分组被拒且可归因");

	ret = load_text(dir, "[policy]\nbogus = warn\n", &cfg, err,
			sizeof(err));
	ok(ret == -AIRY_EINVAL && err[0], "未知键被拒且可归因");

	ret = load_text(dir, "device = /dev/x\n", &cfg, err, sizeof(err));
	ok(ret == -AIRY_EINVAL, "分组之前的键被拒");

	ret = load_text(dir, "[policy]\ntimeout = shout\n", &cfg, err,
			sizeof(err));
	ok(ret == -AIRY_EINVAL, "非法动作名被拒");

	ret = load_text(dir, "[superv]\ndevice = dev/airy\n", &cfg, err,
			sizeof(err));
	ok(ret == -AIRY_EINVAL, "相对设备路径被拒");

	snprintf(path, sizeof(path), "%s/absent.conf", dir);
	ret = macro_cfg_load(path, &cfg, err, sizeof(err));
	ok(ret == -AIRY_ENOENT, "缺失配置返回 ENOENT");

	snprintf(path, sizeof(path), "%s/macro-superv.conf", dir);
	unlink(path);
	rmdir(dir);
	free(dir);

	if (g_bad)
		printf("# %u/%u 项失败\n", g_bad, g_nr);
	return g_bad ? 1 : 0;
}
