// SPDX-License-Identifier: AGPL-3.0-or-later OR Apache-2.0
/*
 * Copyright (c) 2025-2026 SPHARX Ltd. All Rights Reserved.
 *
 * dmn_ipc.c — daemon 框架 IPC 通道注册与整帧分派
 *
 * 通道限定为 SOCK_SEQPACKET/SOCK_DGRAM，保留消息边界。读取先用
 * MSG_PEEK|MSG_TRUNC 取整帧长度，再一次性读入，避免分步读取在数据
 * 报套接字上丢弃剩余字节。消息头按 Layout C 校验 magic、保留标志位、
 * payload_len 与 C-S12 摘要后回调；非法帧与对端关闭均返回负值，由
 * 主循环终止，不做字节流重同步。发送以 128 字节头加消息体单次写出
 * 一条记录，摘要由本层盖写，调用方头副本不被修改。
 *
 * IRON-9 v3: [IND] 独立实现层 — agent-linux services 子仓内部。
 *
 * SSoT: docs-linux/20-modules/02-services.md §3.7
 */

#define _GNU_SOURCE

#include <errno.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <sys/epoll.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <sys/uio.h>

#include "dmn_int.h"

static ssize_t dmn_retry(int fd, void *buf, size_t size, int flags)
{
	ssize_t ret;

	do {
		ret = recv(fd, buf, size, flags);
	} while (ret < 0 && errno == EINTR);

	return ret;
}

static int dmn_type(int fd)
{
	socklen_t len = sizeof(int);
	int type;

	if (getsockopt(fd, SOL_SOCKET, SO_TYPE, &type, &len))
		return -AIRY_EINVAL;
	if (type == SOCK_SEQPACKET || type == SOCK_DGRAM)
		return 0;

	return -AIRY_EINVAL;
}

int airy_dmn_ipc(struct airy_dmn *dmn, int fd, airy_dmn_msg msg, void *data)
{
	struct airy_dmn_evt *evt;
	int ret;
	int slot;

	if (!dmn || fd < 0 || !msg)
		return -AIRY_EINVAL;
	if (dmn->state != AIRY_DMN_INIT)
		return -AIRY_EBUSY;

	ret = dmn_type(fd);
	if (ret)
		return ret;
	if (airy_dmn_find(dmn, fd) >= 0)
		return -AIRY_EEXIST;

	slot = airy_dmn_attach(dmn, fd, EPOLLIN, AIRY_DMN_KIND_IPC);
	if (slot < 0)
		return slot;

	evt = &dmn->evts[slot];
	evt->events = AIRY_DMN_IN;
	evt->cb.ipc.msg = msg;
	evt->cb.ipc.data = data;

	return 0;
}

/* reserved[72] 按 C-S4 必须全零，发送与接收共用本检查。 */
static int dmn_zchk(const struct airy_ipc_msg_hdr *hdr)
{
	size_t i;

	for (i = 0; i < sizeof(hdr->reserved); i++)
		if (hdr->reserved[i])
			return -AIRY_EIPC_RESERVED;

	return 0;
}

/* 校验待发送头的静态约束。 */
static int dmn_sndchk(const struct airy_ipc_msg_hdr *hdr, const void *payload)
{
	int ret;

	if (!hdr)
		return -AIRY_EINVAL;
	if (hdr->magic != AIRY_IPC_MAGIC)
		return -AIRY_EINVAL;
	if (hdr->flags & AIRY_IPC_FLAG_RESERVED)
		return -AIRY_EINVAL;
	if (hdr->payload_len > AIRY_IPC_MAX_PAYLOAD)
		return -AIRY_EINVAL;
	if (hdr->payload_len && !payload)
		return -AIRY_EINVAL;

	ret = dmn_zchk(hdr);

	return ret ? -AIRY_EINVAL : 0;
}

/* 确认描述符已注册为 IPC 通道，避免向普通 fd 事件源误发消息。 */
static int dmn_sndfd(const struct airy_dmn *dmn, int fd)
{
	int slot;

	slot = airy_dmn_find(dmn, fd);
	if (slot < 0)
		return slot;

	return dmn->evts[slot].kind == AIRY_DMN_KIND_IPC ? 0 : -AIRY_EINVAL;
}

static int dmn_snderr(int err)
{
	if (err == EAGAIN || err == EWOULDBLOCK)
		return -AIRY_EAGAIN;

	return airy_dmn_err(err);
}

/* 单次写出一条记录；EINTR 重试，部分写入视为通道失序。 */
static int dmn_wrmsg(int fd, const struct msghdr *msg, size_t total)
{
	ssize_t ret;

	do {
		ret = sendmsg(fd, msg, MSG_NOSIGNAL);
	} while (ret < 0 && errno == EINTR);

	if (ret < 0)
		return dmn_snderr(errno);

	return (size_t)ret == total ? 0 : -AIRY_EIO;
}

