/* SPDX-License-Identifier: AGPL-3.0-or-later OR Apache-2.0 */
/*
 * Copyright (c) 2025-2026 SPHARX Ltd. All Rights Reserved.
 *
 * dmn_notify.c — daemon 框架的 systemd 就绪与看门狗上报
 *
 * systemd 以 NOTIFY_SOCKET 指向的 Unix 数据报套接字接收状态串，
 * Type=notify 单元依赖 READY=1 结束启动阶段，WatchdogSec 依赖周期
 * 的 WATCHDOG=1 重置计时。本分片只把调用方给定的状态串投递到该
 * 地址：不链接 libsystemd，不解析内容，发送失败也不改变 daemon
 * 生命周期；未在 systemd 下运行时按无操作成功返回。
 *
 * IRON-9 v3: [IND] 独立实现层 — agent-linux services 子仓内部。
 *
 * SSoT: docs-linux/20-modules/02-services.md §3.7
 *       docs-linux/100-operations/07-systemd-integration.md §3.3
 */

#define _GNU_SOURCE

#include <errno.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>

#include "dmn_int.h"

/* 状态串上界：远大于 STATUS= 诊断行，避免调用方传入无界缓冲。 */
#define DMN_NOTIFY_MAX	4096

static int dmn_naddr(const char *path, struct sockaddr_un *addr,
		     socklen_t *len)
{
	size_t size = strlen(path);

	memset(addr, 0, sizeof(*addr));
	addr->sun_family = AF_UNIX;

	if (path[0] == '@') {
		if (size < 2 || size > sizeof(addr->sun_path))
			return -AIRY_EINVAL;
		memcpy(addr->sun_path + 1, path + 1, size - 1);
		*len = (socklen_t)(offsetof(struct sockaddr_un, sun_path) +
				   size);
		return 0;
	}

	if (size == 0 || size >= sizeof(addr->sun_path))
		return -AIRY_EINVAL;

	memcpy(addr->sun_path, path, size + 1);
	*len = (socklen_t)(offsetof(struct sockaddr_un, sun_path) +
			   size + 1);

	return 0;
}

static int dmn_ndest(struct sockaddr_un *addr, socklen_t *len)
{
	const char *path = getenv("NOTIFY_SOCKET");

	if (!path || !path[0])
		return 1;

	return dmn_naddr(path, addr, len);
}

static int dmn_nsend(int fd, const struct sockaddr_un *addr, socklen_t len,
		     const char *state, size_t size)
{
	ssize_t ret;

	do {
		ret = sendto(fd, state, size, MSG_NOSIGNAL,
			     (const struct sockaddr *)addr, len);
	} while (ret < 0 && errno == EINTR);

	if (ret < 0)
		return airy_dmn_err(errno);
	if ((size_t)ret != size)
		return -AIRY_EIO;

	return 0;
}

static int dmn_npost(const struct sockaddr_un *addr, socklen_t len,
		     const char *state, size_t size)
{
	int fd;
	int ret;

	fd = socket(AF_UNIX, SOCK_DGRAM | SOCK_CLOEXEC, 0);
	if (fd < 0)
		return airy_dmn_err(errno);

	ret = dmn_nsend(fd, addr, len, state, size);
	close(fd);

	return ret;
}

int airy_dmn_notify(const char *state)
{
	struct sockaddr_un addr;
	socklen_t len;
	size_t size;
	int ret;

	if (!state || !state[0])
		return -AIRY_EINVAL;

	size = strlen(state);
	if (size > DMN_NOTIFY_MAX)
		return -AIRY_EINVAL;

	ret = dmn_ndest(&addr, &len);
	if (ret)
		return ret > 0 ? 0 : ret;

	return dmn_npost(&addr, len, state, size);
}
