/* SPDX-License-Identifier: AGPL-3.0-or-later OR Apache-2.0 */
/*
 * Copyright (c) 2025-2026 SPHARX Ltd. All Rights Reserved.
 *
 * uevent_test.c — user_events 生产端封装的单元判据（TAP 13）
 *
 * 注册需要真实 tracefs，用例只在能控制 fd 的路径上验证行为：使能位
 * 未打开时不写、打开时按「写索引 + 载荷」布局写出、关闭后句柄归零。
 * 这两种路径决定了空消费者成本与提交格式，是内核侧唯一能静态对齐的
 * 部分。
 */
#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#include "airy/uevent.h"

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

static void t_open_args(void)
{
	struct airy_uev ev;
	char name[600];

	tap_ok(airy_uev_open(NULL, "x", "u32 a") == -EINVAL,
	       "open rejects null handle");
	tap_ok(airy_uev_open(&ev, NULL, "u32 a") == -EINVAL,
	       "open rejects null name");
	tap_ok(airy_uev_open(&ev, "x", NULL) == -EINVAL,
	       "open rejects null args");

	memset(name, 'a', sizeof(name) - 1);
	name[sizeof(name) - 1] = '\0';
	tap_ok(airy_uev_open(&ev, name, "u32 a") == -E2BIG,
	       "open rejects overlong description");
}

static void t_emit_off(void)
{
	struct airy_uev ev = { .fd = -1, .index = 7, .enabled = 0 };
	__u32 rec = 1;

	tap_ok(!airy_uev_on(&ev), "disabled handle reports off");
	tap_ok(airy_uev_emit(&ev, &rec, sizeof(rec)) == 0,
	       "disabled event skips write");
}

static void t_emit_on(void)
{
	struct airy_uev ev = { .fd = -1, .index = 0x11223344U, .enabled = 1 };
	__u16 rec[2] = { 5, 9 };
	__u32 index = 0x11223344U;
	char buf[16];
	char want[16];
	int p[2];
	ssize_t n;

	if (pipe(p) != 0) {
		tap_ok(0, "pipe for record layout");
		return;
	}
	ev.fd = p[1];
	tap_ok(airy_uev_on(&ev), "enabled handle reports on");
	tap_ok(airy_uev_emit(&ev, rec, sizeof(rec)) == 0,
	       "enabled event writes record");

	n = read(p[0], buf, sizeof(buf));
	memcpy(want, &index, sizeof(index));
	memcpy(want + sizeof(index), rec, sizeof(rec));
	tap_ok(n == (ssize_t)(sizeof(index) + sizeof(rec)) &&
	       memcmp(buf, want, (size_t)n) == 0,
	       "record is index followed by payload");
	close(p[0]);
	close(p[1]);
}

static void t_close(void)
{
	struct airy_uev ev = { .fd = -1, .index = 3, .enabled = 1 };
	char c;
	int p[2];

	if (pipe(p) != 0) {
		tap_ok(0, "pipe for close path");
		return;
	}
	ev.fd = p[1];
	airy_uev_close(&ev);
	tap_ok(ev.fd == -1 && ev.enabled == 0,
	       "close clears the handle");
	tap_ok(read(p[0], &c, 1) == 0, "close releases the descriptor");
	close(p[0]);
}

int main(void)
{
	setvbuf(stdout, NULL, _IONBF, 0);

	t_open_args();
	t_emit_off();
	t_emit_on();
	t_close();

	printf("1..%d\n", g_done);
	if (g_fail)
		printf("# %d of %d checks failed\n", g_fail, g_done);

	return g_fail ? 1 : 0;
}
