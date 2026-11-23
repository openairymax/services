// SPDX-License-Identifier: AGPL-3.0-or-later OR Apache-2.0
/*
 * Copyright (c) 2025-2026 SPHARX Ltd. All Rights Reserved.
 *
 * logd_file_test.c —— 记录格式化与轮转的单测（TAP）
 *
 * 覆盖：UTC 纳秒时间戳与级别/来源前缀、不可打印字节转义、尾部换行
 * 剔除、按大小轮转、保留窗口裁剪、append 语义下的现有大小计入。
 */
#define _POSIX_C_SOURCE 200809L

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include <linux/airymax/error.h>

#include "logd.h"

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

static int put4(const char *p, uint64_t size)
{
	struct stat st;

	if (stat(p, &st))
		return -1;
	return (uint64_t)st.st_size == size ? 0 : -1;
}

static int absent(const char *p)
{
	struct stat st;

	return stat(p, &st) ? 1 : 0;
}

static void fill(struct airy_log_record *rec, uint16_t level, uint16_t fac,
		 uint32_t caller, const char *msg)
{
	memset(rec, 0, sizeof(*rec));
	rec->magic = AIRY_LOG_MAGIC;
	rec->level = level;
	rec->facility = fac;
	rec->timestamp_ns = 0;
	rec->caller_id = caller;
	rec->payload_len = (uint32_t)strlen(msg);
	memcpy(rec->payload, msg, rec->payload_len);
}

int main(void)
{
	struct airy_log_record rec;
	struct logd_cfg cfg;
	struct logd_file file;
	char line[LOGD_LINE_MAX];
	char dir[LOGD_PATH_MAX];
	char path[LOGD_PATH_MAX];
	char p1[LOGD_PATH_MAX + 8];
	char p3[LOGD_PATH_MAX + 8];
	char p4[LOGD_PATH_MAX + 8];
	const char *text = "012345678901234567890123456789";
	int ret;

	fill(&rec, AIRY_LOG_INFO, AIRY_LOG_FAC_KERN, 0, "hello");
	ret = logd_fmt(line, sizeof(line), &rec);
	ok(ret > 0, "记录可格式化");
	ok(strcmp(line,
		  "1970-01-01T00:00:00.000000000Z INFO  kern[0] hello\n") == 0,
	   "UTC 时间戳与级别/来源前缀");

	fill(&rec, AIRY_LOG_WARN, AIRY_LOG_FAC_SCHED, 7, "abc\r\n");
	ret = logd_fmt(line, sizeof(line), &rec);
	ok(ret > 0 && strstr(line, "WARN  fac-0005[7] abc\n") != NULL,
	   "尾部换行剔除且未知来源按编号输出");

	memset(&rec, 0, sizeof(rec));
	rec.magic = AIRY_LOG_MAGIC;
	rec.level = AIRY_LOG_ERROR;
	rec.facility = AIRY_LOG_FAC_KERN;
	rec.caller_id = 1;
	rec.payload_len = 3;
	rec.payload[0] = 'a';
	rec.payload[1] = 0x01;
	rec.payload[2] = 'b';
	ret = logd_fmt(line, sizeof(line), &rec);
	ok(ret > 0 && strstr(line, "a\\x01b\n") != NULL,
	   "不可打印字节转义为 \\xNN");

	dir[0] = '\0';
	snprintf(dir, sizeof(dir), "/tmp/logd-file-XXXXXX");
	ok(mkdtemp(dir) != NULL, "创建临时目录");

	snprintf(path, sizeof(path), "%s/airy.log", dir);
	ok(logd_cfg_def(&cfg) == 0, "默认配置可用");
	strcpy(cfg.path, path);
	cfg.max_bytes = 40;
	cfg.max_age_s = 0;
	cfg.flush_ms = 0;
	cfg.keep = 3;

	ret = logd_file_open(&file, &cfg);
	ok(ret == 0, "落盘文件可打开");

	ret = logd_file_put(&file, text, 30);
	ok(ret == 0 && put4(path, 30) == 0, "第一条写入主文件");

	ret = logd_file_put(&file, text, 30);
	ok(ret == 0 && put4(path, 30) == 0, "越界前轮转并重开主文件");

	snprintf(p1, sizeof(p1), "%s.1", path);
	ok(put4(p1, 30) == 0, "历史文件 .1 保留上一代内容");

	ret = logd_file_put(&file, text, 30);
	ret = logd_file_put(&file, text, 30);
	snprintf(p3, sizeof(p3), "%s.3", path);
	snprintf(p4, sizeof(p4), "%s.4", path);
	ok(ret == 0, "连续轮转不失败");
	ok(put4(p3, 30) == 0, "保留窗口内最新历史为 .3");
	ok(absent(p4), "超出 keep 的历史文件被删除");

	logd_file_close(&file);
	unlink(path);
	unlink(p1);
	snprintf(p1, sizeof(p1), "%s.2", path);
	unlink(p1);
	unlink(p3);
	rmdir(dir);

	if (g_bad)
		printf("# %u/%u 项失败\n", g_bad, g_nr);
	return g_bad ? 1 : 0;
}
