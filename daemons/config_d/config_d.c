// SPDX-License-Identifier: AGPL-3.0-or-later OR Apache-2.0
/*
 * Copyright (c) 2025-2026 SPHARX Ltd. All Rights Reserved.
 *
 * config_d.c —— A-UCS 统一配置守护进程
 *
 * 守护进程的职责是让「配置已生效」可被验证：启动时装载 /etc/agentrt，
 * 校验版本与取值，把生效值发布为进程内快照并落到内核 sysctl；运行期
 * 监听配置目录，改动后重载，非法配置保留旧值而不是半套生效。
 *
 * 主配置源不可用时进入降级生存：以内置默认值只读运行，持续告警，
 * 并在配置恢复后自动切回。降级不是失败退出——配置坏了仍然要能开机。
 *
 * SSoT: docs-linux/20-modules/11-unified-config.md §4、§8.2
 *       docs-linux/100-operations/02-configuration.md §4
 */
#define _GNU_SOURCE

#include <errno.h>
#include <signal.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <linux/airymax/error.h>

#include <airy/daemon.h>

#include "cfg.h"

#define CONFIG_D_VERSION "0.1.0"
#define CFG_TICK_MS	1000
#define CFG_BEAT_MS	5000
#define CFG_RETRY_TICK	5	/* 监听缺失时的重试间隔，单位 tick */

struct cfg_ctx {
	const char *root;
	int ifd;
	int retry;
	bool pending;
	bool degraded;
};

static void cli_help(void)
{
	printf("用法: config_d [选项]\n"
	       "  --root DIR   配置根目录（默认 " AIRY_CFG_ROOT "）\n"
	       "  --check      校验配置后退出\n"
	       "  --dump       打印生效配置后退出\n"
	       "  --defaults   打印内置默认值后退出\n"
	       "  --version    打印版本后退出\n"
	       "  --help       显示本帮助\n");
}

static int cli_fail(int rc, const char *err)
{
	fprintf(stderr, "config_d: %s\n", err);
	switch (-rc) {
	case AIRY_ECFGVERSION:
		return AIRY_ECFGVERSION;
	case AIRY_ECFGSCHEMA:
		return AIRY_ECFGSCHEMA;
	case AIRY_ECFGIO:
		return AIRY_ECFGIO;
	default:
		return 1;
	}
}

static int cli_load(const char *root, struct airy_cfg *cfg, bool dump)
{
	char err[256] = "";
	int rc;

	rc = cfg_load(root, cfg, err, sizeof(err));
	if (rc)
		return cli_fail(rc, err);
	if (dump)
		cfg_dump(cfg);
	else
		printf("config_d: 配置校验通过（version=%d）\n", cfg->version);
	return 0;
}

/* 热重载拒绝不可热改项：cap.max_agents 是 agent_caps[] 静态容量 */
static void keep_frozen(const struct airy_cfg *old, struct airy_cfg *new)
{
	if (old->cap_max == new->cap_max)
		return;
	cfg_log("warn", "cap.max_agents 不可热重载，保留 %d（忽略 %d）",
		old->cap_max, new->cap_max);
	new->cap_max = old->cap_max;
}

static int reload(struct cfg_ctx *ctx, bool startup)
{
	struct airy_cfg next;
	const struct cfg_snap *snap;
	char err[256] = "";
	char aerr[256] = "";
	int rc;

	rc = cfg_load(ctx->root, &next, err, sizeof(err));
	if (!rc) {
		snap = cfg_hold();
		if (snap)
			keep_frozen(cfg_view(snap), &next);
		cfg_put(snap);

		/* 配置与内核几何不一致时整次拒绝，不发布不可满足的值 */
		rc = cfg_apply(&next, aerr, sizeof(aerr));
		if (rc == -AIRY_ECFGSCHEMA)
			snprintf(err, sizeof(err), "%s", aerr);
		else if (rc)
			cfg_log("warn", "内核参数落值不完整: %s", aerr);
	}

	if (rc) {
		if (startup) {
			cfg_default(&next);
			cfg_publish(&next);
			ctx->degraded = true;
			cfg_log("err", "配置不可用，降级为只读默认值: %s", err);
			return 0;
		}
		cfg_log("err", "配置重载被拒绝，保留当前值: %s", err);
		return rc;
	}

	cfg_publish(&next);
	ctx->degraded = false;

	cfg_log("info", "配置已生效 version=%d min_level=%d ring=%d "
		"cap.max_agents=%d", next.version, next.log_level,
		next.log_ring, next.cap_max);
	return 0;
}

/* 建立配置目录监听；配置根尚不存在时返回负值，由 tick 重试 */
static int on_event(struct airy_dmn *dmn, int fd, unsigned int events,
		    void *data);

