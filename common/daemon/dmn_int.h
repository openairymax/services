/* SPDX-License-Identifier: AGPL-3.0-or-later OR Apache-2.0 */
/*
 * Copyright (c) 2025-2026 SPHARX Ltd. All Rights Reserved.
 *
 * dmn_int.h — services daemon 框架内部契约
 *
 * 仅供 services/common/daemon/ 下的实现分片包含，不对外安装。定义
 * 事件源统一表与上下文结构，使各分片共享同一份内部布局而不重复
 * 声明；公共可用面仍以 <airy/daemon.h> 为准。
 *
 * IRON-9 v3: [IND] 独立实现层 — agent-linux services 子仓内部。
 *
 * SSoT: docs-linux/20-modules/02-services.md §3.7
 */
#ifndef _AIRY_DMN_INT_H
#define _AIRY_DMN_INT_H

#include <limits.h>
#include <signal.h>
#include <stdatomic.h>
#include <stdbool.h>
#include <stdint.h>

#include <airy/daemon.h>

/* 单 daemon 的事件源上限：覆盖 12 个 daemon 的 fd 与 IPC 通道总和。 */
#define AIRY_DMN_MAX_SRC	32
#define AIRY_DMN_NAME_MAX	32
#define AIRY_DMN_DFL_MISS	3
#define AIRY_DMN_EPOLL_MAX	64

/* 框架自持描述符在 epoll data.u32 中的哨兵值，不占用事件源槽位。 */
#define AIRY_DMN_CTL		UINT32_MAX
#define AIRY_DMN_SIG		(UINT32_MAX - 1)

#define AIRY_ARRAY_SIZE(arr)	(sizeof(arr) / sizeof((arr)[0]))

/**
 * enum airy_dmn_kind - 事件源类型
 * @AIRY_DMN_KIND_FD: 普通 fd，按 AIRY_DMN_IN/OUT/ERR/HUP 回调
 * @AIRY_DMN_KIND_IPC: IPC 通道，按 128 字节头读取并校验后回调
 */
enum airy_dmn_kind {
	AIRY_DMN_KIND_FD = 0,
	AIRY_DMN_KIND_IPC,
};

/**
 * struct airy_dmn_evt - 统一事件源表项
 * @used: 槽位占用标志；epoll 以槽位下标作为 data.u32，不压缩表
 * @fd: 调用方持有的描述符
 * @events: AIRY_DMN_* 位掩码
 * @kind: 事件源类型，决定 cb 联合体的有效成员
 * @cb: 回调联合体，fd 与 ipc 共用同一槽位以消除重复表
 */
struct airy_dmn_evt {
	bool used;
	int fd;
	unsigned int events;
	enum airy_dmn_kind kind;
	union {
		struct {
			airy_dmn_ev recv;
			void *data;
		} fd;
		struct {
			airy_dmn_msg msg;
			void *data;
		} ipc;
	} cb;
};

/**
 * struct airy_dmn - daemon 框架上下文
 * @name: 进程名副本，长度受 AIRY_DMN_NAME_MAX 限制
 * @config: 配置路径副本，可为空串
 * @tick_ms: 周期 tick 毫秒数，0 关闭
 * @beat_ms: 心跳间隔毫秒数，0 关闭存活记账
 * @miss_max: 允许连续错过的心跳数
 * @flags: AIRY_DMN_F_* 位掩码
 * @state: 生命周期状态
 * @bound: 是否已通过 airy_dmn_reg() 绑定业务回调
 * @epfd: 框架自持的 epoll 描述符，-1 表示未创建
 * @sigfd: 框架自持的 signalfd 描述符，-1 表示未创建
 * @wfd: 框架自持的唤醒 eventfd，-1 表示未创建
 * @next_tick: 下一次 tick 的单调毫秒截止点
 * @last_beat: 最近一次心跳的单调毫秒时刻，0 表示尚未上报；原子量，
 *             跨线程刷新与读取
 * @old_mask: 运行前的线程信号掩码，仅 sig_saved 为真时有效
 * @sig_saved: 是否已接管信号掩码
 * @ops: 业务回调副本
 * @evts: 固定槽位事件表
 * @nr_evt: 已占用槽位数
 */
struct airy_dmn {
	char name[AIRY_DMN_NAME_MAX];
	char config[PATH_MAX];
	unsigned int tick_ms;
	unsigned int beat_ms;
	unsigned int miss_max;
	unsigned int flags;
	enum airy_dmn_state state;
	bool bound;
	int epfd;
	int sigfd;
	int wfd;
	uint64_t next_tick;
	_Atomic uint64_t last_beat;
	sigset_t old_mask;
	bool sig_saved;
	struct airy_dmn_ops ops;
	struct airy_dmn_evt evts[AIRY_DMN_MAX_SRC];
	unsigned int nr_evt;
};

/*
 * 事件源内部操作：airy_dmn_attach() 占用槽位并以槽位下标作为 epoll
 * data.u32，返回槽位；airy_dmn_detach() 释放槽位，外部已关闭的描述符
 * 不视为错误。airy_dmn_ep() 惰性创建 epoll，airy_dmn_mask() 与
 * airy_dmn_rev() 负责两向掩码翻译，airy_dmn_err() 收敛 errno。
 * airy_dmn_find() 按描述符查槽位，fd 与 IPC 注册共用，保证同一
 * 描述符不会占用两个槽位。以上均由各实现分片共用，避免多份实现。
 */
uint32_t airy_dmn_mask(unsigned int events);
unsigned int airy_dmn_rev(uint32_t mask);
int airy_dmn_ep(struct airy_dmn *dmn);
int airy_dmn_err(int err);
int airy_dmn_find(const struct airy_dmn *dmn, int fd);
int airy_dmn_attach(struct airy_dmn *dmn, int fd, uint32_t mask,
		    enum airy_dmn_kind kind);
int airy_dmn_detach(struct airy_dmn *dmn, int slot);

/*
 * IPC 通道内部操作：airy_dmn_ipcrecv() 从已注册通道读取一条完整
 * 消息帧，校验 Layout C 头后回调。帧过短、magic 或 payload_len 不
 * 符、摘要失配、对端关闭均返回负值；主循环据此终止，避免在失序
 * 通道上继续解释字节流。airy_dmn_crc() 按 C-S12 计算 header[0:52)
 * 与 payload 的摘要，发送与接收共用同一实现。
 */
int airy_dmn_ipcrecv(struct airy_dmn *dmn, struct airy_dmn_evt *evt);
uint32_t airy_dmn_crc(const struct airy_ipc_msg_hdr *hdr,
		      const void *payload);

/*
 * 信号收口内部操作：airy_dmn_sigarm() 阻塞 SIGTERM/SIGINT/SIGHUP 并
 * 创建 signalfd 注册到同一 epoll；airy_dmn_sigev() 排空已就绪信号并
 * 分派；airy_dmn_sigoff() 恢复运行前的线程信号掩码。三者在主循环
 * 分片中成对使用。
 */
int airy_dmn_sigarm(struct airy_dmn *dmn);
int airy_dmn_sigev(struct airy_dmn *dmn);
void airy_dmn_sigoff(struct airy_dmn *dmn);

#endif /* _AIRY_DMN_INT_H */
