// SPDX-License-Identifier: AGPL-3.0-or-later OR Apache-2.0
/*
 * Copyright (c) 2025-2026 SPHARX Ltd. All Rights Reserved.
 *
 * dmn_core.c — daemon 框架对象生命周期
 *
 * 负责上下文创建/释放、业务回调绑定与状态查询。所有入参在写入
 * 上下文前完成校验；name 与 config 按值复制，调用方无需保持字符
 * 串生命周期。框架自持的 epoll/signalfd 描述符在此统一关闭，调用
 * 方注册的外部描述符一律不关闭。
 *
 * IRON-9 v3: [IND] 独立实现层 — agent-linux services 子仓内部。
 *
 * SSoT: docs-linux/20-modules/02-services.md §3.7
 */

#define _GNU_SOURCE

#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include "dmn_int.h"

static int dmn_copy(char *dst, size_t size, const char *src)
{
	size_t len;

	if (!src) {
		dst[0] = '\0';
		return 0;
	}

	len = strlen(src);
	if (len >= size)
		return -AIRY_EINVAL;

	memcpy(dst, src, len + 1);
	return 0;
}

struct airy_dmn *airy_dmn_new(const struct airy_dmn_cfg *cfg)
{
	struct airy_dmn *dmn;

	if (!cfg || !cfg->name || !cfg->name[0]) {
		errno = EINVAL;
		return NULL;
	}

	dmn = calloc(1, sizeof(*dmn));
	if (!dmn) {
		errno = ENOMEM;
		return NULL;
	}

	if (dmn_copy(dmn->name, sizeof(dmn->name), cfg->name) ||
	    dmn_copy(dmn->config, sizeof(dmn->config), cfg->config)) {
		free(dmn);
		errno = EINVAL;
		return NULL;
	}

	dmn->tick_ms = cfg->tick_ms;
	dmn->beat_ms = cfg->beat_ms;
	dmn->miss_max = cfg->miss_max ? cfg->miss_max : AIRY_DMN_DFL_MISS;
	dmn->flags = cfg->flags;
	atomic_init(&dmn->state, AIRY_DMN_INIT);
	atomic_init(&dmn->last_beat, 0);
	atomic_init(&dmn->loop_tid, 0);
	atomic_init(&dmn->stop_req, false);
	dmn->epfd = -1;
	dmn->sigfd = -1;
	atomic_init(&dmn->wfd, -1);

	return dmn;
}

void airy_dmn_free(struct airy_dmn *dmn)
{
	int wfd;

	if (!dmn)
		return;

	wfd = atomic_load(&dmn->wfd);
	if (dmn->epfd >= 0)
		close(dmn->epfd);
	if (dmn->sigfd >= 0)
		close(dmn->sigfd);
	if (wfd >= 0)
		close(wfd);

	free(dmn);
}

int airy_dmn_reg(struct airy_dmn *dmn, const struct airy_dmn_ops *ops)
{
	if (!dmn || !ops)
		return -AIRY_EINVAL;
	if (dmn_state(dmn) != AIRY_DMN_INIT)
		return -AIRY_EBUSY;

	dmn->ops = *ops;
	dmn->bound = true;

	return 0;
}

enum airy_dmn_state airy_dmn_state(const struct airy_dmn *dmn)
{
	if (!dmn)
		return AIRY_DMN_INIT;

	return dmn_state(dmn);
}

const char *airy_dmn_name(const struct airy_dmn *dmn)
{
	if (!dmn)
		return "";

	return dmn->name;
}

uint64_t airy_dmn_now(void)
{
	struct timespec ts;

	if (clock_gettime(CLOCK_MONOTONIC, &ts))
		return 0;

	return (uint64_t)ts.tv_sec * 1000u + (uint64_t)ts.tv_nsec / 1000000u;
}
