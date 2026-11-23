// SPDX-License-Identifier: AGPL-3.0-or-later OR Apache-2.0
/*
 * Copyright (c) 2025-2026 SPHARX Ltd. All Rights Reserved.
 *
 * macro_d.h —— macro_d 的进程内契约
 *
 * 配置只决定两件事：监督通道设备与各故障码的裁决动作。裁决执行的
 * 内核命令通道（[IND] ADJUDICATE）要到 0.1.x 才接入，本版把判定
 * 结果连同故障上下文写入日志，不直接改动 Agent 状态。
 *
 * SSoT: docs-linux/20-modules/10-user-supervisor-daemon.md §3、§5
 */
#ifndef _MACRO_D_H
#define _MACRO_D_H

#include <stddef.h>
#include <stdint.h>

#include <linux/airymax/superv.h>

#define MACRO_DEV_MAX	64
#define MACRO_PATH_MAX	256
#define MACRO_LINE_MAX	512
#define MACRO_FAULT_NR	6

struct macro_cfg {
	char		dev[MACRO_DEV_MAX];
	enum airy_adjud	act[MACRO_FAULT_NR];
};

int macro_cfg_def(struct macro_cfg *cfg);
int macro_cfg_load(const char *path, struct macro_cfg *cfg, char *err,
		   size_t len);
enum airy_adjud macro_cfg_act(const struct macro_cfg *cfg, uint32_t fault);
const char *macro_fault(uint32_t fault);
const char *macro_verdict(enum airy_adjud act);

#endif /* _MACRO_D_H */
