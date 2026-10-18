// SPDX-License-Identifier: AGPL-3.0-or-later OR Apache-2.0
/*
 * Copyright (c) 2025-2026 SPHARX Ltd. All Rights Reserved.
 *
 * dmn_loop.c — daemon 框架主循环与周期 tick
 *
 * epoll_wait() 是唯一阻塞点：fd 与 IPC 事件按槽位分派，tick 按单调
 * 时钟截止点触发。框架自持的 eventfd 以 AIRY_DMN_CTL 哨兵注册，使
 * airy_dmn_stop() 在没有外部事件时也能立即打断等待，且不占用事件
 * 源槽位；signalfd 以 AIRY_DMN_SIG 哨兵注册，信号经同一循环分派。
 * ERR/HUP 不受注册掩码过滤，避免对端关闭后反复唤醒空转。
 *
 * IRON-9 v3: [IND] 独立实现层 — agent-linux services 子仓内部。
 *
 * SSoT: docs-linux/20-modules/02-services.md §3.7
 */

#define _GNU_SOURCE

#include <errno.h>
#include <limits.h>
#include <stdint.h>
#include <sys/epoll.h>
#include <sys/eventfd.h>
#include <unistd.h>

#include "dmn_int.h"

static void dmn_drain(struct airy_dmn *dmn)
{
	uint64_t val;

	(void)!read(dmn->wfd, &val, sizeof(val));
}

static int dmn_wake(struct airy_dmn *dmn)
{
	struct epoll_event ev = {
		.events = EPOLLIN,
		.data.u32 = AIRY_DMN_CTL,
	};
	int ret;

	if (dmn->wfd >= 0)
		return 0;

	dmn->wfd = eventfd(0, EFD_CLOEXEC | EFD_NONBLOCK);
	if (dmn->wfd < 0)
		return airy_dmn_err(errno);

	if (!epoll_ctl(dmn->epfd, EPOLL_CTL_ADD, dmn->wfd, &ev))
		return 0;

	ret = airy_dmn_err(errno);
	close(dmn->wfd);
	dmn->wfd = -1;

	return ret;
}

static int dmn_wait(struct airy_dmn *dmn, struct epoll_event *evs)
{
	uint64_t now;
	uint64_t left;
	int timeout = -1;
	int n;

	if (dmn->tick_ms && dmn->ops.tick) {
		now = airy_dmn_now();
		left = dmn->next_tick > now ? dmn->next_tick - now : 0;
		timeout = left > INT_MAX ? INT_MAX : (int)left;
	}

	n = epoll_wait(dmn->epfd, evs, AIRY_DMN_EPOLL_MAX, timeout);
	if (n >= 0)
		return n;
	if (errno == EINTR)
		return 0;

	return airy_dmn_err(errno);
}

static int dmn_fire_fd(struct airy_dmn *dmn, struct airy_dmn_evt *evt,
		       const struct epoll_event *ev)
{
	unsigned int events;

	events = airy_dmn_rev(ev->events);
	events &= evt->events | AIRY_DMN_ERR | AIRY_DMN_HUP;
	if (!events)
		return 0;

	return evt->cb.fd.recv(dmn, evt->fd, events, evt->cb.fd.data);
}

static int dmn_fire_one(struct airy_dmn *dmn, const struct epoll_event *ev)
{
	struct airy_dmn_evt *evt;
	uint32_t slot;

	slot = ev->data.u32;
	if (slot == AIRY_DMN_CTL) {
		dmn_drain(dmn);
		return 0;
	}
	if (slot == AIRY_DMN_SIG)
		return airy_dmn_sigev(dmn);
	if (slot >= AIRY_ARRAY_SIZE(dmn->evts))
		return 0;

	evt = &dmn->evts[slot];
	if (!evt->used)
		return 0;
	if (evt->kind == AIRY_DMN_KIND_IPC)
		return airy_dmn_ipcrecv(dmn, evt);

	return dmn_fire_fd(dmn, evt, ev);
}

static int dmn_fire(struct airy_dmn *dmn, const struct epoll_event *evs,
		    int n)
{
	unsigned int i;
	int ret;

	for (i = 0; i < (unsigned int)n; i++) {
		ret = dmn_fire_one(dmn, &evs[i]);
		if (ret)
			return ret;
	}

	return 0;
}

static int dmn_tick(struct airy_dmn *dmn)
{
	uint64_t now;

	if (!dmn->tick_ms || !dmn->ops.tick)
		return 0;

	now = airy_dmn_now();
	if (now < dmn->next_tick)
		return 0;

	dmn->next_tick = now + dmn->tick_ms;

	return dmn->ops.tick(dmn, dmn->ops.data);
}

int airy_dmn_run(struct airy_dmn *dmn)
{
	struct epoll_event evs[AIRY_DMN_EPOLL_MAX];
	int ret = 0;
	int sret = 0;
	int n;

	if (!dmn || !dmn->bound)
		return -AIRY_EINVAL;
	if (dmn->state != AIRY_DMN_INIT)
		return -AIRY_EBUSY;

	ret = airy_dmn_ep(dmn);
	if (!ret)
		ret = dmn_wake(dmn);
	if (!ret)
		ret = airy_dmn_sigarm(dmn);
	if (ret)
		return ret;

	if (dmn->ops.start) {
		ret = dmn->ops.start(dmn, dmn->ops.data);
		if (ret) {
			dmn->state = AIRY_DMN_STOPPED;
			airy_dmn_sigoff(dmn);
			return ret;
		}
	}

	dmn->state = AIRY_DMN_RUNNING;
	dmn->next_tick = airy_dmn_now() + dmn->tick_ms;

	while (dmn->state == AIRY_DMN_RUNNING) {
		n = dmn_wait(dmn, evs);
		if (n < 0) {
			ret = n;
			break;
		}

		ret = dmn_fire(dmn, evs, n);
		if (ret || dmn->state != AIRY_DMN_RUNNING)
			break;

		ret = dmn_tick(dmn);
		if (ret)
			break;
	}

	dmn->state = AIRY_DMN_STOPPING;
	if (dmn->ops.stop)
		sret = dmn->ops.stop(dmn, dmn->ops.data);
	dmn->state = AIRY_DMN_STOPPED;
	airy_dmn_sigoff(dmn);

	return ret ? ret : sret;
}

void airy_dmn_stop(struct airy_dmn *dmn)
{
	uint64_t one = 1;

	if (!dmn)
		return;

	if (dmn->state == AIRY_DMN_RUNNING)
		dmn->state = AIRY_DMN_STOPPING;

	if (dmn->wfd >= 0)
		(void)!write(dmn->wfd, &one, sizeof(one));
}
