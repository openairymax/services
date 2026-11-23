// SPDX-License-Identifier: AGPL-3.0-or-later OR Apache-2.0
/*
 * Copyright (c) 2025-2026 SPHARX Ltd. All Rights Reserved.
 *
 * logd_file.c —— 记录格式化与按量/按时轮转
 *
 * 一行一条记录，前缀为 UTC 的 RFC3339 纳秒时间戳、级别、来源与 pid，
 * 正文按可打印字符原样输出，其余字节转义为 \xNN，保证落盘文件始终
 * 是纯文本、可直接被 grep/awk 处理。轮转先移位再重开，保留窗口内的
 * 文件从不被截断；keep 之外的最旧文件先删除。
 */
#define _GNU_SOURCE

#include <errno.h>
#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

#include <linux/airymax/error.h>

#include "logd.h"

static uint64_t now_ms(void)
{
	struct timespec ts;

	clock_gettime(CLOCK_MONOTONIC, &ts);
	return (uint64_t)ts.tv_sec * 1000ULL + (uint64_t)ts.tv_nsec / 1000000ULL;
}

static const char *lvl_name(uint16_t level)
{
	static const char *const names[] = {
		"DEBUG", "INFO", "WARN", "ERROR", "FATAL",
	};

	return level < sizeof(names) / sizeof(names[0]) ? names[level] : "?";
}

static void fac_name(char *dst, size_t cap, uint16_t fac)
{
	if (fac == AIRY_LOG_FAC_KERN)
		snprintf(dst, cap, "kern");
	else
		snprintf(dst, cap, "fac-%04x", (unsigned int)fac);
}

static size_t escape(char *dst, size_t cap, const uint8_t *src, uint32_t len)
{
	size_t o = 0;
	uint32_t i;

	while (len && (src[len - 1] == '\n' || src[len - 1] == '\r'))
		len--;
	for (i = 0; i < len && o + 4 < cap; i++) {
		uint8_t c = src[i];

		if (c >= 0x20 && c < 0x7f) {
			dst[o++] = (char)c;
		} else {
			o += (size_t)snprintf(dst + o, cap - o, "\\x%02x", c);
		}
	}
	dst[o] = '\0';
	return o;
}

int logd_fmt(char *buf, size_t cap, const struct airy_log_record *rec)
{
	char msg[AIRY_LOG_RECORD_SIZE * 4 + 1];
	char fac[16];
	struct timespec ts;
	struct tm tm;
	uint32_t len;
	int n;

	if (!buf || !rec)
		return -AIRY_EINVAL;

	len = rec->payload_len;
	if (len > sizeof(rec->payload))
		len = sizeof(rec->payload);
	escape(msg, sizeof(msg), rec->payload, len);
	fac_name(fac, sizeof(fac), rec->facility);

	ts.tv_sec = (time_t)(rec->timestamp_ns / 1000000000ULL);
	ts.tv_nsec = (long)(rec->timestamp_ns % 1000000000ULL);
	if (!gmtime_r(&ts.tv_sec, &tm))
		return -AIRY_EINVAL;

	n = snprintf(buf, cap,
		     "%04d-%02d-%02dT%02d:%02d:%02d.%09ldZ %-5s %s[%u] %s\n",
		     tm.tm_year + 1900, tm.tm_mon + 1, tm.tm_mday,
		     tm.tm_hour, tm.tm_min, tm.tm_sec, ts.tv_nsec,
		     lvl_name(rec->level), fac, (unsigned int)rec->caller_id,
		     msg);
	if (n < 0 || (size_t)n >= cap)
		return -AIRY_EINVAL;
	return n;
}

static int mkdirs(const char *path)
{
	char tmp[LOGD_PATH_MAX];
	size_t n = strlen(path);
	size_t i;

	if (n == 0 || n >= sizeof(tmp))
		return -AIRY_EINVAL;
	memcpy(tmp, path, n + 1);
	for (i = 1; i < n; i++) {
		if (tmp[i] != '/')
			continue;
		tmp[i] = '\0';
		if (mkdir(tmp, 0750) && errno != EEXIST)
			return -AIRY_EACCES;
		tmp[i] = '/';
	}
	return 0;
}

static int sync_dir(const char *path)
{
	char tmp[LOGD_PATH_MAX];
	char *slash;
	int fd;

	if (strlen(path) >= sizeof(tmp))
		return -AIRY_EINVAL;
	strcpy(tmp, path);
	slash = strrchr(tmp, '/');
	if (!slash || slash == tmp)
		return 0;
	*slash = '\0';
	fd = open(tmp, O_RDONLY | O_DIRECTORY | O_CLOEXEC);
	if (fd < 0)
		return -AIRY_EIO;
	fsync(fd);
	close(fd);
	return 0;
}

