/* SPDX-License-Identifier: AGPL-3.0-or-later OR Apache-2.0 */
/*
 * Copyright (c) 2025-2026 SPHARX Ltd. All Rights Reserved.
 *
 * airy/daemon.h — services 子仓 daemon 框架公共契约
 *
 * 为 12 个 daemon 提供统一的主循环、fd 事件源、IPC 通道注册、周期
 * tick、信号收口与心跳记账。框架只实现机制，裁决与业务处理由各
 * daemon 通过 struct airy_dmn_ops 与事件回调注入。
 *
 * IRON-9 v3: [IND] 独立实现层 — 仅存在于 agent-linux services 子仓，
 *            不与 agentrt 共享源码；[SC] 类型一律引用
 *            kernel/include/uapi/linux/airymax/，不设物理副本。
 *
 * SSoT: docs-linux/20-modules/02-services.md §3.7
 *       docs-linux/10-architecture/07-directory-structure.md §4.2
 */
#ifndef _AIRY_DAEMON_H
#define _AIRY_DAEMON_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include <linux/airymax/error.h>
#include <linux/airymax/ipc.h>

/**
 * enum airy_dmn_state - daemon 生命周期状态
 * @AIRY_DMN_INIT: 已创建，主循环未进入
 * @AIRY_DMN_RUNNING: 主循环运行中
 * @AIRY_DMN_STOPPING: 已收到停止请求，等待当前回调返回
 * @AIRY_DMN_STOPPED: 主循环已退出
 */
enum airy_dmn_state {
	AIRY_DMN_INIT = 0,
	AIRY_DMN_RUNNING,
	AIRY_DMN_STOPPING,
	AIRY_DMN_STOPPED,
};

struct airy_dmn;

/**
 * typedef airy_dmn_cb - 生命周期与 tick 回调
 * @dmn: daemon 上下文
 * @data: struct airy_dmn_ops.data
 *
 * Return: 0 成功；负值终止主循环并作为 airy_dmn_run() 的返回值。
 */
typedef int (*airy_dmn_cb)(struct airy_dmn *dmn, void *data);

/**
 * typedef airy_dmn_ev - fd 事件回调
 * @dmn: daemon 上下文
 * @fd: 触发事件的描述符
 * @events: AIRY_DMN_IN/OUT/ERR/HUP 位掩码
 * @data: struct airy_dmn_src.data
 *
 * Return: 0 成功；负值终止主循环。
 */
typedef int (*airy_dmn_ev)(struct airy_dmn *dmn, int fd,
			   unsigned int events, void *data);

/**
 * typedef airy_dmn_msg - IPC 消息回调
 * @dmn: daemon 上下文
 * @hdr: 已校验 magic 与 payload_len 的 128 字节消息头
 * @payload: 消息体，仅在回调期间有效
 * @data: airy_dmn_ipc() 注册的私有数据
 *
 * Return: 0 成功；负值终止主循环。
 */
typedef int (*airy_dmn_msg)(struct airy_dmn *dmn,
			    const struct airy_ipc_msg_hdr *hdr,
			    const void *payload, void *data);

/**
 * struct airy_dmn_cfg - 创建参数
 * @name: 进程名，必填，用于日志与状态标识
 * @config: 配置文件路径，可为 NULL
 * @tick_ms: 周期 tick 毫秒数，0 关闭
 * @beat_ms: 心跳间隔毫秒数，0 关闭存活记账
 * @miss_max: 允许连续错过的心跳数，0 取默认值 3
 * @flags: AIRY_DMN_F_* 位掩码
 */
struct airy_dmn_cfg {
	const char *name;
	const char *config;
	unsigned int tick_ms;
	unsigned int beat_ms;
	unsigned int miss_max;
	unsigned int flags;
};

/**
 * struct airy_dmn_ops - daemon 业务回调
 * @start: 主循环进入前调用，可返回负值阻止启动
 * @tick: 每个 tick_ms 周期调用
 * @reload: 收到 SIGHUP 时调用，用于配置热加载
 * @stop: 主循环退出前调用，用于释放业务资源
 * @data: 透传给上述回调
 *
 * 未使用的回调置 NULL。
 */
struct airy_dmn_ops {
	airy_dmn_cb start;
	airy_dmn_cb tick;
	airy_dmn_cb reload;
	airy_dmn_cb stop;
	void *data;
};

/**
 * struct airy_dmn_src - fd 事件源
 * @fd: 调用方持有的描述符，框架不关闭
 * @events: AIRY_DMN_IN/OUT/ERR/HUP 位掩码
 * @recv: 事件回调，必填
 * @data: 透传给 recv
 */
struct airy_dmn_src {
	int fd;
	unsigned int events;
	airy_dmn_ev recv;
	void *data;
};

#define AIRY_DMN_IN	0x0001u
#define AIRY_DMN_OUT	0x0002u
#define AIRY_DMN_ERR	0x0004u
#define AIRY_DMN_HUP	0x0008u

#define AIRY_DMN_F_SIG	0x0001u

/**
 * airy_dmn_new() - 创建 daemon 上下文
 * @cfg: 创建参数；name 必填
 *
 * Return: 成功返回上下文；失败返回 NULL 并置 errno（EINVAL/ENOMEM）。
 */
