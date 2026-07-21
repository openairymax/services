// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2025-2026 SPHARX Ltd. All Rights Reserved.
 *
 * macro_d.c — Macro-Supervisor 用户态裁决主循环
 *
 * 职责：
 *   接收内核 Micro-Supervisor 通过 eventfd 上报的故障信号，
 *   读取 [SC] error.h 中定义的故障码，执行宏观裁决
 *   （警告 / 降级 / 暂停 / 终止），将裁决结果打印到 stderr。
 *
 * [IND] 完全独立层（agentrt-linux 专属实现，决策 F1 真实可编译骨架）。
 *
 * 相关文档：
 *   docs/AirymaxOS/10-architecture/07-directory-structure.md §4.2
 *   kernel/include/uapi/linux/airymax/error.h
 */

#include <errno.h>
#include <fcntl.h>
#include <inttypes.h>
#include <poll.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/epoll.h>
#include <sys/eventfd.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/uio.h>
#include <time.h>
#include <unistd.h>

/* [SC] 共享契约头文件（通过 -I../kernel/include/uapi/linux 引用） */
#include <airymax/error.h>

/* ─── 常量 ──────────────────────────────────────────────────────────── */

#define MACRO_D_NAME		"macro_d"
#define MACRO_D_VERSION		"0.1.1"
#define MACRO_D_EPOLLEVS	16
#define MACRO_D_FAULTBUF	64
#define MACRO_D_STATUSPATH	"/proc/airy/status"

/* 裁决动作枚举（与 kernel superv 模型对齐） */
enum macro_verdict {
	MACRO_VERDICT_WARN	= 0,	/* 警告：记录日志，不干预 */
	MACRO_VERDICT_DEGRADE	= 1,	/* 降级：限制资源 */
	MACRO_VERDICT_PAUSE	= 2,	/* 暂停：冻结 agent */
	MACRO_VERDICT_TERMINATE	= 3,	/* 终止：杀掉 agent */
	MACRO_VERDICT_MAX
};

/* ─── 辅助函数 ──────────────────────────────────────────────────────── */

static const char *macro_fault_name(uint32_t fault)
{
	switch (fault) {
	case AIRY_FAULT_CAP_FORGED:
		return "CAP_FORGED";
	case AIRY_FAULT_CAP_LEAK:
		return "CAP_LEAK";
	case AIRY_FAULT_RING_CORRUPT:
		return "RING_CORRUPT";
	case AIRY_FAULT_TIMEOUT:
		return "TIMEOUT";
	case AIRY_FAULT_ABNORMAL_CAP:
		return "ABNORMAL_CAP";
	case AIRY_FAULT_VM_FAULT:
		return "VM_FAULT";
	default:
		return "UNKNOWN";
	}
}

static const char *macro_verdict_name(enum macro_verdict v)
{
	switch (v) {
	case MACRO_VERDICT_WARN:
		return "WARN";
	case MACRO_VERDICT_DEGRADE:
		return "DEGRADE";
	case MACRO_VERDICT_PAUSE:
		return "PAUSE";
	case MACRO_VERDICT_TERMINATE:
		return "TERMINATE";
	default:
		return "UNKNOWN";
	}
}

static void macro_log(const char *level, const char *fmt, ...)
{
	va_list ap;
	struct timespec ts;

	clock_gettime(CLOCK_REALTIME, &ts);
	fprintf(stderr, "[%s] [%lld.%09ld] " MACRO_D_NAME ": ",
		level, (long long)ts.tv_sec, ts.tv_nsec);
	va_start(ap, fmt);
	vfprintf(stderr, fmt, ap);
	va_end(ap);
	fputc('\n', stderr);
}

/* 读取 eventfd 计数器，返回读取的字节数（0 表示错误） */
static ssize_t macro_eventfd_read(int fd, uint64_t *value)
{
	ssize_t n = read(fd, value, sizeof(*value));
	if (n != sizeof(*value)) {
		macro_log("ERROR", "eventfd read 失败: n=%zd errno=%d (%s)",
			  n, errno, strerror(errno));
		return 0;
	}
	return n;
}

