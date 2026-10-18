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
 * @payload: 消息体，空载荷为 NULL，仅在回调期间有效
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
 * 框架按 128 字节头 + payload_len 读取一条完整消息，校验 magic、
 * 保留标志位、payload_len 上界与 C-S12 摘要后回调；一条消息须由
 * 单次 send 写入同一条记录或数据报。帧短于头部、magic、标志位或
 * 摘要不符、对端关闭时主循环以负值终止，不做字节流重同步。描述符
 * 由调用方持有。
 *
 * Return: 0 成功；-AIRY_EINVAL 参数非法或 fd 类型不符；
 *         -AIRY_EEXIST/-AIRY_EBUSY/-AIRY_ENOMEM 同 airy_dmn_add()。
 */
int airy_dmn_ipc(struct airy_dmn *dmn, int fd, airy_dmn_msg msg, void *data);

/**
 * airy_dmn_send() - 经已注册 IPC 通道发送一条完整消息
 * @dmn: 上下文
 * @fd: 已由 airy_dmn_ipc() 注册的 IPC 通道描述符
 * @hdr: Layout C 消息头；payload_len 为消息体长度
 * @payload: 消息体，payload_len 为 0 时可为 NULL
 *
 * 校验 magic、保留标志位与 payload_len 上界后，按 C-S12 计算摘要
 * （覆盖 header[0:52) + payload）并盖写副本的 crc32 字段，再以
 * 128 字节头加消息体单次写入一条记录或数据报。通道须为已连接
 * 描述符；部分写入视为通道失序。调用方持有的 hdr 不被修改。
 *
 * Return: 0 成功；-AIRY_EINVAL 参数非法或 fd 非 IPC 通道；
 *         -AIRY_ENOENT fd 未注册；-AIRY_EAGAIN 发送缓冲已满；
 *         -AIRY_EIO 通道失序或对端已关闭。
 */
int airy_dmn_send(struct airy_dmn *dmn, int fd,
		  const struct airy_ipc_msg_hdr *hdr, const void *payload);

/**
 * struct airy_dmn_frame - 批量发送的一帧
 * @hdr: Layout C 消息头；opcode 与 BATCH_TAIL 由框架盖写
 * @payload: 消息体，payload_len 为 0 时可为 NULL
 */
struct airy_dmn_frame {
	const struct airy_ipc_msg_hdr *hdr;
	const void *payload;
};

/**
 * airy_dmn_batch() - 经已注册 IPC 通道批量发送消息
 * @dmn: 上下文
 * @fd: 已由 airy_dmn_ipc() 注册的 IPC 通道描述符
 * @frames: 待发送帧数组
 * @nr: 帧数，至少 2
 * @done: 已完整写出的帧数，可为 NULL
 *
 * 先校验全部帧再写出，任一帧非法时不发送任何消息，不留半批。opcode
 * 统一盖写为 AIRY_IPC_OP_SEND_BATCH，仅末帧置 AIRY_IPC_FLAG_BATCH_TAIL，
 * 其余帧清除该位；每帧按 C-S12 独立计算摘要。实现按固定上限分块提交，
 * 不分配堆内存。部分写出时 *done 为已完整写出的帧数，调用方只能从该
 * 位置续发，重发整批会产生重复消息。接收方逐帧回调，按 BATCH_TAIL
 * 判断一批结束。
 *
 * Return: 0 全部写出；-AIRY_EINVAL 参数非法、任一帧非法或 fd 非 IPC
 *         通道；-AIRY_ENOENT fd 未注册；-AIRY_EAGAIN 发送缓冲已满；
 *         -AIRY_EIO 通道失序或对端已关闭。
 */
int airy_dmn_batch(struct airy_dmn *dmn, int fd,
		   const struct airy_dmn_frame *frames, size_t nr,
		   size_t *done);

/**
 * airy_dmn_notify() - 向 systemd 上报就绪、看门狗或状态
 * @state: 一行或多行 KEY=VALUE，以 '\n' 分隔
 *
 * 从环境变量 NOTIFY_SOCKET 取得目标 Unix 数据报地址并发送 @state
 * 原文；路径以 '@' 开头时表示抽象命名空间。未在 systemd 下运行时
 * 变量缺省，按无操作成功返回。本函数只做传输，不解析内容，由调用
 * 方决定 READY=1、WATCHDOG=1 或 STATUS=... 等语义；状态串按原样
 * 发送，需要尾随换行时由调用方自行包含。
 *
 * Return: 0 已发送或未配置；-AIRY_EINVAL 状态为空或超长、套接字路
 *         径非法；-AIRY_ENOENT 套接字不存在；-AIRY_ENOMEM 描述符耗
 *         尽；-AIRY_EIO 目标拒绝或发送失败。
 */
int airy_dmn_notify(const char *state);

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
 *
 * 可在任意线程调用，与 airy_dmn_live() 并发安全。
 */
void airy_dmn_beat(struct airy_dmn *dmn);

/**
 * airy_dmn_live() - 查询心跳是否新鲜
 * @dmn: 上下文
 *
 * 容忍窗口为 beat_ms * miss_max，最近一次心跳落在窗口内即为新鲜。
 * 首次 airy_dmn_beat() 之前不视为新鲜，避免未上报即被当作存活。
 *
 * Return: beat_ms 已配置且首次心跳后在容忍窗口内返回 true；否则
 *         false。
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
