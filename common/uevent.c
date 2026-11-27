// SPDX-License-Identifier: AGPL-3.0-or-later OR Apache-2.0
/*
 * Copyright (c) 2025-2026 SPHARX Ltd. All Rights Reserved.
 *
 * uevent.c — user_events 生产端封装
 *
 * 注册走 DIAG_IOCSREG：内核校验事件描述串，返回写索引，并把使能位
 * 钉在 handle 的 enabled 字所在的页上。每次上报先读使能位，未打开就
 * 直接返回，避免为空消费者付费；写入布局是 4 字节写索引加载荷，与
 * 内核 user_events_write() 的最小长度断言一致。
 *
 * 关闭时用 DIAG_IOCSUNREG 撤销使能位再关 fd；即便撤销失败，fd 关闭
 * 也会释放事件引用，不留跨进程残留。
 */
#define _GNU_SOURCE

#include <errno.h>
#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/uio.h>
#include <unistd.h>

#include <linux/user_events.h>

#include <airy/uevent.h>

#define UEV_PATH	"/sys/kernel/tracing/user_events_data"
#define UEV_PATH_ALT	"/sys/kernel/debug/tracing/user_events_data"
#define UEV_DESC_MAX	512

static int uev_open_file(void)
{
	int fd;

	fd = open(UEV_PATH, O_RDWR | O_CLOEXEC);
	if (fd >= 0)
		return fd;

	return open(UEV_PATH_ALT, O_RDWR | O_CLOEXEC);
}

int airy_uev_open(struct airy_uev *ev, const char *name, const char *args)
{
	struct user_reg reg;
	char desc[UEV_DESC_MAX];
	int fd, n;

	if (!ev || !name || !args)
		return -EINVAL;

	memset(ev, 0, sizeof(*ev));
	ev->fd = -1;

	n = snprintf(desc, sizeof(desc), "%s %s", name, args);
	if (n < 0 || (size_t)n >= sizeof(desc))
		return -E2BIG;

	fd = uev_open_file();
	if (fd < 0)
		return -errno;

	memset(&reg, 0, sizeof(reg));
	reg.size = sizeof(reg);
	reg.enable_bit = 0;
	reg.enable_size = sizeof(ev->enabled);
	reg.enable_addr = (__u64)(uintptr_t)&ev->enabled;
	reg.name_args = (__u64)(uintptr_t)desc;

	if (ioctl(fd, DIAG_IOCSREG, &reg)) {
		n = -errno;
		close(fd);
		return n;
	}

	ev->fd = fd;
	ev->index = reg.write_index;

	return 0;
}

void airy_uev_close(struct airy_uev *ev)
{
	struct user_unreg reg;

	if (!ev || ev->fd < 0)
		return;

	memset(&reg, 0, sizeof(reg));
	reg.size = sizeof(reg);
	reg.disable_bit = 0;
	reg.disable_addr = (__u64)(uintptr_t)&ev->enabled;
	(void)ioctl(ev->fd, DIAG_IOCSUNREG, &reg);
	close(ev->fd);

	memset(ev, 0, sizeof(*ev));
	ev->fd = -1;
}

bool airy_uev_on(const struct airy_uev *ev)
{
	if (!ev || ev->fd < 0)
		return false;

	return (__atomic_load_n(&ev->enabled, __ATOMIC_RELAXED) & 1U) != 0;
}

int airy_uev_emit(struct airy_uev *ev, const void *rec, size_t len)
{
	struct iovec iov[2];
	ssize_t n;

	if (!ev || !rec || !len)
		return -EINVAL;
	if (!airy_uev_on(ev))
		return 0;

	iov[0].iov_base = &ev->index;
	iov[0].iov_len = sizeof(ev->index);
	iov[1].iov_base = (void *)rec;
	iov[1].iov_len = len;

	n = writev(ev->fd, iov, 2);
	if (n < 0)
		return -errno;
	if ((size_t)n != sizeof(ev->index) + len)
		return -EIO;

	return 0;
}