/* 从状态文件读取故障码（0.1.1 阶段使用 /proc/airy/status） */
static int macro_read_fault_code(uint32_t *fault_code)
{
	FILE *fp;
	char line[MACRO_D_FAULTBUF];

	fp = fopen(MACRO_D_STATUSPATH, "r");
	if (!fp) {
		/* 状态文件不存在时，使用事件计数器作为伪故障码 */
		macro_log("WARN", "无法打开 %s: %s，使用默认故障码",
			  MACRO_D_STATUSPATH, strerror(errno));
		*fault_code = AIRY_FAULT_TIMEOUT;
		return 0;
	}

	if (fgets(line, sizeof(line), fp) == NULL) {
		fclose(fp);
		macro_log("WARN", "%s 为空，使用默认故障码",
			  MACRO_D_STATUSPATH);
		*fault_code = AIRY_FAULT_TIMEOUT;
		return 0;
	}
	fclose(fp);

	/* 解析故障码（16 进制） */
	*fault_code = (uint32_t)strtoul(line, NULL, 16);
	return 0;
}

/*
 * macro_adjudicate — 宏观裁决逻辑
 *
 * 根据故障码选择裁决动作：
 *   CAP_FORGED / CAP_LEAK    → TERMINATE（安全违规，立即终止）
 *   RING_CORRUPT             → PAUSE（通信故障，暂停等待恢复）
 *   TIMEOUT                  → DEGRADE（超时，降级运行）
 *   ABNORMAL_CAP / VM_FAULT  → WARN（异常，警告记录）
 *   其他                      → WARN
 */
static enum macro_verdict macro_adjudicate(uint32_t fault_code)
{
	switch (fault_code) {
	case AIRY_FAULT_CAP_FORGED:
	case AIRY_FAULT_CAP_LEAK:
		return MACRO_VERDICT_TERMINATE;
	case AIRY_FAULT_RING_CORRUPT:
		return MACRO_VERDICT_PAUSE;
	case AIRY_FAULT_TIMEOUT:
		return MACRO_VERDICT_DEGRADE;
	case AIRY_FAULT_ABNORMAL_CAP:
	case AIRY_FAULT_VM_FAULT:
	default:
		return MACRO_VERDICT_WARN;
	}
}

/* 执行裁决动作（0.1.1 阶段仅打印到 stderr） */
static void macro_execute(enum macro_verdict verdict, uint32_t fault_code)
{
	const char *action = macro_verdict_name(verdict);
	const char *fault = macro_fault_name(fault_code);

	switch (verdict) {
	case MACRO_VERDICT_TERMINATE:
		macro_log("CRIT", "裁决=TERMINATE 故障=%s(0x%04x) — 安全违规，"
			  "建议立即终止 agent", fault, fault_code);
		break;
	case MACRO_VERDICT_PAUSE:
		macro_log("ERROR", "裁决=PAUSE 故障=%s(0x%04x) — 通信故障，"
			  "建议冻结 agent 等待恢复", fault, fault_code);
		break;
	case MACRO_VERDICT_DEGRADE:
		macro_log("WARN", "裁决=DEGRADE 故障=%s(0x%04x) — 超时，"
			  "建议降级运行（限制资源）", fault, fault_code);
		break;
	case MACRO_VERDICT_WARN:
	default:
		macro_log("INFO", "裁决=WARN 故障=%s(0x%04x) — 异常，"
			  "记录日志继续运行", fault, fault_code);
		break;
	}
}

/* ─── 主循环 ────────────────────────────────────────────────────────── */

