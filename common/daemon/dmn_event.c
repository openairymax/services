// SPDX-License-Identifier: AGPL-3.0-or-later OR Apache-2.0
/*
 * Copyright (c) 2025-2026 SPHARX Ltd. All Rights Reserved.
 *
 * dmn_event.c — daemon 框架 fd 事件源表与 epoll 注册
 *
 * 事件源以固定槽位存放，槽位下标即 epoll data.u32，回调期间下标
 * 保持稳定。epoll 实例在首次注册时惰性创建并随上下文释放；注册
 * 与注销只操作框架自身的 epoll，不关闭调用方描述符。外部提前关闭
 * 描述符导致的 ENOENT/EBADF 视为已注销，槽位仍然回收。
 *
 * 主循环运行期间允许在回调内注册、注销与修改关注掩码，事件表因此
 * 只由循环线程修改：以 loop_tid 与调用线程比对，其他线程在运行期
 * 变更返回 EBUSY，避免与分派并发改写槽位。
 *
 * IRON-9 v3: [IND] 独立实现层 — agent-linux services 子仓内部。
 *
 * SSoT: docs-linux/20-modules/02-services.md §3.7
 */

#define _GNU_SOURCE

#include <errno.h>
#include <string.h>
#include <sys/epoll.h>
#include <unistd.h>

#include "dmn_int.h"

#define DMN_EVT_ALL	(AIRY_DMN_IN | AIRY_DMN_OUT | \
			 AIRY_DMN_ERR | AIRY_DMN_HUP)

static const struct {
	int err;
	int airy;
} dmn_emap[] = {
	{ EEXIST,	AIRY_EEXIST	},
	{ ENOENT,	AIRY_ENOENT	},
	{ ENOMEM,	AIRY_ENOMEM	},
	{ ENOSPC,	AIRY_ENOMEM	},
	{ EMFILE,	AIRY_ENOMEM	},
	{ ENFILE,	AIRY_ENOMEM	},
	{ EPERM,	AIRY_EACCES	},
};

int airy_dmn_err(int err)
{
	size_t i;

	for (i = 0; i < AIRY_ARRAY_SIZE(dmn_emap); i++)
		if (dmn_emap[i].err == err)
			return -dmn_emap[i].airy;

	return -AIRY_EIO;
}

int airy_dmn_ep(struct airy_dmn *dmn)
{
	if (dmn->epfd >= 0)
		return 0;

	dmn->epfd = epoll_create1(EPOLL_CLOEXEC);
	if (dmn->epfd < 0)
		return airy_dmn_err(errno);

	return 0;
}

static int dmn_slot(const struct airy_dmn *dmn)
{
	unsigned int i;

	for (i = 0; i < AIRY_ARRAY_SIZE(dmn->evts); i++)
		if (!dmn->evts[i].used)
			return (int)i;

	return -AIRY_ENOMEM;
}

/* 事件表写操作只允许在初始化期或循环线程的运行期/停止期进行。 */
bool airy_dmn_evok(const struct airy_dmn *dmn)
{
	if (dmn->state == AIRY_DMN_INIT)
		return true;

	if (dmn->state != AIRY_DMN_RUNNING &&
	    dmn->state != AIRY_DMN_STOPPING)
		return false;

	return dmn->loop_tid == gettid();
}

bool airy_dmn_delok(const struct airy_dmn *dmn)
{
	return dmn->state == AIRY_DMN_STOPPED || airy_dmn_evok(dmn);
}

static int dmn_ctl(struct airy_dmn *dmn, int op, int fd, int slot,
		   uint32_t mask)
{
	struct epoll_event ev = {
		.events = mask,
		.data.u32 = (uint32_t)slot,
	};

	if (epoll_ctl(dmn->epfd, op, fd, &ev))
		return airy_dmn_err(errno);

	return 0;
}

int airy_dmn_find(const struct airy_dmn *dmn, int fd)
{
	unsigned int i;

	for (i = 0; i < AIRY_ARRAY_SIZE(dmn->evts); i++)
		if (dmn->evts[i].used && dmn->evts[i].fd == fd)
			return (int)i;

	return -AIRY_ENOENT;
}