struct airy_dmn *airy_dmn_new(const struct airy_dmn_cfg *cfg);

/**
 * airy_dmn_free() - 释放 daemon 上下文
 * @dmn: 上下文；NULL 安全
 *
 * 必须在 airy_dmn_run() 返回后调用，框架不关闭任何外部 fd。
 */
void airy_dmn_free(struct airy_dmn *dmn);

/**
 * airy_dmn_reg() - 注册业务回调
 * @dmn: 上下文
 * @ops: 回调表；结构体按值复制
 *
 * Return: 0 成功；-AIRY_EINVAL 参数非法；-AIRY_EBUSY 主循环已启动。
 */
int airy_dmn_reg(struct airy_dmn *dmn, const struct airy_dmn_ops *ops);

/**
 * airy_dmn_add() - 注册 fd 事件源
 * @dmn: 上下文
 * @src: 事件源；fd 由调用方持有
 *
 * Return: 0 成功；-AIRY_EINVAL 参数非法；-AIRY_EEXIST fd 已注册；
 *         -AIRY_EBUSY 主循环已启动；-AIRY_ENOMEM 资源不足。
 */
int airy_dmn_add(struct airy_dmn *dmn, const struct airy_dmn_src *src);

/**
 * airy_dmn_del() - 注销 fd 事件源
 * @dmn: 上下文
 * @fd: 已注册的描述符
 *
 * Return: 0 成功；-AIRY_ENOENT 未注册。
 */
int airy_dmn_del(struct airy_dmn *dmn, int fd);

/**
 * airy_dmn_ipc() - 注册 IPC 通道
 * @dmn: 上下文
 * @fd: 已连接的 SOCK_SEQPACKET 或 SOCK_DGRAM 描述符
 * @msg: 消息回调，必填
 * @data: 透传给 msg
 *
 * 框架按 128 字节头 + payload_len 读取一条完整消息，校验 magic 与
 * 长度上界后回调；描述符由调用方持有。
 *
 * Return: 0 成功；-AIRY_EINVAL 参数非法或 fd 类型不符；
 *         -AIRY_EEXIST/-AIRY_EBUSY/-AIRY_ENOMEM 同 airy_dmn_add()。
 */
int airy_dmn_ipc(struct airy_dmn *dmn, int fd, airy_dmn_msg msg, void *data);

/**
 * airy_dmn_run() - 进入主循环
 * @dmn: 上下文
 *
 * 依次调用 start、epoll 事件循环、stop。AIRY_DMN_F_SIG 生效时，
 * 运行线程阻塞 SIGTERM/SIGINT/SIGHUP 并由 signalfd 收口：
 * SIGTERM/SIGINT 触发停止，SIGHUP 触发 reload；退出后恢复原掩码。
 * 多线程 daemon 须在创建线程前自行阻塞这些信号。框架自持唤醒源，
 * airy_dmn_stop() 可在其他线程调用并立即打断等待。
 *
 * Return: 0 正常停止；-AIRY_EINVAL 未注册回调；-AIRY_EBUSY 主循环
 *         已启动；-AIRY_ENOMEM/-AIRY_EACCES/-AIRY_EIO 事件循环资源
 *         创建失败；负值来自回调。
 */
int airy_dmn_run(struct airy_dmn *dmn);

/**
 * airy_dmn_stop() - 请求停止主循环
 * @dmn: 上下文；NULL 安全
 *
 * 只置停止标志，事件循环在当前回调返回后退出。
 */
void airy_dmn_stop(struct airy_dmn *dmn);

/**
 * airy_dmn_beat() - 记录一次心跳
 * @dmn: 上下文；NULL 安全
 */
void airy_dmn_beat(struct airy_dmn *dmn);

/**
 * airy_dmn_live() - 查询心跳是否新鲜
 * @dmn: 上下文
 *
 * Return: beat_ms 已配置且最近一次心跳未超过 beat_ms * miss_max
 *         时返回 true；否则 false。
 */
bool airy_dmn_live(const struct airy_dmn *dmn);

/**
 * airy_dmn_now() - 读取单调时钟
 *
 * Return: CLOCK_MONOTONIC 毫秒数。
 */
uint64_t airy_dmn_now(void);

/**
 * airy_dmn_state() - 查询生命周期状态
 * @dmn: 上下文
 *
 * Return: 当前状态；dmn 为 NULL 时返回 AIRY_DMN_INIT。
 */
enum airy_dmn_state airy_dmn_state(const struct airy_dmn *dmn);

/**
 * airy_dmn_name() - 查询 daemon 名
 * @dmn: 上下文
 *
 * Return: cfg.name 指针；dmn 为 NULL 时返回空串。
 */
const char *airy_dmn_name(const struct airy_dmn *dmn);

_Static_assert(AIRY_IPC_HDR_SIZE == 128,
	       "daemon framework assumes the 128-byte IPC header");
_Static_assert(AIRY_IPC_MAGIC == 0x41524531u,
	       "daemon framework validates the Layout C magic");
_Static_assert(AIRY_DMN_STOPPED == 3,
	       "AIRY_DMN_* values are part of the framework contract");
_Static_assert(AIRY_DMN_HUP == 0x0008u,
	       "AIRY_DMN_* event bits are part of the framework contract");

#endif /* _AIRY_DAEMON_H */
