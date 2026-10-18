// SPDX-License-Identifier: AGPL-3.0-or-later OR Apache-2.0
/*
 * Copyright (c) 2025-2026 SPHARX Ltd. All Rights Reserved.
 *
 * dmn_beat.c — daemon 框架心跳记账
 *
 * 业务侧以 airy_dmn_beat() 刷新存活时刻，监管侧以 airy_dmn_live()
 * 判断是否仍在容忍窗口内。两者可在不同线程并发调用，时间戳以
 * C11 原子量保存，读侧不加锁。时刻取 CLOCK_MONOTONIC 毫秒，只做
 * 相对比较，不受系统时间跳变影响。
 *
 * IRON-9 v3: [IND] 独立实现层 — agent-linux services 子仓内部。
 *
 * SSoT: docs-linux/20-modules/02-services.md §3.7
 */

#define _GNU_SOURCE

#include <stdatomic.h>

#include "dmn_int.h"

void airy_dmn_beat(struct airy_dmn *dmn)
{
	if (!dmn)
		return;

	atomic_store_explicit(&dmn->last_beat, airy_dmn_now(),
			      memory_order_relaxed);
}

bool airy_dmn_live(const struct airy_dmn *dmn)
{
	uint64_t span;
	uint64_t last;

	if (!dmn || !dmn->beat_ms)
		return false;

	span = (uint64_t)dmn->beat_ms * dmn->miss_max;
	last = atomic_load_explicit(&dmn->last_beat, memory_order_relaxed);

	return airy_dmn_now() - last <= span;
}
