/* SPDX-License-Identifier: AGPL-3.0-or-later OR Apache-2.0 */
/*
 * Copyright (c) 2025-2026 SPHARX Ltd. All Rights Reserved.
 *
 * cfg.h —— A-UCS 统一配置模型的共享契约
 *
 * config_d 是 /etc/agentrt 的唯一读者与校验者：装载 agentrt.conf、
 * config.conf 与 conf.d/NN-*.conf，按 [SS] 语义映射表校验取值范围，
 * 再把生效配置以不可变快照发布给进程内读者。热重载只替换快照，
 * 已持有旧快照的读者看到旧值直到释放，等价于内核侧 RCU 指针切换。
 *
 * 本头只暴露模型、装载、快照与变更监听接口；INI 词法与 sysctl 落值
 * 为实现细节，见 cfg_int.h。
 *
 * SSoT: docs-linux/20-modules/11-unified-config.md §2–§4
 *       docs-linux/100-operations/02-configuration.md §4
 */
#ifndef _AIRY_CFG_H
#define _AIRY_CFG_H

#include <stdbool.h>
#include <stddef.h>

/* [SC] 共享契约：配置版本号，与 kernel uapi config.h 同值 */
#define AIRY_CFG_VERSION	1
#define AIRY_CFG_ROOT		"/etc/agentrt"
#define AIRY_CFG_MAIN		"agentrt.conf"
#define AIRY_CFG_SELF		"config.conf"
#define AIRY_CFG_DROPIN		"conf.d"
#define AIRY_CFG_PATHMAX	256
#define AIRY_CFG_LINEMAX	512

/* 生效配置：字段顺序与 [SS] 语义映射表一致，全部为有界整数 */
struct airy_cfg {
	int version;
	int log_level;
	int log_ring;
	int ipc_ring;
	int ipc_fast;
	int sched_rt;
	int sched_dl;
	int beat_iv;
	int beat_to;
	int cap_max;
	int cap_epoch;
	int cap_tag;
	int cap_forge;
};

/* 快照对读者不透明：持有期间旧值不被回收 */
struct cfg_snap;

void cfg_default(struct airy_cfg *cfg);
int cfg_valid(const struct airy_cfg *cfg);
bool cfg_same(const struct airy_cfg *a, const struct airy_cfg *b);
int cfg_load(const char *root, struct airy_cfg *cfg, char *err, size_t len);
int cfg_apply(const struct airy_cfg *cfg, char *err, size_t len);
int cfg_publish(const struct airy_cfg *cfg);
const struct cfg_snap *cfg_hold(void);
const struct airy_cfg *cfg_view(const struct cfg_snap *snap);
void cfg_put(const struct cfg_snap *snap);
int cfg_watch(const char *root, char *err, size_t len);
int cfg_drop(int fd, const char *root);
int cfg_drain(int fd);
void cfg_dump(const struct airy_cfg *cfg);
void cfg_log(const char *level, const char *fmt, ...)
	__attribute__((format(printf, 2, 3)));

#endif /* _AIRY_CFG_H */