static int open_sink(struct logd_file *file)
{
	struct stat st;
	int fd;

	fd = open(file->path, O_WRONLY | O_APPEND | O_CREAT | O_CLOEXEC, 0640);
	if (fd < 0 && errno == ENOENT) {
		int ret = mkdirs(file->path);

		if (ret)
			return ret;
		fd = open(file->path, O_WRONLY | O_APPEND | O_CREAT | O_CLOEXEC,
			  0640);
	}
	if (fd < 0)
		return errno == EACCES ? -AIRY_EACCES : -AIRY_EIO;

	file->fd = fd;
	file->bytes = 0;
	if (fstat(fd, &st) == 0 && S_ISREG(st.st_mode))
		file->bytes = (uint64_t)st.st_size;
	file->opened_ms = now_ms();
	file->last_sync = file->opened_ms;
	return 0;
}

int logd_file_open(struct logd_file *file, const struct logd_cfg *cfg)
{
	if (!file || !cfg)
		return -AIRY_EINVAL;
	memset(file, 0, sizeof(*file));
	file->fd = -1;
	if (strlen(cfg->path) >= sizeof(file->path))
		return -AIRY_EINVAL;
	strcpy(file->path, cfg->path);
	file->max_bytes = cfg->max_bytes;
	file->max_age_s = cfg->max_age_s;
	file->flush_ms = cfg->flush_ms;
	file->keep = cfg->keep;
	return open_sink(file);
}

static void mark(char *buf, size_t cap, const char *path, uint32_t idx)
{
	snprintf(buf, cap, "%s.%u", path, idx);
}

static int rotate(struct logd_file *file)
{
	char from[LOGD_PATH_MAX + 16];
	char to[LOGD_PATH_MAX + 16];
	uint32_t i;
	int ret;

	if (file->fd >= 0) {
		fdatasync(file->fd);
		close(file->fd);
		file->fd = -1;
	}

	if (file->bytes > 0) {
		mark(to, sizeof(to), file->path, file->keep);
		unlink(to);
		for (i = file->keep - 1; i >= 1; i--) {
			mark(from, sizeof(from), file->path, i);
			mark(to, sizeof(to), file->path, i + 1);
			if (rename(from, to) && errno != ENOENT)
				return -AIRY_EIO;
		}
		mark(to, sizeof(to), file->path, 1);
		if (rename(file->path, to) && errno != ENOENT)
			return -AIRY_EIO;
		ret = sync_dir(file->path);
		if (ret)
			return ret;
	}
	return open_sink(file);
}

static bool need_rot(const struct logd_file *file, size_t len)
{
	if (file->max_bytes && file->bytes + len > file->max_bytes)
		return true;
	return false;
}

int logd_file_put(struct logd_file *file, const char *line, size_t len)
{
	size_t off = 0;

	if (!file || !line)
		return -AIRY_EINVAL;
	if (file->fd < 0) {
		int ret = open_sink(file);

		if (ret)
			return ret;
	}
	if (need_rot(file, len)) {
		int ret = rotate(file);

		if (ret)
			return ret;
	}

	while (off < len) {
		ssize_t n = write(file->fd, line + off, len - off);

		if (n < 0) {
			if (errno == EINTR)
				continue;
			return errno == ENOSPC ? -AIRY_ENOSPC : -AIRY_EIO;
		}
		if (n == 0)
			return -AIRY_EIO;
		off += (size_t)n;
	}
	file->bytes += len;
	return 0;
}

int logd_file_tick(struct logd_file *file)
{
	uint64_t now;

	if (!file)
		return -AIRY_EINVAL;
	if (file->fd < 0)
		return 0;
	now = now_ms();
	if (file->max_age_s && file->bytes &&
	    now - file->opened_ms >= (uint64_t)file->max_age_s * 1000ULL)
		return rotate(file);
	if (file->flush_ms && now - file->last_sync >= file->flush_ms) {
		fdatasync(file->fd);
		file->last_sync = now;
	}
	return 0;
}

void logd_file_close(struct logd_file *file)
{
	if (!file)
		return;
	if (file->fd >= 0) {
		fdatasync(file->fd);
		close(file->fd);
		file->fd = -1;
	}
}
