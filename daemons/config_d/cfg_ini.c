// SPDX-License-Identifier: AGPL-3.0-or-later OR Apache-2.0
/*
 * Copyright (c) 2025-2026 SPHARX Ltd. All Rights Reserved.
 *
 * cfg_ini.c —— /etc/agentrt 的 INI 装载与覆盖合成
 *
 * 装载顺序固定为 agentrt.conf → config.conf → conf.d/NN-*.conf，后者
 * 覆盖前者。conf.d 只接受两位数字前缀且以 .conf 结尾的文件，按文件
 * 名字典序加载；无前缀文件的归属无法判定，直接忽略而不是猜测。
 *
 * 严格模式：未知分组、未知键、非整数、超出行长的行都判 schema 非法。
 * 配置项拼错若被静默忽略，运行期就会拿默认值跑，比启动失败更难查。
 *
 * SSoT: docs-linux/100-operations/02-configuration.md §4.2、§4.3
 */
#define _POSIX_C_SOURCE 200809L

#include <ctype.h>
#include <dirent.h>
#include <errno.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include <linux/airymax/error.h>

#include "cfg_int.h"

#define CFG_DROP_MAX 64

static void put_err(char *err, size_t len, const char *fmt, ...)
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

static const struct cfg_key *find_key(const char *sec, const char *key)
{
	size_t i;

	for (i = 0; i < g_cfg_keynr; i++) {
		const struct cfg_key *k = &g_cfg_keys[i];

		if (strcmp(k->sec, sec) == 0 && strcmp(k->key, key) == 0)
			return k;
	}

	return NULL;
}

static int put_val(struct airy_cfg *cfg, const struct cfg_key *k, char *val,
		   const char *path, unsigned int nr, char *err, size_t len)
{
	char *end = NULL;
	long v;

	errno = 0;
	v = strtol(val, &end, 10);
	if (errno || !end || *end || end == val) {
		put_err(err, len, "%s:%u: %s.%s 不是整数: %s", path, nr, k->sec,
			k->key, val);
		return -AIRY_ECFGSCHEMA;
	}
	if (!k->gate && !cfg_key_ok(k, (int)v)) {
		if (k->set)
			put_err(err, len, "%s:%u: %s.%s=%ld 不在允许取值内",
				path, nr, k->sec, k->key, v);
		else
			put_err(err, len, "%s:%u: %s.%s=%ld 越界 [%d,%d]",
				path, nr, k->sec, k->key, v, k->min, k->max);
		return -AIRY_ECFGSCHEMA;
	}
	*(int *)((char *)cfg + k->off) = (int)v;
	return AIRY_EOK;
}

static int do_line(char *raw, struct airy_cfg *cfg, char *sec, size_t seclen,
		   const char *path, unsigned int nr, char *err, size_t len)
{
	char *line;
	char *eq;
	char *key;
	char *val;
	size_t n;
	const struct cfg_key *k;

	strip(raw);
	line = trim(raw);
	if (!*line)
		return AIRY_EOK;

	if (*line == '[') {
		n = strlen(line);
		if (n < 3 || line[n - 1] != ']') {
			put_err(err, len, "%s:%u: 分组头不完整", path, nr);
			return -AIRY_ECFGSCHEMA;
		}
		line[n - 1] = '\0';
		key = trim(line + 1);
		if (!*key || strlen(key) >= seclen) {
			put_err(err, len, "%s:%u: 分组名非法", path, nr);
			return -AIRY_ECFGSCHEMA;
		}
		strcpy(sec, key);
		return AIRY_EOK;
	}

	eq = strchr(line, '=');
	if (!eq) {
		put_err(err, len, "%s:%u: 缺少 '='", path, nr);
		return -AIRY_ECFGSCHEMA;
	}
	*eq = '\0';
	key = trim(line);
	val = trim(eq + 1);
	if (!*key || !*val) {
		put_err(err, len, "%s:%u: 空键或空值", path, nr);
		return -AIRY_ECFGSCHEMA;
	}
	k = find_key(sec, key);
	if (!k) {
		put_err(err, len, "%s:%u: 未知配置项 %s.%s", path, nr, sec, key);
		return -AIRY_ECFGSCHEMA;
	}

	return put_val(cfg, k, val, path, nr, err, len);
}

