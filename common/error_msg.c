/* SPDX-License-Identifier: AGPL-3.0-or-later OR Apache-2.0 */
/*
 * Copyright (c) 2025-2026 SPHARX Ltd. All Rights Reserved.
 *
 * error_msg.c — 错误码到本地化消息的解析
 *
 * 数据流：错误码 → 消息标识（符号名）→ gettext 域 airymaxos → 英文
 * 源文回退。未装消息目录时 dgettext 原样返回 msgid，此时改用生成的
 * 英文说明，保证裁剪镜像里也不会只剩一个数字。
 *
 * SSoT: kernel/include/uapi/linux/airymax/error.h
 */
#include <libintl.h>

#include "airy/error_msg.h"
#include "error_msg_int.h"

const char *airy_strerror(int code)
{
	const char *msgid;
	const char *text;
	unsigned int mag;

	/* INT_MIN 取反会溢出，先扩到 long 再转幅值。 */
	if (code < 0)
		mag = (unsigned int)(-(long)code);
	else
		mag = (unsigned int)code;

	msgid = airy_err_msgid(mag);
	if (msgid == NULL)
		return dgettext(AIRY_GETTEXT_DOMAIN, "Unknown error");

	text = dgettext(AIRY_GETTEXT_DOMAIN, msgid);
	if (text != NULL && text != msgid)
		return text;

	text = airy_err_text(mag);

	return text != NULL ? text : msgid;
}
