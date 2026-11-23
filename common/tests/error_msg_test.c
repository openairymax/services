/* SPDX-License-Identifier: AGPL-3.0-or-later OR Apache-2.0 */
/*
 * Copyright (c) 2025-2026 SPHARX Ltd. All Rights Reserved.
 *
 * error_msg_test.c — 错误消息解析的单元判据（TAP 13）
 *
 * 固定到 C locale，断言的是「未装目录时的英文源文回退」这条确定路径：
 * 若宿主装了 airymaxos 目录，翻译会先被命中，测试不再可判定。
 */
#include <locale.h>
#include <stdio.h>
#include <string.h>

#include <linux/airymax/error.h>

#include "airy/error_msg.h"

static int g_done;
static int g_fail;

static void tap_ok(int ok, const char *name)
{
	g_done++;
	if (ok) {
		printf("ok %d - %s\n", g_done, name);
	} else {
		printf("not ok %d - %s\n", g_done, name);
		g_fail++;
	}
}

int main(void)
{
	const char *msg;

	setvbuf(stdout, NULL, _IONBF, 0);
	setlocale(LC_ALL, "C");

	tap_ok(strcmp(airy_strerror(AIRY_EOK), "Success") == 0,
	       "EOK maps to success text");
	tap_ok(strcmp(airy_strerror(AIRY_EINVAL), "Invalid argument") == 0,
	       "POSIX code falls back to header text");
	tap_ok(strcmp(airy_strerror(-AIRY_EINVAL),
		      airy_strerror(AIRY_EINVAL)) == 0,
	       "negative value resolves like its magnitude");
	tap_ok(strstr(airy_strerror(AIRY_EIPC_FROZEN), "Ring frozen") != NULL,
	       "IPC code keeps its contract description");
	tap_ok(strstr(airy_strerror(-AIRY_ELOG_FULL), "Ring Buffer full") != NULL,
	       "log code resolves through negation");
	tap_ok(strstr(airy_strerror(AIRY_FAULT_RING_CORRUPT), "ring") != NULL,
	       "fault code shares the same table");
	tap_ok(strcmp(airy_strerror(424242), "Unknown error") == 0,
	       "unknown code yields translatable placeholder");
	tap_ok(strcmp(airy_strerror(-424242), "Unknown error") == 0,
	       "unknown negative code yields placeholder");

	msg = airy_strerror(AIRY_ELOCKDOWN);
	tap_ok(msg != NULL && msg[0] != '\0', "lockdown message is non-empty");

	printf("1..%d\n", g_done);
	if (g_fail)
		printf("# %d of %d checks failed\n", g_fail, g_done);

	return g_fail ? 1 : 0;
}