uint32_t airy_dmn_mask(unsigned int events)
{
	uint32_t mask = 0;

	if (events & AIRY_DMN_IN)
		mask |= EPOLLIN;
	if (events & AIRY_DMN_OUT)
		mask |= EPOLLOUT;
	if (events & AIRY_DMN_ERR)
		mask |= EPOLLERR;
	if (events & AIRY_DMN_HUP)
		mask |= EPOLLRDHUP;

	return mask;
}

unsigned int airy_dmn_rev(uint32_t mask)
{
	unsigned int events = 0;

	if (mask & (EPOLLIN | EPOLLPRI))
		events |= AIRY_DMN_IN;
	if (mask & EPOLLOUT)
		events |= AIRY_DMN_OUT;
	if (mask & EPOLLERR)
		events |= AIRY_DMN_ERR;
	if (mask & (EPOLLHUP | EPOLLRDHUP))
		events |= AIRY_DMN_HUP;

	return events;
}

int airy_dmn_attach(struct airy_dmn *dmn, int fd, uint32_t mask,
		    enum airy_dmn_kind kind)
{
	int slot;
	int ret;

	slot = dmn_slot(dmn);
	if (slot < 0)
		return slot;

	ret = airy_dmn_ep(dmn);
	if (!ret)
		ret = dmn_ctl(dmn, EPOLL_CTL_ADD, fd, slot, mask);
	if (ret)
		return ret;

	dmn->evts[slot].fd = fd;
	dmn->evts[slot].kind = kind;
	dmn->evts[slot].used = true;
	dmn->nr_evt++;

	return slot;
}

int airy_dmn_detach(struct airy_dmn *dmn, int slot)
{
	struct airy_dmn_evt *evt;

	if (!dmn || slot < 0 || slot >= (int)AIRY_ARRAY_SIZE(dmn->evts))
		return -AIRY_EINVAL;

	evt = &dmn->evts[slot];
	if (!evt->used)
		return -AIRY_ENOENT;

	if (dmn->epfd >= 0 &&
	    epoll_ctl(dmn->epfd, EPOLL_CTL_DEL, evt->fd, NULL) &&
	    errno != ENOENT && errno != EBADF)
		return airy_dmn_err(errno);

	memset(evt, 0, sizeof(*evt));
	dmn->nr_evt--;

	return 0;
}

int airy_dmn_add(struct airy_dmn *dmn, const struct airy_dmn_src *src)
{
	int slot;

	if (!dmn || !src || src->fd < 0 || !src->recv || !src->events)
		return -AIRY_EINVAL;
	if (src->events & ~DMN_EVT_ALL)
		return -AIRY_EINVAL;
	if (!airy_dmn_evok(dmn))
		return -AIRY_EBUSY;
	if (airy_dmn_find(dmn, src->fd) >= 0)
		return -AIRY_EEXIST;

	slot = airy_dmn_attach(dmn, src->fd, airy_dmn_mask(src->events),
			       AIRY_DMN_KIND_FD);
	if (slot < 0)
		return slot;

	dmn->evts[slot].events = src->events;
	dmn->evts[slot].cb.fd.recv = src->recv;
	dmn->evts[slot].cb.fd.data = src->data;

	return 0;
}

int airy_dmn_del(struct airy_dmn *dmn, int fd)
{
	int slot;

	if (!dmn || fd < 0)
		return -AIRY_EINVAL;
	if (!airy_dmn_delok(dmn))
		return -AIRY_EBUSY;

	slot = airy_dmn_find(dmn, fd);
	if (slot < 0)
		return slot;

	return airy_dmn_detach(dmn, slot);
}

int airy_dmn_mod(struct airy_dmn *dmn, int fd, unsigned int events)
{
	struct airy_dmn_evt *evt;
	int slot;
	int ret;

	if (!dmn || fd < 0 || !events || (events & ~DMN_EVT_ALL))
		return -AIRY_EINVAL;
	if (!airy_dmn_evok(dmn))
		return -AIRY_EBUSY;

	slot = airy_dmn_find(dmn, fd);
	if (slot < 0)
		return slot;

	evt = &dmn->evts[slot];
	if (evt->kind != AIRY_DMN_KIND_FD)
		return -AIRY_EINVAL;

	ret = dmn_ctl(dmn, EPOLL_CTL_MOD, fd, slot, airy_dmn_mask(events));
	if (ret)
		return ret;

	evt->events = events;

	return 0;
}
