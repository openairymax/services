// SPDX-License-Identifier: AGPL-3.0-or-later OR Apache-2.0
/*
 * Copyright (c) 2025-2026 SPHARX Ltd. All Rights Reserved.
 *
 * macro_cfg.c —— /etc/agentrt/macro-superv.conf 的严格装载
 *
 * 只接受 [superv] 与 [policy] 两个分组。未知分组、未知键、未知动作名
 * 都判 schema 非法：监督守护拿默认值默默跑起来，比启动失败更难查。
 * 故障码与配置键的对应关系只在下面的描述表里写一次，默认值、解析与
 * 日志名都从它派生。
 */
#define _POSIX_C_SOURCE 200809L

#include <ctype.h>
#include <errno.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

#include <linux/airymax/error.h>

#include "macro_d.h"

struct macro_fault_desc {
	const char	*key;
	const char	*name;
	uint32_t	fault;
	enum airy_adjud	dflt;
};

static const struct macro_fault_desc macro_faults[MACRO_FAULT_NR] = {
	{
		"cap_forged", "CAP_FORGED",
		AIRY_FAULT_CAP_FORGED, AIRY_ADJUD_TERMINATE,
	},
	{
		"cap_leak", "CAP_LEAK",
		AIRY_FAULT_CAP_LEAK, AIRY_ADJUD_TERMINATE,
	},
	{
		"ring_corrupt", "RING_CORRUPT",
		AIRY_FAULT_RING_CORRUPT, AIRY_ADJUD_PAUSE,
	},
	{
		"timeout", "TIMEOUT",
		AIRY_FAULT_TIMEOUT, AIRY_ADJUD_DEGRADE,
	},
	{
		"abnormal_cap", "ABNORMAL_CAP",
		AIRY_FAULT_ABNORMAL_CAP, AIRY_ADJUD_WARN,
	},
	{
		"vm_fault", "VM_FAULT",
		AIRY_FAULT_VM_FAULT, AIRY_ADJUD_WARN,
	},
};

static void set_err(char *err, size_t len, const char *fmt, ...)
{
	va_list ap;

	if (!err || !len)
		return;
	va_start(ap, fmt);
	vsnprintf(err, len, fmt, ap);
	va_end(ap);
}

static char *trim(char *s)
{
	char *end;

	while (*s && isspace((unsigned char)*s))
		s++;
	end = s + strlen(s);
	while (end > s && isspace((unsigned char)end[-1]))
		end--;
	*end = '\0';
	return s;
}

/* 行内注释只在行首或空白之后开始，避免把值里的 '#' 当成注释 */
static void strip(char *s)
{
	size_t i;

	for (i = 0; s[i]; i++) {
		if (s[i] != '#' && s[i] != ';')
			continue;
		if (i > 0 && !isspace((unsigned char)s[i - 1]))
			continue;
		s[i] = '\0';
		return;
	}
}

static int take_path(const char *v, char *dst, size_t cap)
{
	size_t n = strlen(v);

	if (v[0] != '/' || n == 0 || n >= cap)
		return -AIRY_EINVAL;
	memcpy(dst, v, n + 1);
	return 0;
}

static int parse_action(const char *v, enum airy_adjud *out)
{
	static const struct {
		const char	*name;
		enum airy_adjud	act;
	} names[] = {
		{ "warn", AIRY_ADJUD_WARN },
		{ "degrade", AIRY_ADJUD_DEGRADE },
		{ "pause", AIRY_ADJUD_PAUSE },
		{ "terminate", AIRY_ADJUD_TERMINATE },
	};
	size_t i;

	for (i = 0; i < sizeof(names) / sizeof(names[0]); i++) {
		if (strcasecmp(v, names[i].name) == 0) {
			*out = names[i].act;
			return 0;
		}
	}
	return -AIRY_EINVAL;
}

static int take_policy(struct macro_cfg *cfg, const char *k, const char *v)
{
	size_t i;

	for (i = 0; i < MACRO_FAULT_NR; i++) {
		if (strcmp(k, macro_faults[i].key) == 0)
			return parse_action(v, &cfg->act[i]);
	}
	return -AIRY_ENOENT;
}

enum airy_adjud macro_cfg_act(const struct macro_cfg *cfg, uint32_t fault)
{
	size_t i;

	for (i = 0; i < MACRO_FAULT_NR; i++) {
		if (macro_faults[i].fault == fault)
			return cfg->act[i];
	}
	return AIRY_ADJUD_WARN;
}

