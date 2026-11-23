// SPDX-License-Identifier: AGPL-3.0-or-later OR Apache-2.0
/*
 * Copyright (c) 2025-2026 SPHARX Ltd. All Rights Reserved.
 *
 * logd.h —— logger_d 的进程内契约
 *
 * 把「配置校验 / 环消费 / 落盘轮转」拆成三个可单测的单元：logd_cfg
 * 只解析并校验 INI，logd_ring 只负责 mmap 读取与私有游标推进，
 * logd_file 只负责格式化、追加、按量/按时轮转与保留数裁剪。
 * logger_d 本体只做生命周期编排，不重复三者的判断逻辑。
 *
 * SSoT: docs-linux/20-modules/12-logger-daemon-module.md
 */
#ifndef _LOGD_H
#define _LOGD_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include <linux/airymax/log_ring.h>
#include <linux/airymax/log_types.h>

#define LOGD_DEV_MAX	64
#define LOGD_PATH_MAX	256
#define LOGD_LINE_MAX	640

struct logd_cfg {
	char		dev[LOGD_DEV_MAX];
	char		path[LOGD_PATH_MAX];
	uint32_t	level;
	uint32_t	flush_ms;
	uint64_t	max_bytes;
	uint32_t	max_age_s;
	uint32_t	keep;
};

struct logd_ring {
	int		dev_fd;
	int		efd;
	void		*map;
	size_t		map_len;
	uint32_t	capacity;
	uint32_t	hdr_size;
	uint32_t	rec_size;
	uint64_t	cursor;
	uint64_t	lost;
};

struct logd_file {
	int		fd;
	char		path[LOGD_PATH_MAX];
	uint64_t	bytes;
	uint64_t	opened_ms;
	uint64_t	last_sync;
	uint64_t	max_bytes;
	uint32_t	max_age_s;
	uint32_t	flush_ms;
	uint32_t	keep;
};

typedef int (*logd_emit)(void *ctx, const struct airy_log_record *rec);

int logd_cfg_def(struct logd_cfg *cfg);
int logd_cfg_load(const char *path, struct logd_cfg *cfg, char *err,
		  size_t len);

int logd_ring_open(struct logd_ring *ring, const char *dev);
void logd_ring_close(struct logd_ring *ring);
int logd_ring_drain(struct logd_ring *ring, logd_emit emit, void *ctx,
		    unsigned int *nr);

int logd_file_open(struct logd_file *file, const struct logd_cfg *cfg);
void logd_file_close(struct logd_file *file);
int logd_file_put(struct logd_file *file, const char *line, size_t len);
int logd_file_tick(struct logd_file *file);
int logd_fmt(char *buf, size_t cap, const struct airy_log_record *rec);

#endif /* _LOGD_H */