int airy_dmn_send(struct airy_dmn *dmn, int fd,
		  const struct airy_ipc_msg_hdr *hdr, const void *payload)
{
	struct airy_ipc_msg_hdr wire;
	struct iovec iov[2];
	struct msghdr msg = { 0 };
	int ret;

	if (!dmn || fd < 0)
		return -AIRY_EINVAL;

	ret = dmn_sndchk(hdr, payload);
	if (ret)
		return ret;

	ret = dmn_sndfd(dmn, fd);
	if (ret)
		return ret;

	wire = *hdr;
	wire.crc32 = airy_dmn_crc(&wire, payload);

	iov[0].iov_base = &wire;
	iov[0].iov_len = AIRY_IPC_HDR_SIZE;
	iov[1].iov_base = (void *)payload;
	iov[1].iov_len = hdr->payload_len;
	msg.msg_iov = iov;
	msg.msg_iovlen = 2;

	return dmn_wrmsg(fd, &msg, AIRY_IPC_HDR_SIZE + hdr->payload_len);
}

static ssize_t dmn_peek(int fd)
{
	uint8_t byte;

	return dmn_retry(fd, &byte, sizeof(byte), MSG_PEEK | MSG_TRUNC);
}

/*
 * 取一条完整消息帧。返回 1 表示当前无帧可读（非阻塞描述符），调用
 * 方直接返回成功；返回 0 时 *frame 已分配、*len 为整帧长度。
 */
static int dmn_frame(int fd, uint8_t **frame, ssize_t *len)
{
	ssize_t size;

	*frame = NULL;
	*len = 0;

	size = dmn_peek(fd);
	if (size < 0) {
		if (errno == EAGAIN || errno == EWOULDBLOCK)
			return 1;

		return -AIRY_EIO;
	}
	if (size < AIRY_IPC_HDR_SIZE)
		return -AIRY_EIPC_HDRSIZE;
	if (size > AIRY_IPC_HDR_SIZE + (ssize_t)AIRY_IPC_MAX_PAYLOAD)
		return -AIRY_EIPC_PAYLOAD;

	*frame = malloc((size_t)size);
	if (!*frame)
		return -AIRY_ENOMEM;

	*len = size;

	return 0;
}

/* 返回 1 表示暂不可读，调用方丢弃本帧缓冲并等待下一次唤醒。 */
static int dmn_fill(int fd, uint8_t *frame, ssize_t size)
{
	ssize_t ret;

	ret = dmn_retry(fd, frame, (size_t)size, 0);
	if (ret == size)
		return 0;
	if (ret < 0 && (errno == EAGAIN || errno == EWOULDBLOCK))
		return 1;

	return -AIRY_EIO;
}

static int dmn_hdrchk(const struct airy_ipc_msg_hdr *hdr, ssize_t size)
{
	if (hdr->magic != AIRY_IPC_MAGIC)
		return -AIRY_EIPC_MAGIC;
	if (hdr->flags & AIRY_IPC_FLAG_RESERVED)
		return -AIRY_EIPC_FLAGS;
	if (hdr->payload_len != (uint32_t)(size - AIRY_IPC_HDR_SIZE))
		return -AIRY_EIPC_PAYLOAD;

	return dmn_zchk(hdr);
}

static int dmn_crcchk(const struct airy_ipc_msg_hdr *hdr, const void *payload)
{
	if (airy_dmn_crc(hdr, payload) != hdr->crc32)
		return -AIRY_EIPC_CRC32;

	return 0;
}

int airy_dmn_ipcrecv(struct airy_dmn *dmn, struct airy_dmn_evt *evt)
{
	struct airy_ipc_msg_hdr hdr;
	const void *payload = NULL;
	uint8_t *frame = NULL;
	ssize_t size = 0;
	int ret;

	ret = dmn_frame(evt->fd, &frame, &size);
	if (ret)
		return ret > 0 ? 0 : ret;

	ret = dmn_fill(evt->fd, frame, size);
	if (ret) {
		free(frame);
		return ret > 0 ? 0 : ret;
	}

	memcpy(&hdr, frame, sizeof(hdr));
	ret = dmn_hdrchk(&hdr, size);
	if (ret)
		goto out;

	if (hdr.payload_len)
		payload = frame + AIRY_IPC_HDR_SIZE;

	ret = dmn_crcchk(&hdr, payload);
	if (ret)
		goto out;

	ret = evt->cb.ipc.msg(dmn, &hdr, payload, evt->cb.ipc.data);
out:
	free(frame);

	return ret;
}
