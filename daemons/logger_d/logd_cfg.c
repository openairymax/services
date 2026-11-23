// SPDX-License-Identifier: AGPL-3.0-or-later OR Apache-2.0
/*
 * Copyright (c) 2025-2026 SPHARX Ltd. All Rights Reserved.
 *
 * logd_cfg.c —— /etc/agentrt/logger.conf 的严格装载
 *
 * 只接受 [logger] 一个分组。未知分组、未知键、非整数、超出行长的行
 * 都判 schema 非法：日志守护拿默认值默默跑起来，比启动失败更难查。
 * 轮转阈值必须至少启用一项，否则单文件会无界增长。
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

#include "logd.h"

#define LOGD_LINE	512
#define LOGD_AGE_MAX	(30U * 86400U)

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

static int parse_u32(const char *v, uint32_t lo, uint32_t hi,
		     uint32_t *out)
{
	char *end;
	unsigned long n;

	if (!*v)
		return -AIRY_EINVAL;
	errno = 0;
	n = strtoul(v, &end, 10);
	if (errno || *end || n < lo || n > hi)
		return -AIRY_EINVAL;
	*out = (uint32_t)n;
	return 0;
}

static int parse_size(const char *v, uint64_t *out)
{
	char *end;
	unsigned long long n;
	uint64_t mul = 1;

	errno = 0;
	n = strtoull(v, &end, 10);
	if (errno || end == v)
		return -AIRY_EINVAL;
	switch (*end) {
	case 'k':
	case 'K':
		mul = 1024ULL;
		end++;
		break;
	case 'm':
	case 'M':
		mul = 1024ULL * 1024ULL;
		end++;
		break;
	case 'g':
	case 'G':
		mul = 1024ULL * 1024ULL * 1024ULL;
		end++;
		break;
	default:
		break;
	}
	if (*end || (uint64_t)n > UINT64_MAX / mul)
		return -AIRY_EINVAL;
	*out = (uint64_t)n * mul;
	return 0;
}

static int parse_level(const char *v, uint32_t *out)
{
	static const char *const names[] = {
		"debug", "info", "warn", "error", "fatal",
	};
	size_t i;

	for (i = 0; i < sizeof(names) / sizeof(names[0]); i++) {
		if (strcasecmp(v, names[i]) == 0) {
			*out = (uint32_t)i;
			return 0;
		}
	}
	return parse_u32(v, AIRY_LOG_DEBUG, AIRY_LOG_FATAL, out);
}

static int take_path(const char *v, char *dst, size_t cap)
{
	size_t n = strlen(v);

	if (v[0] != '/' || n == 0 || n >= cap)
		return -AIRY_EINVAL;
	memcpy(dst, v, n + 1);
	return 0;
}

static int take_key(struct logd_cfg *cfg, const char *k, const char *v)
{
	if (strcmp(k, "device") == 0)
		return take_path(v, cfg->dev, sizeof(cfg->dev));
	if (strcmp(k, "path") == 0)
		return take_path(v, cfg->path, sizeof(cfg->path));
	if (strcmp(k, "level") == 0)
		return parse_level(v, &cfg->level);
	if (strcmp(k, "flush_ms") == 0)
		return parse_u32(v, 0, 60000, &cfg->flush_ms);
	if (strcmp(k, "max_bytes") == 0)
		return parse_size(v, &cfg->max_bytes);
	if (strcmp(k, "max_age_s") == 0)
		return parse_u32(v, 0, LOGD_AGE_MAX, &cfg->max_age_s);
	if (strcmp(k, "keep") == 0)
		return parse_u32(v, 1, 1024, &cfg->keep);
	return -AIRY_ENOENT;
}

int logd_cfg_def(struct logd_cfg *cfg)
{
	if (!cfg)
		return -AIRY_EINVAL;
	memset(cfg, 0, sizeof(*cfg));
	snprintf(cfg->dev, sizeof(cfg->dev), "%s", AIRY_LOG_DEV_PATH);
	snprintf(cfg->path, sizeof(cfg->path), "%s",
		 "/var/log/agentrt/airy.log");
	cfg->level	= AIRY_LOG_INFO;
	cfg->flush_ms	= 200;
	cfg->max_bytes	= 100ULL * 1024ULL * 1024ULL;
	cfg->max_age_s	= 86400;
	cfg->keep	= 16;
	return 0;
}

int logd_cfg_load(const char *path, struct logd_cfg *cfg, char *err,
		  size_t len)
{
	FILE *fp;
	char line[LOGD_LINE];
	unsigned int nr = 0;
	bool in_sec = false;
	int ret = 0;

	if (!path || !cfg)
		return -AIRY_EINVAL;
	set_err(err, len, "%s", "");
	if (logd_cfg_def(cfg))
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
				path, nr, LOGD_LINE - 2);
			ret = -AIRY_EINVAL;
			break;
		}
		s = trim(line);
		if (!*s || *s == '#' || *s == ';')
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
			if (strcmp(trim(s + 1), "logger") != 0) {
				set_err(err, len, "%s:%u: 未知分组 %s",
					path, nr, trim(s + 1));
				ret = -AIRY_EINVAL;
				break;
			}
			in_sec = true;
			continue;
		}

		if (!in_sec) {
			set_err(err, len, "%s:%u: 键出现在分组之前",
				path, nr);
			ret = -AIRY_EINVAL;
			break;
		}

		strip(s);
		s = trim(s);
		if (!*s)
			continue;
		eq = strchr(s, '=');
		if (!eq) {
			set_err(err, len, "%s:%u: 缺少 '='", path, nr);
			ret = -AIRY_EINVAL;
			break;
		}
		*eq = '\0';
		k = trim(s);
		v = trim(eq + 1);
		rc = take_key(cfg, k, v);
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

	if (!ret && !cfg->max_bytes && !cfg->max_age_s) {
		set_err(err, len, "%s: 轮转阈值不得同时为 0", path);
		ret = -AIRY_EINVAL;
	}
	return ret;
}
