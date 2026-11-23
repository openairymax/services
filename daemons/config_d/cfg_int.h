/* SPDX-License-Identifier: AGPL-3.0-or-later OR Apache-2.0 */
/*
 * Copyright (c) 2025-2026 SPHARX Ltd. All Rights Reserved.
 *
 * cfg_int.h —— config_d 内部契约（不随头文件包安装）
 *
 * 键表是 schema 的唯一事实源：INI 解析按它落值，装载后的整体校验
 * 也按它复查。新增一个配置项只改一处，解析与校验不会出现两套范围。
 */
#ifndef _AIRY_CFG_INT_H
#define _AIRY_CFG_INT_H

#include <stdbool.h>
#include <stddef.h>

#include "cfg.h"

struct cfg_key {
	const char *sec;
	const char *key;
	size_t off;
	int min;
	int max;
	unsigned long long set;	/* 非零：只接受位图中列出的离散取值 */
	bool gate;	/* 版本闸门项：由装载层单独判定，不做取值范围校验 */
	bool frozen;	/* 热重载拒绝变更 */
};

extern const struct cfg_key g_cfg_keys[];
extern const size_t g_cfg_keynr;

/* 单一取值判据：装载与复查共用，避免两处范围各说各话 */
bool cfg_key_ok(const struct cfg_key *k, int v);

int cfg_ini_read(const char *path, struct airy_cfg *cfg, char *err, size_t len);
int cfg_ini_self(const char *root, struct airy_cfg *cfg, char *err, size_t len);
int cfg_ini_drop(const char *root, struct airy_cfg *cfg, char *err, size_t len);

#endif /* _AIRY_CFG_INT_H */
