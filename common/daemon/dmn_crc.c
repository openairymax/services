// SPDX-License-Identifier: AGPL-3.0-or-later OR Apache-2.0
/*
 * Copyright (c) 2025-2026 SPHARX Ltd. All Rights Reserved.
 *
 * dmn_crc.c — IPC 消息完整性摘要
 *
 * 按 C-S12 计算 crc32，覆盖 header[0:52) 与 payload，不覆盖 crc32
 * 字段本身与 reserved[72]。算法与内核 airy_crc32_calc() 一致：zlib
 * 的 crc32(0, buf, len) 等价于内核 crc32_le(~0U, ...) ^ ~0U，发送
 * 与接收两侧共用本实现，避免两套摘要逻辑漂移。
 *
 * IRON-9 v3: [IND] 独立实现层 — agent-linux services 子仓内部。
 *
 * SSoT: docs-linux/30-interfaces/07-ipc-fastpath.md §3.1 (C-S12)
 */

#define _GNU_SOURCE

#include <stddef.h>
#include <stdint.h>

#include <zlib.h>

#include <linux/airymax/ipc.h>

#include "dmn_int.h"

uint32_t airy_dmn_crc(const struct airy_ipc_msg_hdr *hdr, const void *payload)
{
	uLong crc;

	crc = crc32(0L, Z_NULL, 0);
	crc = crc32(crc, (const Bytef *)hdr,
		    (uInt)offsetof(struct airy_ipc_msg_hdr, crc32));
	if (hdr->payload_len)
		crc = crc32(crc, (const Bytef *)payload,
			    (uInt)hdr->payload_len);

	return (uint32_t)crc;
}
