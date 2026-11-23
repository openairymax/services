// SPDX-License-Identifier: AGPL-3.0-or-later OR Apache-2.0
/*
 * Copyright (c) 2025-2026 SPHARX Ltd. All Rights Reserved.
 *
 * logger_d.c —— 内核日志环的落盘守护
 *
 * 通过 /dev/airy_log 的 mmap 区消费记录，按配置过滤级别后写入本地
 * 日志文件，并按大小或时间轮转。事件源是环注册的 eventfd，tick 只
 * 负责超时轮转、周期性 fdatasync 与看门狗上报，因此空闲时不做无谓
 * 唤醒。SIGHUP 重新校验整份配置，非法则整次拒绝并保留旧配置。
 */
#define _GNU_SOURCE

#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sysexits.h>
#include <unistd.h>

#include <linux/airymax/error.h>

#include <airy/daemon.h>

#include "logd.h"

#define LOGD_CONF	"/etc/agentrt/logger.conf"

struct logd_ctx {
	struct logd_cfg		cfg;
	char			cfg_path[LOGD_PATH_MAX];
	struct logd_ring	ring;
	struct logd_file	file;
	uint64_t		lines;
	uint64_t		sink_lost;
};

static int put_rec(void *ctx, const struct airy_log_record *rec)
{
	struct logd_ctx *c = ctx;
	char line[LOGD_LINE_MAX];
	int ret;

	c->lines++;
	if (rec->level < c->cfg.level)
		return 0;
	ret = logd_fmt(line, sizeof(line), rec);
	if (ret < 0) {
		c->sink_lost++;
		return 0;
	}
	ret = logd_file_put(&c->file, line, (size_t)ret);
	if (ret) {
		if (!c->sink_lost)
			fprintf(stderr,
				"logger_d: 落盘失败，后续记录计入丢弃\n");
		c->sink_lost++;
	}
	return 0;
}

static int on_input(struct airy_dmn *dmn, int fd, unsigned int events,
		    void *data)
{
	(void)dmn;
	(void)fd;
	if (!(events & AIRY_DMN_IN))
		return 0;
	logd_ring_drain(&((struct logd_ctx *)data)->ring, put_rec, data, NULL);
	return 0;
}

static int on_tick(struct airy_dmn *dmn, void *data)
{
	struct logd_ctx *c = data;
	char msg[192];

	logd_ring_drain(&c->ring, put_rec, c, NULL);
	logd_file_tick(&c->file);
	airy_dmn_beat(dmn);
	snprintf(msg, sizeof(msg),
		 "WATCHDOG=1\nSTATUS=lines=%llu ring_lost=%llu sink_lost=%llu\n",
		 (unsigned long long)c->lines,
		 (unsigned long long)c->ring.lost,
		 (unsigned long long)c->sink_lost);
	airy_dmn_notify(msg);
	return 0;
}

static int on_start(struct airy_dmn *dmn, void *data)
{
	struct logd_ctx *c = data;

	(void)dmn;
	logd_ring_drain(&c->ring, put_rec, c, NULL);
	airy_dmn_notify("READY=1\nSTATUS=logging\n");
	return 0;
}

static void take_knobs(struct logd_file *file, const struct logd_cfg *cfg)
{
	file->max_bytes = cfg->max_bytes;
	file->max_age_s = cfg->max_age_s;
	file->flush_ms = cfg->flush_ms;
	file->keep = cfg->keep;
}

static int on_reload(struct airy_dmn *dmn, void *data)
{
	struct logd_ctx *c = data;
	struct airy_dmn_src src;
	struct logd_ring ring;
	struct logd_file file;
	struct logd_cfg cfg;
	bool dev_new, path_new;
	char err[192];
	int ret;

	ret = logd_cfg_load(c->cfg_path, &cfg, err, sizeof(err));
	if (ret) {
		fprintf(stderr, "logger_d: 配置未生效: %s\n", err);
		return 0;
	}
	dev_new = strcmp(cfg.dev, c->cfg.dev) != 0;
	path_new = strcmp(cfg.path, c->cfg.path) != 0;

	if (dev_new) {
		ret = logd_ring_open(&ring, cfg.dev);
		if (ret)
			goto reject;
		memset(&src, 0, sizeof(src));
		src.fd = ring.efd;
		src.events = AIRY_DMN_IN;
		src.recv = on_input;
		src.data = c;
		ret = airy_dmn_add(dmn, &src);
		if (ret) {
			logd_ring_close(&ring);
			goto reject;
		}
	}
	if (path_new) {
		ret = logd_file_open(&file, &cfg);
		if (ret) {
			if (dev_new) {
				airy_dmn_del(dmn, ring.efd);
				logd_ring_close(&ring);
			}
			goto reject;
		}
	}

	if (dev_new) {
		airy_dmn_del(dmn, c->ring.efd);
		logd_ring_close(&c->ring);
		c->ring = ring;
	}
	if (path_new) {
		logd_file_close(&c->file);
		c->file = file;
	} else {
		take_knobs(&c->file, &cfg);
	}
	c->cfg = cfg;
	airy_dmn_notify("STATUS=reloaded\n");
	return 0;

reject:
	fprintf(stderr, "logger_d: 新配置不可用，保留旧配置\n");
	return 0;
}

