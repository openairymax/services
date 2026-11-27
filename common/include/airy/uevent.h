/* SPDX-License-Identifier: AGPL-3.0-or-later OR Apache-2.0 */
/*
 * Copyright (c) 2025-2026 SPHARX Ltd. All Rights Reserved.
 *
 * uevent.h — user_events 生产端封装
 *
 * 守护进程通过 tracefs 的 user_events_data 注册事件，内核把一个使能位
 * 写到 handle 里的 enabled 字上；只有该位被 trace 工具打开时才写载荷。
 * 事件名与格式串由 [SC] <linux/airymax/user_events.h> 给出，本模块不
 * 自带字面量。
 */
#ifndef AIRY_UEVENT_H
#define AIRY_UEVENT_H

#include <stdbool.h>
#include <stddef.h>

#include <linux/types.h>
#include <linux/airymax/user_events.h>

/*
 * 一个事件注册句柄。enabled 由内核写入，必须自然对齐且大小等于注册时
 * 声明的 enable_size；调用方用 airy_uev_open() 初始化，不要手工填写。
 */
struct airy_uev {
	int fd;
	__u32 index;
	volatile __u32 enabled;
};

int airy_uev_open(struct airy_uev *ev, const char *name, const char *args);
void airy_uev_close(struct airy_uev *ev);
bool airy_uev_on(const struct airy_uev *ev);
int airy_uev_emit(struct airy_uev *ev, const void *rec, size_t len);

#endif /* AIRY_UEVENT_H */
