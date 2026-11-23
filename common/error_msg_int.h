/* SPDX-License-Identifier: AGPL-3.0-or-later OR Apache-2.0 */
/*
 * Copyright (c) 2025-2026 SPHARX Ltd. All Rights Reserved.
 *
 * error_msg_int.h — 生成映射表的内部访问接口
 *
 * 仅供 error_msg.c 与生成的 error_msg_table.c 使用；外部调用方只依赖
 * airy/error_msg.h，不绑定到数值表这一实现细节。
 */
#ifndef _AIRY_ERROR_MSG_INT_H
#define _AIRY_ERROR_MSG_INT_H

/* 返回错误码对应的消息标识（符号名）；未知返回 NULL。 */
const char *airy_err_msgid(unsigned int code);

/* 返回契约头里的英文说明；未知返回 NULL。 */
const char *airy_err_text(unsigned int code);

#endif /* _AIRY_ERROR_MSG_INT_H */