static int on_stop(struct airy_dmn *dmn, void *data)
{
	struct logd_ctx *c = data;

	(void)dmn;
	logd_ring_drain(&c->ring, put_rec, c, NULL);
	airy_dmn_notify("STOPPING=1\n");
	logd_file_close(&c->file);
	logd_ring_close(&c->ring);
	return 0;
}

static int usage(const char *argv0)
{
	printf("用法: %s [--config=路径]\n", argv0);
	return 0;
}

int main(int argc, char **argv)
{
	struct airy_dmn_ops ops;
	struct airy_dmn_src src;
	struct airy_dmn_cfg dcfg;
	struct airy_dmn *dmn;
	struct logd_ctx ctx;
	const char *path = LOGD_CONF;
	char err[192];
	int ret;
	int i;

	for (i = 1; i < argc; i++) {
		if (strncmp(argv[i], "--config=", 9) == 0) {
			path = argv[i] + 9;
		} else if (strcmp(argv[i], "--config") == 0 && i + 1 < argc) {
			path = argv[++i];
		} else if (strcmp(argv[i], "--help") == 0) {
			return usage(argv[0]);
		} else {
			fprintf(stderr, "logger_d: 未知参数 %s\n", argv[i]);
			return EX_USAGE;
		}
	}
	if (strlen(path) >= sizeof(ctx.cfg_path)) {
		fprintf(stderr, "logger_d: 配置路径过长\n");
		return EX_CONFIG;
	}

	memset(&ctx, 0, sizeof(ctx));
	strcpy(ctx.cfg_path, path);
	ret = logd_cfg_load(path, &ctx.cfg, err, sizeof(err));
	if (ret) {
		fprintf(stderr, "logger_d: %s\n", err);
		return EX_CONFIG;
	}
	ret = logd_ring_open(&ctx.ring, ctx.cfg.dev);
	if (ret) {
		fprintf(stderr, "logger_d: 无法接入 %s，请确认日志环模块已加载\n",
			ctx.cfg.dev);
		return EX_OSERR;
	}
	ret = logd_file_open(&ctx.file, &ctx.cfg);
	if (ret) {
		fprintf(stderr, "logger_d: 无法打开 %s\n", ctx.cfg.path);
		logd_ring_close(&ctx.ring);
		return EX_OSERR;
	}

	memset(&dcfg, 0, sizeof(dcfg));
	dcfg.name = "logger_d";
	dcfg.config = path;
	dcfg.tick_ms = 1000;
	dcfg.flags = AIRY_DMN_F_SIG;
	dmn = airy_dmn_new(&dcfg);
	if (!dmn) {
		logd_file_close(&ctx.file);
		logd_ring_close(&ctx.ring);
		return EX_OSERR;
	}

	memset(&ops, 0, sizeof(ops));
	ops.start = on_start;
	ops.tick = on_tick;
	ops.reload = on_reload;
	ops.stop = on_stop;
	ops.data = &ctx;
	ret = airy_dmn_reg(dmn, &ops);
	if (ret)
		goto out;

	memset(&src, 0, sizeof(src));
	src.fd = ctx.ring.efd;
	src.events = AIRY_DMN_IN;
	src.recv = on_input;
	src.data = &ctx;
	ret = airy_dmn_add(dmn, &src);
	if (ret)
		goto out;

	ret = airy_dmn_run(dmn);
	if (ret)
		fprintf(stderr, "logger_d: 主循环异常 %d\n", ret);

out:
	airy_dmn_free(dmn);
	logd_file_close(&ctx.file);
	logd_ring_close(&ctx.ring);
	return ret ? EX_SOFTWARE : 0;
}