int cfg_ini_read(const char *path, struct airy_cfg *cfg, char *err, size_t len)
{
	char sec[32] = "main";
	char buf[AIRY_CFG_LINEMAX];
	unsigned int nr = 0;
	FILE *fp;
	int rc = AIRY_EOK;

	fp = fopen(path, "r");
	if (!fp) {
		put_err(err, len, "%s: %s", path, strerror(errno));
		return -AIRY_ECFGIO;
	}

	while (fgets(buf, sizeof(buf), fp)) {
		nr++;
		if (!strchr(buf, '\n') && !feof(fp)) {
			put_err(err, len, "%s:%u: 行超过 %d 字节", path, nr,
				AIRY_CFG_LINEMAX - 1);
			rc = -AIRY_ECFGSCHEMA;
			break;
		}
		rc = do_line(buf, cfg, sec, sizeof(sec), path, nr, err, len);
		if (rc)
			break;
	}

	if (!rc && ferror(fp)) {
		put_err(err, len, "%s: 读取失败", path);
		rc = -AIRY_ECFGIO;
	}
	fclose(fp);
	return rc;
}

static int join(char *out, size_t len, const char *root, const char *name)
{
	int n = snprintf(out, len, "%s/%s", root, name);

	if (n < 0 || (size_t)n >= len)
		return -AIRY_ECFGIO;
	return AIRY_EOK;
}

int cfg_ini_self(const char *root, struct airy_cfg *cfg, char *err, size_t len)
{
	char path[AIRY_CFG_PATHMAX];
	struct stat st;
	int rc;

	rc = join(path, sizeof(path), root, AIRY_CFG_SELF);
	if (rc)
		return rc;
	if (stat(path, &st) < 0) {
		if (errno == ENOENT)
			return AIRY_EOK;
		put_err(err, len, "%s: %s", path, strerror(errno));
		return -AIRY_ECFGIO;
	}

	return cfg_ini_read(path, cfg, err, len);
}

static bool drop_name(const char *name)
{
	size_t n = strlen(name);

	if (n < 6 || !isdigit((unsigned char)name[0]) ||
	    !isdigit((unsigned char)name[1]) || name[2] != '-')
		return false;
	return strcmp(name + n - 5, ".conf") == 0;
}

static int drop_cmp(const void *a, const void *b)
{
	return strcmp(*(const char *const *)a, *(const char *const *)b);
}

int cfg_ini_drop(const char *root, struct airy_cfg *cfg, char *err, size_t len)
{
	char dir[AIRY_CFG_PATHMAX];
	char path[AIRY_CFG_PATHMAX];
	char *names[CFG_DROP_MAX];
	size_t num = 0;
	size_t i;
	DIR *dp;
	struct dirent *de;
	int rc = AIRY_EOK;

	rc = join(dir, sizeof(dir), root, AIRY_CFG_DROPIN);
	if (rc)
		return rc;
	dp = opendir(dir);
	if (!dp) {
		if (errno == ENOENT)
			return AIRY_EOK;
		put_err(err, len, "%s: %s", dir, strerror(errno));
		return -AIRY_ECFGIO;
	}

	while ((de = readdir(dp)) != NULL) {
		if (!drop_name(de->d_name))
			continue;
		if (num >= CFG_DROP_MAX) {
			put_err(err, len, "%s: 覆盖文件超过 %d 个", dir,
				CFG_DROP_MAX);
			rc = -AIRY_ECFGSCHEMA;
			break;
		}
		names[num] = strdup(de->d_name);
		if (!names[num]) {
			rc = -AIRY_ENOMEM;
			break;
		}
		num++;
	}
	closedir(dp);
	if (rc)
		goto out;

	qsort(names, num, sizeof(names[0]), drop_cmp);
	for (i = 0; i < num; i++) {
		rc = join(path, sizeof(path), dir, names[i]);
		if (!rc)
			rc = cfg_ini_read(path, cfg, err, len);
		if (rc)
			break;
	}

out:
	for (i = 0; i < num; i++)
		free(names[i]);
	return rc;
}

int cfg_load(const char *root, struct airy_cfg *cfg, char *err, size_t len)
{
	char path[AIRY_CFG_PATHMAX];
	struct stat st;
	int rc;

	if (!root || !cfg)
		return -AIRY_EINVAL;

	cfg_default(cfg);
	rc = join(path, sizeof(path), root, AIRY_CFG_MAIN);
	if (rc)
		return rc;
	if (stat(path, &st) < 0) {
		put_err(err, len, "%s: %s", path, strerror(errno));
		return -AIRY_ECFGIO;
	}

	rc = cfg_ini_read(path, cfg, err, len);
	if (!rc)
		rc = cfg_ini_self(root, cfg, err, len);
	if (!rc)
		rc = cfg_ini_drop(root, cfg, err, len);
	if (rc)
		return rc;

	if (cfg->version != AIRY_CFG_VERSION) {
		put_err(err, len, "%s: 配置版本 %d，本守护只接受 %d",
			path, cfg->version, AIRY_CFG_VERSION);
		return -AIRY_ECFGVERSION;
	}

	rc = cfg_valid(cfg);
	if (rc)
		put_err(err, len, "%s: 配置取值越界", path);

	return rc;
}
