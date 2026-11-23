/* SPDX-License-Identifier: AGPL-3.0-or-later OR Apache-2.0 */
/*
 * Copyright (c) 2025-2026 SPHARX Ltd. All Rights Reserved.
 *
 * airy/error_msg.h — 错误码的本地化消息
 *
 * 错误码是机器契约，消息是人类呈现，二者分离：调用方只处理
 * airy_err_t，翻译随 locale 切换，不改变返回码语义。
 *
 * 消息目录的键是 [SC] 错误码符号名（如 AIRY_EINVAL），翻译文件位于
 * services/po/，安装后由 gettext 从域 airymaxos 查找。未装目录时回退
 * 到契约头里的英文说明，因此任何部署形态都能拿到可读文本。
 *
 * SSoT: kernel/include/uapi/linux/airymax/error.h
 *       docs-linux/180-i18n/03-error-message-i18n.md
 */
#ifndef _AIRY_ERROR_MSG_H
#define _AIRY_ERROR_MSG_H

/* gettext 域；消息目录安装为 <locale>/LC_MESSAGES/airymaxos.mo */
#define AIRY_GETTEXT_DOMAIN "airymaxos"

/**
 * airy_strerror - 取错误码的本地化消息
 * @code: 正数幅值（AIRY_E*）或调用方返回的负值（-AIRY_E*）
 *
 * 未知错误码返回可翻译的 "Unknown error"。返回值为静态存储，调用方
 * 不得释放。
 */
const char *airy_strerror(int code);

#endif /* _AIRY_ERROR_MSG_H */