const char *macro_fault(uint32_t fault)
{
	size_t i;

	for (i = 0; i < MACRO_FAULT_NR; i++) {
		if (macro_faults[i].fault == fault)
			return macro_faults[i].name;
	}
	return "UNKNOWN";
}

const char *macro_verdict(enum airy_adjud act)
{
	switch (act) {
	case AIRY_ADJUD_WARN:
		return "WARN";
	case AIRY_ADJUD_DEGRADE:
		return "DEGRADE";
	case AIRY_ADJUD_PAUSE:
		return "PAUSE";
	case AIRY_ADJUD_TERMINATE:
		return "TERMINATE";
	case AIRY_ADJUD_RESUME:
		return "RESUME";
	default:
		return "UNKNOWN";
	}
}

int macro_cfg_def(struct macro_cfg *cfg)
{
	size_t i;

	if (!cfg)
		return -AIRY_EINVAL;
	memset(cfg, 0, sizeof(*cfg));
	snprintf(cfg->dev, sizeof(cfg->dev), "%s", AIRY_SUPERV_DEV_PATH);
	for (i = 0; i < MACRO_FAULT_NR; i++)
		cfg->act[i] = macro_faults[i].dflt;
	return 0;
}

int macro_cfg_load(const char *path, struct macro_cfg *cfg, char *err,
		   size_t len)
{
	enum { SEC_NONE = 0, SEC_SUPERV, SEC_POLICY } sec = SEC_NONE;
	FILE *fp;
	char line[MACRO_LINE_MAX];
	unsigned int nr = 0;
	int ret = 0;

	if (!path || !cfg)
		return -AIRY_EINVAL;
	set_err(err, len, "%s", "");
	if (macro_cfg_def(cfg))
		return -AIRY_EINVAL;

	fp = fopen(path, "r");
	if (!fp) {
		set_err(err, len, "%s: 无法打开", path);
		return errno == ENOENT ? -AIRY_ENOENT : -AIRY_EIO;
	}

	while (fgets(line, sizeof(line), fp)) {
		char *s, *eq, *k, *v;
		int rc;

		nr++;
		if (!strchr(line, '\n') && !feof(fp)) {
			set_err(err, len, "%s:%u: 行超过 %d 字节",
				path, nr, MACRO_LINE_MAX - 2);
			ret = -AIRY_EINVAL;
			break;
		}
		strip(line);
		s = trim(line);
		if (!*s)
			continue;

		if (*s == '[') {
			char *close = strrchr(s, ']');

			if (!close || close[1]) {
				set_err(err, len, "%s:%u: 分组行格式非法",
					path, nr);
				ret = -AIRY_EINVAL;
				break;
			}
			*close = '\0';
			s = trim(s + 1);
			if (strcmp(s, "superv") == 0) {
				sec = SEC_SUPERV;
			} else if (strcmp(s, "policy") == 0) {
				sec = SEC_POLICY;
			} else {
				set_err(err, len, "%s:%u: 未知分组 %s",
					path, nr, s);
				ret = -AIRY_EINVAL;
				break;
			}
			continue;
		}

		if (sec == SEC_NONE) {
			set_err(err, len, "%s:%u: 键出现在分组之前",
				path, nr);
			ret = -AIRY_EINVAL;
			break;
		}

		eq = strchr(s, '=');
		if (!eq) {
			set_err(err, len, "%s:%u: 缺少 '='", path, nr);
			ret = -AIRY_EINVAL;
			break;
		}
		*eq = '\0';
		k = trim(s);
		v = trim(eq + 1);
		if (sec == SEC_SUPERV) {
			rc = strcmp(k, "device") == 0 ?
				take_path(v, cfg->dev, sizeof(cfg->dev)) :
				-AIRY_ENOENT;
		} else {
			rc = take_policy(cfg, k, v);
		}
		if (rc == -AIRY_ENOENT) {
			set_err(err, len, "%s:%u: 未知键 %s", path, nr, k);
			ret = -AIRY_EINVAL;
			break;
		}
		if (rc) {
			set_err(err, len, "%s:%u: %s 取值非法", path, nr, k);
			ret = -AIRY_EINVAL;
			break;
		}
	}

	if (!ret && ferror(fp)) {
		set_err(err, len, "%s: 读取失败", path);
		ret = -AIRY_EIO;
	}
	fclose(fp);
	return ret;
}
