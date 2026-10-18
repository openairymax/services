// SPDX-License-Identifier: AGPL-3.0-or-later OR Apache-2.0
/*
 * Copyright (c) 2025-2026 SPHARX Ltd. All Rights Reserved.
 *
 * dmn_sig.c — daemon 框架信号收口
 *
 * AIRY_DMN_F_SIG 生效时，框架在进入主循环前阻塞 SIGTERM/SIGINT/
 * SIGHUP 并创建 signalfd，使信号以描述符事件进入同一 epoll，避免
 * 异步信号处理函数的重入风险。SIGTERM/SIGINT 置停止请求，SIGHUP
 * 调用 ops.reload 完成配置热加载；主循环退出后恢复调用线程原有的
 * 信号掩码。
 *
 * IRON-9 v3: [IND] 独立实现层 — agent-linux services 子仓内部。
 *
 * SSoT: docs-linux/20-modules/02-services.md §3.7
 */

#define _GNU_SOURCE

#include <errno.h>
#include <signal.h>
#include <stdint.h>
#include <sys/epoll.h>
#include <sys/signalfd.h>
#include <unistd.h>

#include "dmn_int.h"

static int dmn_sigset(sigset_t *set, sigset_t *old)
{
	sigemptyset(set);
	sigaddset(set, SIGTERM);
	sigaddset(set, SIGINT);
	sigaddset(set, SIGHUP);

	if (sigprocmask(SIG_BLOCK, set, old))
		return airy_dmn_err(errno);

	return 0;
}

int airy_dmn_sigarm(struct airy_dmn *dmn)
{
	struct epoll_event ev = {
		.events = EPOLLIN,
		.data.u32 = AIRY_DMN_SIG,
	};
	sigset_t set;
	int ret;

	if (!(dmn->flags & AIRY_DMN_F_SIG) || dmn->sigfd >= 0)
		return 0;

	ret = dmn_sigset(&set, &dmn->old_mask);
	if (ret)
		return ret;
	dmn->sig_saved = true;

	dmn->sigfd = signalfd(-1, &set, SFD_CLOEXEC | SFD_NONBLOCK);
	if (dmn->sigfd < 0) {
		ret = airy_dmn_err(errno);
		goto err;
	}

	if (epoll_ctl(dmn->epfd, EPOLL_CTL_ADD, dmn->sigfd, &ev)) {
		ret = airy_dmn_err(errno);
		goto err;
	}

	return 0;

err:
	if (dmn->sigfd >= 0) {
		close(dmn->sigfd);
		dmn->sigfd = -1;
	}
	airy_dmn_sigoff(dmn);

	return ret;
}

void airy_dmn_sigoff(struct airy_dmn *dmn)
{
	if (!dmn->sig_saved)
		return;

	(void)sigprocmask(SIG_SETMASK, &dmn->old_mask, NULL);
	dmn->sig_saved = false;
}

static int dmn_sigact(struct airy_dmn *dmn, uint32_t signo)
{
	if (signo == SIGTERM || signo == SIGINT)
		atomic_store(&dmn->stop_req, true);

	if (signo != SIGHUP || !dmn->ops.reload)
		return 0;

	return dmn->ops.reload(dmn, dmn->ops.data);
}

int airy_dmn_sigev(struct airy_dmn *dmn)
{
	struct signalfd_siginfo si;
	ssize_t n;
	int ret;

	if (dmn->sigfd < 0)
		return 0;

	for (;;) {
		n = read(dmn->sigfd, &si, sizeof(si));
		if (n < 0) {
			if (errno == EINTR)
				continue;
			if (errno == EAGAIN || errno == EWOULDBLOCK)
				return 0;

			return airy_dmn_err(errno);
		}

		ret = dmn_sigact(dmn, si.ssi_signo);
		if (ret)
			return ret;
	}
}