static int macro_main_loop(int efd)
{
	int epfd;
	struct epoll_event ev;
	struct epoll_event events[MACRO_D_EPOLLEVS];
	int running = 1;
	uint64_t event_count = 0;

	/* 创建 epoll 实例 */
	epfd = epoll_create1(EPOLL_CLOEXEC);
	if (epfd < 0) {
		macro_log("ERROR", "epoll_create1 失败: %s",
			  strerror(errno));
		return -1;
	}

	/* 注册 eventfd 到 epoll */
	memset(&ev, 0, sizeof(ev));
	ev.events = EPOLLIN;
	ev.data.fd = efd;
	if (epoll_ctl(epfd, EPOLL_CTL_ADD, efd, &ev) < 0) {
		macro_log("ERROR", "epoll_ctl ADD 失败: %s",
			  strerror(errno));
		close(epfd);
		return -1;
	}

	macro_log("INFO", MACRO_D_NAME " v" MACRO_D_VERSION
		  " 启动，监听 eventfd=%d", efd);

	while (running) {
		int nfds = epoll_wait(epfd, events, MACRO_D_EPOLLEVS, -1);
		if (nfds < 0) {
			if (errno == EINTR) {
				macro_log("INFO", "epoll_wait 被信号中断，"
					  "继续运行");
				continue;
			}
			macro_log("ERROR", "epoll_wait 失败: %s",
				  strerror(errno));
			break;
		}

		for (int i = 0; i < nfds; i++) {
			if (events[i].data.fd != efd) {
				continue;
			}

			uint64_t val;
			if (!macro_eventfd_read(efd, &val)) {
				continue;
			}
			event_count += val;

			uint32_t fault_code;
			if (macro_read_fault_code(&fault_code) < 0) {
				continue;
			}

			enum macro_verdict v = macro_adjudicate(fault_code);
			macro_execute(v, fault_code);

			macro_log("INFO", "累计事件=%" PRIu64
				  " 本次故障码=0x%04x", event_count,
				  fault_code);
		}
	}

	close(epfd);
	macro_log("INFO", MACRO_D_NAME " 退出，累计事件=%" PRIu64,
		  event_count);
	return 0;
}

/* ─── 使用说明 ──────────────────────────────────────────────────────── */

static void usage(FILE *out, const char *prog)
{
	fprintf(out,
		"用法: %s [选项] <eventfd>\n"
		"\n"
		"Macro-Supervisor 用户态裁决守护进程。\n"
		"\n"
		"参数:\n"
		"  <eventfd>       内核 Micro-Supervisor 创建的 eventfd 编号\n"
		"\n"
		"选项:\n"
		"  -h, --help      显示帮助\n"
		"  -v, --version   显示版本\n"
		"\n"
		"示例:\n"
		"  %s 7            # 监听 fd=7 的 eventfd\n",
		prog, prog);
}

/* ─── main 入口 ─────────────────────────────────────────────────────── */

int main(int argc, char **argv)
{
	int efd;
	const char *prog = argv[0] ? argv[0] : MACRO_D_NAME;

	/* 解析命令行参数 */
	for (int i = 1; i < argc; i++) {
		if (strcmp(argv[i], "-h") == 0 ||
		    strcmp(argv[i], "--help") == 0) {
			usage(stdout, prog);
			return 0;
		}
		if (strcmp(argv[i], "-v") == 0 ||
		    strcmp(argv[i], "--version") == 0) {
			printf(MACRO_D_NAME " v" MACRO_D_VERSION
			       " — AirymaxOS Macro-Supervisor\n");
			return 0;
		}
	}

	if (argc < 2) {
		usage(stderr, prog);
		return EXIT_FAILURE;
	}

	/* 解析 eventfd 编号 */
	errno = 0;
	long val = strtol(argv[1], NULL, 10);
	if (errno != 0 || val < 0 || val > INT_MAX) {
		fprintf(stderr, "%s: 无效的 eventfd 编号: %s\n", prog,
			argv[1]);
		return EXIT_FAILURE;
	}
	efd = (int)val;

	/* 验证 fd 可读 */
	if (fcntl(efd, F_GETFD) < 0) {
		fprintf(stderr, "%s: eventfd %d 无效: %s\n", prog, efd,
			strerror(errno));
		return EXIT_FAILURE;
	}

	/* 进入主循环 */
	return macro_main_loop(efd) == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