static int arm_watch(struct cfg_ctx *ctx, struct airy_dmn *dmn, bool quiet)
{
	struct airy_dmn_src src;
	char err[256] = "";
	int fd;
	int rc;

	fd = cfg_watch(ctx->root, err, sizeof(err));
	if (fd < 0) {
		if (!quiet)
			cfg_log("warn", "配置变更监听不可用: %s", err);
		return fd;
	}

	src.fd = fd;
	src.events = AIRY_DMN_IN;
	src.recv = on_event;
	src.data = ctx;
	rc = airy_dmn_add(dmn, &src);
	if (rc) {
		close(fd);
		if (!quiet)
			cfg_log("warn", "事件源注册失败: %d", rc);
		return rc;
	}

	ctx->ifd = fd;
	return 0;
}

static int on_tick(struct airy_dmn *dmn, void *data)
{
	struct cfg_ctx *ctx = data;

	/* 配置根在启动时不存在的场景：恢复后自动接管并立即重载 */
	if (ctx->ifd < 0 && ++ctx->retry >= CFG_RETRY_TICK) {
		ctx->retry = 0;
		if (!arm_watch(ctx, dmn, true))
			ctx->pending = true;
	}

	if (ctx->pending) {
		ctx->pending = false;
		reload(ctx, false);
	}

	airy_dmn_notify("WATCHDOG=1");
	return 0;
}

static int on_reload(struct airy_dmn *dmn, void *data)
{
	(void)dmn;
	return reload(data, false);
}

static int on_event(struct airy_dmn *dmn, int fd, unsigned int events,
		    void *data)
{
	struct cfg_ctx *ctx = data;

	(void)dmn;
	if (events & (AIRY_DMN_ERR | AIRY_DMN_HUP))
		return -AIRY_EIO;
	if (cfg_drain(fd) > 0) {
		cfg_drop(fd, ctx->root);
		ctx->pending = true;
	}
	return 0;
}

static int on_start(struct airy_dmn *dmn, void *data)
{
	struct cfg_ctx *ctx = data;
	reload(ctx, true);
	arm_watch(ctx, dmn, false);

	airy_dmn_notify(ctx->degraded ? "READY=1\nSTATUS=降级只读" :
					"READY=1\nSTATUS=配置已生效");
	return 0;
}

static int on_stop(struct airy_dmn *dmn, void *data)
{
	struct cfg_ctx *ctx = data;

	(void)dmn;
	if (ctx->ifd >= 0)
		close(ctx->ifd);
	ctx->ifd = -1;
	return 0;
}

static int run_daemon(const char *root)
{
	struct cfg_ctx ctx = {
		.root = root,
		.ifd = -1,
		.retry = 0,
		.pending = false,
		.degraded = false,
	};
	struct airy_dmn_cfg cfg = {
		.name = "config_d",
		.config = NULL,
		.tick_ms = CFG_TICK_MS,
		.beat_ms = CFG_BEAT_MS,
		.miss_max = 3,
		.flags = AIRY_DMN_F_SIG,
	};
	struct airy_dmn_ops ops = {
		.start = on_start,
		.tick = on_tick,
		.reload = on_reload,
		.stop = on_stop,
		.data = &ctx,
	};
	struct airy_dmn *dmn;
	int rc;

	dmn = airy_dmn_new(&cfg);
	if (!dmn) {
		cfg_log("err", "初始化失败: %s", strerror(errno));
		return 1;
	}

	rc = airy_dmn_reg(dmn, &ops);
	if (!rc)
		rc = airy_dmn_run(dmn);
	airy_dmn_free(dmn);

	cfg_log("info", "退出 rc=%d", rc);
	return rc ? 1 : 0;
}

int main(int argc, char **argv)
{
	const char *root = AIRY_CFG_ROOT;
	bool check = false;
	bool dump = false;
	struct airy_cfg cfg;
	int i;

	for (i = 1; i < argc; i++) {
		if (strcmp(argv[i], "--root") == 0 && i + 1 < argc) {
			root = argv[++i];
		} else if (strcmp(argv[i], "--check") == 0) {
			check = true;
		} else if (strcmp(argv[i], "--dump") == 0) {
			dump = true;
		} else if (strcmp(argv[i], "--defaults") == 0) {
			cfg_default(&cfg);
			cfg_dump(&cfg);
			return 0;
		} else if (strcmp(argv[i], "--version") == 0) {
			printf("config_d %s\n", CONFIG_D_VERSION);
			return 0;
		} else if (strcmp(argv[i], "--help") == 0) {
			cli_help();
			return 0;
		} else {
			cli_help();
			return AIRY_EINVAL;
		}
	}

	if (check || dump)
		return cli_load(root, &cfg, dump);

	return run_daemon(root);
}
