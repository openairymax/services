// SPDX-License-Identifier: AGPL-3.0-or-later OR Apache-2.0
/*
 * Copyright (c) 2025-2026 SPHARX Ltd. All Rights Reserved.
 *
 * dmn_test.c — services daemon 框架 CTest 单元测试
 *
 * 白盒覆盖 <airy/daemon.h> 公共 API 与内部错误路径：对象生命周期、
 * 事件源表、IPC 整帧收发与批量提交、心跳记账、周期 tick、systemd
 * 上报与信号收口。用例以真实管道与套接字驱动，不做桩替代；结果以
 * TAP 13 输出，由 CTest 声明式注册调度。
 *
 * 用户态无 KUnit，本仓等价物为 CTest，对应任务台账 D2 测试挂点。
 *
 * SSoT: docs-linux/20-modules/02-services.md §3.7
 *       docs-linux/80-testing/02-kselftest.md §9.2
 */
#define _GNU_SOURCE

#include <errno.h>
#include <fcntl.h>
#include <pthread.h>
#include <signal.h>
#include <stdatomic.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <time.h>
#include <unistd.h>

#include <zlib.h>

#include "../daemon/dmn_int.h"

static int g_done;
static int g_fail;

static void tap_ok(bool ok, const char *name)
{
	g_done++;
	if (ok) {
		printf("ok %d - %s\n", g_done, name);
		return;
	}

	g_fail++;
	printf("not ok %d - %s\n", g_done, name);
}

static void nap_ms(unsigned int ms)
{
	struct timespec ts = {
		.tv_sec = ms / 1000,
		.tv_nsec = (long)(ms % 1000) * 1000000L,
	};

	(void)nanosleep(&ts, NULL);
}

static bool wait_state(struct airy_dmn *dmn, int want, int ms)
{
	int i;

	for (i = 0; i < ms; i++) {
		if ((int)airy_dmn_state(dmn) == want)
			return true;
		nap_ms(1);
	}

	return false;
}

static bool wait_bool(_Atomic bool *flag, int ms)
{
	int i;

	for (i = 0; i < ms; i++) {
		if (atomic_load(flag))
			return true;
		nap_ms(1);
	}

	return false;
}

static bool wait_cnt(_Atomic int *cnt, int at_least, int ms)
{
	int i;

	for (i = 0; i < ms; i++) {
		if (atomic_load(cnt) >= at_least)
			return true;
		nap_ms(1);
	}

	return false;
}

static void cfg_init(struct airy_dmn_cfg *cfg, const char *name)
{
	memset(cfg, 0, sizeof(*cfg));
	cfg->name = name;
}

static void mk_pair(int type, int sv[2])
{
	sv[0] = -1;
	sv[1] = -1;
	if (socketpair(AF_UNIX, type, 0, sv))
		sv[0] = -1;
}

/* ── 回调替身：仅记录触达，不做业务裁决 ─────────────────────────────── */

static int ev_noop(struct airy_dmn *dmn, int fd, unsigned int events,
		   void *data)
{
	(void)dmn;
	(void)fd;
	(void)events;
	(void)data;

	return 0;
}

static int msg_noop(struct airy_dmn *dmn, const struct airy_ipc_msg_hdr *hdr,
		    const void *payload, void *data)
{
	(void)dmn;
	(void)hdr;
	(void)payload;
	(void)data;

	return 0;
}

/* ── T1 对象生命周期与入参校验 ──────────────────────────────────────── */

static void t_new(void)
{
	struct airy_dmn_cfg cfg;
	struct airy_dmn *dmn;
	char name[64];
	char big[4200];

	errno = 0;
	tap_ok(airy_dmn_new(NULL) == NULL && errno == EINVAL,
	       "new rejects NULL cfg");

	memset(&cfg, 0, sizeof(cfg));
	errno = 0;
	tap_ok(airy_dmn_new(&cfg) == NULL && errno == EINVAL,
	       "new rejects missing name");

	cfg.name = "";
	errno = 0;
	tap_ok(airy_dmn_new(&cfg) == NULL && errno == EINVAL,
	       "new rejects empty name");

	memset(name, 'n', sizeof(name));
	name[sizeof(name) - 1] = '\0';
	cfg.name = name;
	errno = 0;
	tap_ok(airy_dmn_new(&cfg) == NULL && errno == EINVAL,
	       "new rejects overlong name");

	memset(big, 'c', sizeof(big) - 1);
	big[sizeof(big) - 1] = '\0';
	cfg.name = "dmn";
	cfg.config = big;
	errno = 0;
	tap_ok(airy_dmn_new(&cfg) == NULL && errno == EINVAL,
	       "new rejects overlong config");

	memset(&cfg, 0, sizeof(cfg));
	strcpy(name, "orig");
	cfg.name = name;
	dmn = airy_dmn_new(&cfg);
	strcpy(name, "hacked");
	tap_ok(dmn != NULL && strcmp(airy_dmn_name(dmn), "orig") == 0,
	       "new copies name by value");
	tap_ok(dmn != NULL && airy_dmn_state(dmn) == AIRY_DMN_INIT,
	       "new starts in INIT");
	tap_ok(dmn != NULL && dmn->miss_max == AIRY_DMN_DFL_MISS,
	       "miss_max defaults to 3");
	tap_ok(dmn != NULL && dmn->epfd == -1 && dmn->sigfd == -1 &&
	       atomic_load(&dmn->wfd) == -1,
	       "new leaves framework fds unopened");
	airy_dmn_free(dmn);
	airy_dmn_free(NULL);
	tap_ok(true, "free tolerates NULL");
}

/* ── T2 errno 收敛映射 ──────────────────────────────────────────────── */

static void t_err(void)
{
	tap_ok(airy_dmn_err(EEXIST) == -AIRY_EEXIST, "errno maps EEXIST");
	tap_ok(airy_dmn_err(ENOENT) == -AIRY_ENOENT, "errno maps ENOENT");
	tap_ok(airy_dmn_err(ENOMEM) == -AIRY_ENOMEM, "errno maps ENOMEM");
	tap_ok(airy_dmn_err(ENOSPC) == -AIRY_ENOMEM, "errno maps ENOSPC");
	tap_ok(airy_dmn_err(EMFILE) == -AIRY_ENOMEM, "errno maps EMFILE");
	tap_ok(airy_dmn_err(EPERM) == -AIRY_EACCES, "errno maps EPERM");
	tap_ok(airy_dmn_err(999) == -AIRY_EIO, "errno maps unknown to EIO");
}

/* ── T3 状态与时钟的空安全 ──────────────────────────────────────────── */

static void t_state(void)
{
	uint64_t a;
	uint64_t b;

	tap_ok(airy_dmn_state(NULL) == AIRY_DMN_INIT, "state NULL reports INIT");
	tap_ok(strcmp(airy_dmn_name(NULL), "") == 0, "name NULL reports empty");

	airy_dmn_beat(NULL);
	airy_dmn_stop(NULL);
	tap_ok(airy_dmn_live(NULL) == false, "live NULL reports false");

	a = airy_dmn_now();
	b = airy_dmn_now();
	tap_ok(a > 0 && b >= a, "now monotonic and positive");
}

/* ── T4 业务回调绑定 ───────────────────────────────────────────────── */

static void t_reg(void)
{
	struct airy_dmn_cfg cfg;
	struct airy_dmn_ops ops = { 0 };
	struct airy_dmn *dmn;

	cfg_init(&cfg, "reg");
	dmn = airy_dmn_new(&cfg);
	if (!dmn) {
		tap_ok(false, "reg fixture created");
		return;
	}

	tap_ok(airy_dmn_reg(NULL, &ops) == -AIRY_EINVAL, "reg rejects NULL dmn");
	tap_ok(airy_dmn_reg(dmn, NULL) == -AIRY_EINVAL, "reg rejects NULL ops");
	tap_ok(airy_dmn_reg(dmn, &ops) == 0, "reg accepts ops table");
	tap_ok(dmn->bound, "reg marks context bound");

	dmn_state_set(dmn, AIRY_DMN_RUNNING);
	tap_ok(airy_dmn_reg(dmn, &ops) == -AIRY_EBUSY,
	       "reg rejects rebind while running");
	dmn_state_set(dmn, AIRY_DMN_INIT);
	airy_dmn_free(dmn);
}

/* ── T5 fd 事件源注册 ──────────────────────────────────────────────── */

static void t_evtadd(void)
{
	struct airy_dmn_cfg cfg;
	struct airy_dmn *dmn;
	struct airy_dmn *dmn2;
	struct airy_dmn_src src;
	int pf[33][2];
	int p[2];
	int i;
	int ret;

	cfg_init(&cfg, "add");
	dmn = airy_dmn_new(&cfg);
	if (!dmn) {
		tap_ok(false, "add fixture created");
		return;
	}

	memset(&src, 0, sizeof(src));
	tap_ok(airy_dmn_add(NULL, &src) == -AIRY_EINVAL, "add rejects NULL dmn");
	tap_ok(airy_dmn_add(dmn, NULL) == -AIRY_EINVAL, "add rejects NULL src");

	src.fd = -1;
	src.events = AIRY_DMN_IN;
	src.recv = ev_noop;
	tap_ok(airy_dmn_add(dmn, &src) == -AIRY_EINVAL, "add rejects negative fd");

	if (pipe(p)) {
		tap_ok(false, "add pipe created");
		airy_dmn_free(dmn);
		return;
	}

	src.fd = p[0];
	src.recv = NULL;
	tap_ok(airy_dmn_add(dmn, &src) == -AIRY_EINVAL, "add rejects NULL recv");

	src.recv = ev_noop;
	src.events = 0;
	tap_ok(airy_dmn_add(dmn, &src) == -AIRY_EINVAL, "add rejects zero events");

	src.events = 0x100;
	tap_ok(airy_dmn_add(dmn, &src) == -AIRY_EINVAL,
	       "add rejects unknown event bits");

	src.events = AIRY_DMN_IN;
	tap_ok(airy_dmn_add(dmn, &src) == 0, "add registers fd source");
	tap_ok(airy_dmn_find(dmn, p[0]) >= 0, "add publishes slot");
	tap_ok(dmn->nr_evt == 1, "add increments count");
	tap_ok(airy_dmn_add(dmn, &src) == -AIRY_EEXIST, "add rejects duplicate fd");

	for (i = 0; i < 33; i++) {
		if (pipe(pf[i])) {
			pf[i][0] = -1;
			pf[i][1] = -1;
		}
	}

	dmn2 = airy_dmn_new(&cfg);
	for (i = 0; i < AIRY_DMN_MAX_SRC; i++) {
		src.fd = pf[i][0];
		if (airy_dmn_add(dmn2, &src))
			break;
	}
	tap_ok(i == AIRY_DMN_MAX_SRC && dmn2->nr_evt == AIRY_DMN_MAX_SRC,
	       "add fills all slots");

	src.fd = pf[AIRY_DMN_MAX_SRC][0];
	ret = airy_dmn_add(dmn2, &src);
	tap_ok(ret == -AIRY_ENOMEM, "add reports ENOMEM on exhaustion");

	airy_dmn_free(dmn2);
	for (i = 0; i < 33; i++) {
		if (pf[i][0] >= 0)
			close(pf[i][0]);
		if (pf[i][1] >= 0)
			close(pf[i][1]);
	}
	close(p[0]);
	close(p[1]);
	airy_dmn_free(dmn);
}

/* ── T6 fd 事件源注销 ──────────────────────────────────────────────── */

static void t_evtdel(void)
{
	struct airy_dmn_cfg cfg;
	struct airy_dmn *dmn;
	struct airy_dmn_src src;
	int p[2];
	int q[2];

	cfg_init(&cfg, "del");
	dmn = airy_dmn_new(&cfg);
	if (!dmn || pipe(p) || pipe(q)) {
		tap_ok(false, "del fixture created");
		return;
	}

	memset(&src, 0, sizeof(src));
	src.fd = p[0];
	src.events = AIRY_DMN_IN;
	src.recv = ev_noop;
	if (airy_dmn_add(dmn, &src)) {
		tap_ok(false, "del source registered");
		return;
	}

	tap_ok(airy_dmn_del(NULL, p[0]) == -AIRY_EINVAL, "del rejects NULL dmn");
	tap_ok(airy_dmn_del(dmn, -1) == -AIRY_EINVAL, "del rejects negative fd");
	tap_ok(airy_dmn_del(dmn, q[0]) == -AIRY_ENOENT, "del rejects unknown fd");

	atomic_store(&dmn->loop_tid, gettid() + 1000);
	dmn_state_set(dmn, AIRY_DMN_RUNNING);
	tap_ok(airy_dmn_del(dmn, p[0]) == -AIRY_EBUSY,
	       "del rejects off-loop caller while running");
	atomic_store(&dmn->loop_tid, 0);
	dmn_state_set(dmn, AIRY_DMN_INIT);

	tap_ok(airy_dmn_del(dmn, p[0]) == 0, "del removes registered source");
	tap_ok(airy_dmn_find(dmn, p[0]) == -AIRY_ENOENT, "del clears slot");
	tap_ok(dmn->nr_evt == 0, "del decrements count");

	if (airy_dmn_add(dmn, &src))
		tap_ok(false, "del re-register source");
	dmn_state_set(dmn, AIRY_DMN_STOPPED);
	tap_ok(airy_dmn_del(dmn, p[0]) == 0, "del allowed after loop exit");

	close(p[0]);
	close(p[1]);
	close(q[0]);
	close(q[1]);
	airy_dmn_free(dmn);
}

/* ── T7 fd 事件源掩码修改 ──────────────────────────────────────────── */

static void t_evtmod(void)
{
	struct airy_dmn_cfg cfg;
	struct airy_dmn *dmn;
	struct airy_dmn_src src;
	int c[2];
	int p[2];
	int q[2];
	int slot;

	cfg_init(&cfg, "mod");
	dmn = airy_dmn_new(&cfg);
	if (!dmn || pipe(p) || pipe(q)) {
		tap_ok(false, "mod fixture created");
		return;
	}

	memset(&src, 0, sizeof(src));
	src.fd = p[0];
	src.events = AIRY_DMN_IN;
	src.recv = ev_noop;
	if (airy_dmn_add(dmn, &src)) {
		tap_ok(false, "mod source registered");
		return;
	}
	slot = airy_dmn_find(dmn, p[0]);

	tap_ok(airy_dmn_mod(NULL, p[0], AIRY_DMN_IN) == -AIRY_EINVAL,
	       "mod rejects NULL dmn");
	tap_ok(airy_dmn_mod(dmn, p[0], 0) == -AIRY_EINVAL,
	       "mod rejects zero mask");
	tap_ok(airy_dmn_mod(dmn, p[0], 0x100) == -AIRY_EINVAL,
	       "mod rejects unknown bits");
	tap_ok(airy_dmn_mod(dmn, q[0], AIRY_DMN_IN) == -AIRY_ENOENT,
	       "mod rejects unknown fd");
	tap_ok(airy_dmn_mod(dmn, p[0], AIRY_DMN_OUT) == 0,
	       "mod accepts new mask");
	tap_ok(dmn->evts[slot].events == AIRY_DMN_OUT,
	       "mod publishes new mask");

	mk_pair(SOCK_SEQPACKET, c);
	if (c[0] < 0 || airy_dmn_ipc(dmn, c[0], msg_noop, NULL)) {
		tap_ok(false, "mod ipc channel registered");
	} else {
		tap_ok(airy_dmn_mod(dmn, c[0], AIRY_DMN_IN) == -AIRY_EINVAL,
		       "mod rejects IPC channel mask");
	}

	atomic_store(&dmn->loop_tid, gettid() + 1000);
	dmn_state_set(dmn, AIRY_DMN_RUNNING);
	tap_ok(airy_dmn_mod(dmn, p[0], AIRY_DMN_IN) == -AIRY_EBUSY,
	       "mod rejects off-loop caller while running");
	atomic_store(&dmn->loop_tid, 0);
	dmn_state_set(dmn, AIRY_DMN_INIT);

	if (c[0] >= 0) {
		close(c[0]);
		close(c[1]);
	}
	close(p[0]);
	close(p[1]);
	close(q[0]);
	close(q[1]);
	airy_dmn_free(dmn);
}

/* ── T8 IPC 通道注册 ───────────────────────────────────────────────── */

static void t_ipcreg(void)
{
	struct airy_dmn_cfg cfg;
	struct airy_dmn *dmn;
	int seq[2];
	int dgr[2];
	int str[2];
	int p[2];

	cfg_init(&cfg, "ipc");
	dmn = airy_dmn_new(&cfg);
	if (!dmn || pipe(p)) {
		tap_ok(false, "ipc fixture created");
		return;
	}

	tap_ok(airy_dmn_ipc(NULL, p[0], msg_noop, NULL) == -AIRY_EINVAL,
	       "ipc rejects NULL dmn");
	tap_ok(airy_dmn_ipc(dmn, -1, msg_noop, NULL) == -AIRY_EINVAL,
	       "ipc rejects negative fd");
	tap_ok(airy_dmn_ipc(dmn, p[0], NULL, NULL) == -AIRY_EINVAL,
	       "ipc rejects NULL callback");
	tap_ok(airy_dmn_ipc(dmn, p[0], msg_noop, NULL) == -AIRY_EINVAL,
	       "ipc rejects non-socket fd");

	mk_pair(SOCK_STREAM, str);
	if (str[0] < 0)
		tap_ok(false, "ipc stream fixture");
	else
		tap_ok(airy_dmn_ipc(dmn, str[0], msg_noop, NULL) == -AIRY_EINVAL,
		       "ipc rejects SOCK_STREAM");

	mk_pair(SOCK_SEQPACKET, seq);
	if (seq[0] < 0) {
		tap_ok(false, "ipc seqpacket fixture");
	} else {
		tap_ok(airy_dmn_ipc(dmn, seq[0], msg_noop, NULL) == 0,
		       "ipc accepts SOCK_SEQPACKET");
		tap_ok(airy_dmn_ipc(dmn, seq[0], msg_noop, NULL) == -AIRY_EEXIST,
		       "ipc rejects duplicate fd");
	}

	mk_pair(SOCK_DGRAM, dgr);
	if (dgr[0] < 0)
		tap_ok(false, "ipc dgram fixture");
	else
		tap_ok(airy_dmn_ipc(dmn, dgr[0], msg_noop, NULL) == 0,
		       "ipc accepts SOCK_DGRAM");

	if (str[0] >= 0) {
		close(str[0]);
		close(str[1]);
	}
	if (seq[0] >= 0) {
		close(seq[0]);
		close(seq[1]);
	}
	if (dgr[0] >= 0) {
		close(dgr[0]);
		close(dgr[1]);
	}
	close(p[0]);
	close(p[1]);
	airy_dmn_free(dmn);
}

static void hdr_base(struct airy_ipc_msg_hdr *h, uint32_t plen)
{
	memset(h, 0, sizeof(*h));
	h->magic = AIRY_IPC_MAGIC;
	h->opcode = AIRY_IPC_OP_SEND;
	h->payload_len = plen;
}

/* ── T9 IPC 单帧发送 ───────────────────────────────────────────────── */

static void t_ipcsend(void)
{
	struct airy_dmn_cfg cfg;
	struct airy_dmn *dmn;
	struct airy_ipc_msg_hdr hdr;
	struct airy_ipc_msg_hdr exp;
	struct airy_dmn_src src;
	const char payload[] = "hello";
	uint8_t buf[AIRY_IPC_HDR_SIZE + 16];
	struct airy_ipc_msg_hdr *got;
	ssize_t n;
	int c[2];
	int p[2];

	cfg_init(&cfg, "send");
	dmn = airy_dmn_new(&cfg);
	if (!dmn || pipe(p)) {
		tap_ok(false, "send fixture created");
		return;
	}
	mk_pair(SOCK_SEQPACKET, c);
	if (c[0] < 0 || airy_dmn_ipc(dmn, c[0], msg_noop, NULL)) {
		tap_ok(false, "send channel registered");
		return;
	}

	tap_ok(airy_dmn_send(NULL, c[0], NULL, NULL) == -AIRY_EINVAL,
	       "send rejects NULL dmn");
	tap_ok(airy_dmn_send(dmn, c[0], NULL, NULL) == -AIRY_EINVAL,
	       "send rejects NULL header");

	hdr_base(&hdr, 5);
	hdr.magic = 0;
	tap_ok(airy_dmn_send(dmn, c[0], &hdr, payload) == -AIRY_EINVAL,
	       "send rejects bad magic");

	hdr_base(&hdr, 5);
	hdr.flags = AIRY_IPC_FLAG_RESERVED;
	tap_ok(airy_dmn_send(dmn, c[0], &hdr, payload) == -AIRY_EINVAL,
	       "send rejects reserved flags");

	hdr_base(&hdr, AIRY_IPC_MAX_PAYLOAD + 1);
	tap_ok(airy_dmn_send(dmn, c[0], &hdr, payload) == -AIRY_EINVAL,
	       "send rejects oversized payload");

	hdr_base(&hdr, 5);
	tap_ok(airy_dmn_send(dmn, c[0], &hdr, NULL) == -AIRY_EINVAL,
	       "send rejects missing payload");

	hdr_base(&hdr, 5);
	hdr.reserved[0] = 1;
	tap_ok(airy_dmn_send(dmn, c[0], &hdr, payload) == -AIRY_EINVAL,
	       "send rejects dirty reserved bytes");

	hdr_base(&hdr, 5);
	tap_ok(airy_dmn_send(dmn, c[1], &hdr, payload) == -AIRY_ENOENT,
	       "send rejects unregistered fd");

	memset(&src, 0, sizeof(src));
	src.fd = p[0];
	src.events = AIRY_DMN_IN;
	src.recv = ev_noop;
	if (airy_dmn_add(dmn, &src)) {
		tap_ok(false, "send fd source registered");
	} else {
		hdr_base(&hdr, 5);
		tap_ok(airy_dmn_send(dmn, p[0], &hdr, payload) == -AIRY_EINVAL,
		       "send rejects fd-kind target");
	}

	hdr_base(&hdr, 5);
	hdr.crc32 = 0xdeadbeefu;
	exp = hdr;
	exp.crc32 = airy_dmn_crc(&hdr, payload);
	tap_ok(airy_dmn_send(dmn, c[0], &hdr, payload) == 0,
	       "send writes complete record");
	tap_ok(hdr.crc32 == 0xdeadbeefu, "send leaves caller header intact");

	n = recv(c[1], buf, sizeof(buf), 0);
	got = (struct airy_ipc_msg_hdr *)buf;
	tap_ok(n == (ssize_t)(AIRY_IPC_HDR_SIZE + 5) && got->magic == AIRY_IPC_MAGIC
	       && got->opcode == AIRY_IPC_OP_SEND,
	       "send preserves opcode on wire");
	tap_ok(n > 0 && got->payload_len == 5 && got->crc32 == exp.crc32,
	       "send seals CRC over header and payload");
	tap_ok(n > 0 && memcmp(buf + AIRY_IPC_HDR_SIZE, payload, 5) == 0,
	       "send delivers payload unchanged");

	close(c[0]);
	close(c[1]);
	close(p[0]);
	close(p[1]);
	airy_dmn_free(dmn);
}

/* ── T10 摘要覆盖范围 ──────────────────────────────────────────────── */

static void t_crc(void)
{
	struct airy_ipc_msg_hdr a;
	struct airy_ipc_msg_hdr b;
	uint8_t p[4] = { 1, 2, 3, 4 };
	uint8_t q[4];
	uLong z;
	uint32_t base;

	hdr_base(&a, 4);
	base = airy_dmn_crc(&a, p);

	b = a;
	b.crc32 = 0x12345678u;
	tap_ok(airy_dmn_crc(&b, p) == base, "crc ignores crc32 field");

	b = a;
	b.reserved[7] = 9;
	tap_ok(airy_dmn_crc(&b, p) == base, "crc ignores reserved tail");

	b = a;
	b.magic ^= 1u;
	tap_ok(airy_dmn_crc(&b, p) != base, "crc covers header prefix");

	memcpy(q, p, sizeof(q));
	q[1] ^= 0x80u;
	tap_ok(airy_dmn_crc(&a, q) != base, "crc covers payload bytes");

	z = crc32(0L, Z_NULL, 0);
	z = crc32(z, (const Bytef *)&a,
		  (uInt)offsetof(struct airy_ipc_msg_hdr, crc32));
	z = crc32(z, p, 4);
	tap_ok((uint32_t)z == base, "crc matches zlib over [0:52)+payload");
}

/* ── T11 IPC 整帧接收与校验次序 ────────────────────────────────────── */

static int g_rcv_hits;

static int msg_rcv(struct airy_dmn *dmn, const struct airy_ipc_msg_hdr *hdr,
		   const void *payload, void *data)
{
	int *hits = data;

	(void)dmn;
	(void)hdr;
	(void)payload;
	(*hits)++;

	return 0;
}

static void raw_send(int fd, const struct airy_ipc_msg_hdr *h,
		     const void *pl)
{
	uint8_t rec[AIRY_IPC_HDR_SIZE + 8];
	ssize_t n;

	memset(rec, 0, sizeof(rec));
	memcpy(rec, h, sizeof(*h));
	if (h->payload_len && pl)
		memcpy(rec + AIRY_IPC_HDR_SIZE, pl, h->payload_len);
	n = send(fd, rec, AIRY_IPC_HDR_SIZE + h->payload_len, 0);
	(void)n;
}

static void raw_short(int fd, size_t size)
{
	uint8_t rec[64];
	ssize_t n;

	memset(rec, 0, sizeof(rec));
	n = send(fd, rec, size, 0);
	(void)n;
}

/* 仅写出 128 字节头，制造 header 声明长度与实收长度失配。 */
static void raw_hdr(int fd, const struct airy_ipc_msg_hdr *h)
{
	ssize_t n;

	n = send(fd, h, AIRY_IPC_HDR_SIZE, 0);
	(void)n;
}

static void t_ipcrcv(void)
{
	struct airy_dmn_cfg cfg;
	struct airy_dmn *dmn;
	struct airy_ipc_msg_hdr h;
	struct airy_dmn_evt evt;
	struct airy_dmn_evt sev;
	uint8_t pl[4] = { 7, 7, 7, 7 };
	int c[2];
	int sc[2];

	cfg_init(&cfg, "rcv");
	dmn = airy_dmn_new(&cfg);
	if (!dmn) {
		tap_ok(false, "rcv fixture created");
		return;
	}
	mk_pair(SOCK_SEQPACKET, c);
	mk_pair(SOCK_SEQPACKET, sc);
	if (c[0] < 0 || sc[0] < 0) {
		tap_ok(false, "rcv channel created");
		return;
	}

	memset(&evt, 0, sizeof(evt));
	evt.used = true;
	evt.fd = c[0];
	evt.kind = AIRY_DMN_KIND_IPC;
	evt.cb.ipc.msg = msg_rcv;
	evt.cb.ipc.data = &g_rcv_hits;

	memset(&sev, 0, sizeof(sev));
	sev.used = true;
	sev.fd = sc[0];
	sev.kind = AIRY_DMN_KIND_IPC;
	sev.cb.ipc.msg = msg_rcv;
	sev.cb.ipc.data = &g_rcv_hits;

	/* 短帧是终态：框架不做字节流重同步，须在独立通道上验证。 */
	raw_short(sc[1], 64);
	tap_ok(airy_dmn_ipcrecv(dmn, &sev) == -AIRY_EIPC_HDRSIZE,
	       "recv rejects short record");
	close(sc[0]);
	close(sc[1]);

	hdr_base(&h, 0);
	h.magic = 0;
	h.flags = AIRY_IPC_FLAG_RESERVED;
	raw_send(c[1], &h, NULL);
	tap_ok(airy_dmn_ipcrecv(dmn, &evt) == -AIRY_EIPC_MAGIC,
	       "recv checks magic before flags");

	hdr_base(&h, 3);
	h.flags = AIRY_IPC_FLAG_RESERVED;
	raw_send(c[1], &h, NULL);
	tap_ok(airy_dmn_ipcrecv(dmn, &evt) == -AIRY_EIPC_FLAGS,
	       "recv checks flags before payload length");

	hdr_base(&h, 3);
	h.reserved[1] = 5;
	raw_hdr(c[1], &h);
	tap_ok(airy_dmn_ipcrecv(dmn, &evt) == -AIRY_EIPC_PAYLOAD,
	       "recv checks payload length before reserved");

	hdr_base(&h, 0);
	h.reserved[1] = 5;
	h.crc32 = 0x11111111u;
	raw_send(c[1], &h, NULL);
	tap_ok(airy_dmn_ipcrecv(dmn, &evt) == -AIRY_EIPC_RESERVED,
	       "recv checks reserved before CRC");

	hdr_base(&h, 0);
	h.crc32 = 0x11111111u;
	raw_send(c[1], &h, NULL);
	tap_ok(airy_dmn_ipcrecv(dmn, &evt) == -AIRY_EIPC_CRC32,
	       "recv rejects bad CRC");

	g_rcv_hits = 0;
	hdr_base(&h, 4);
	h.crc32 = airy_dmn_crc(&h, pl);
	raw_send(c[1], &h, pl);
	tap_ok(airy_dmn_ipcrecv(dmn, &evt) == 0 && g_rcv_hits == 1,
	       "recv dispatches validated frame");

	if (fcntl(c[0], F_SETFL, O_NONBLOCK)) {
		tap_ok(false, "rcv nonblocking fd");
	} else {
		g_rcv_hits = 0;
		tap_ok(airy_dmn_ipcrecv(dmn, &evt) == 0 && g_rcv_hits == 0,
		       "recv reports no pending frame");
	}

	close(c[0]);
	close(c[1]);
	airy_dmn_free(dmn);
}

/* ── T12 IPC 批量发送 ──────────────────────────────────────────────── */

static void t_batch(void)
{
	struct airy_dmn_cfg cfg;
	struct airy_dmn *dmn;
	struct airy_ipc_msg_hdr h[17];
	struct airy_ipc_msg_hdr bad;
	struct airy_dmn_frame fr[17];
	struct airy_dmn_frame bf[2];
	struct airy_ipc_msg_hdr bh[2];
	uint8_t body[17][4];
	uint8_t buf[AIRY_IPC_HDR_SIZE + 16];
	struct airy_ipc_msg_hdr *got;
	size_t done;
	ssize_t n;
	int c[2];
	int i;
	int ret;

	cfg_init(&cfg, "batch");
	dmn = airy_dmn_new(&cfg);
	if (!dmn) {
		tap_ok(false, "batch fixture created");
		return;
	}
	mk_pair(SOCK_SEQPACKET, c);
	if (c[0] < 0 || airy_dmn_ipc(dmn, c[0], msg_noop, NULL)) {
		tap_ok(false, "batch channel registered");
		return;
	}

	done = 0;
	tap_ok(airy_dmn_batch(dmn, c[0], NULL, 3, &done) == -AIRY_EINVAL,
	       "batch rejects NULL frames");
	tap_ok(airy_dmn_batch(dmn, c[0], fr, 1, &done) == -AIRY_EINVAL,
	       "batch rejects single frame");

	hdr_base(&bh[0], 0);
	hdr_base(&bh[1], 0);
	bad = bh[1];
	bad.magic = 0;
	bf[0].hdr = &bh[0];
	bf[0].payload = NULL;
	bf[1].hdr = &bad;
	bf[1].payload = NULL;
	done = 99;
	tap_ok(airy_dmn_batch(dmn, c[0], bf, 2, &done) == -AIRY_EINVAL &&
	       done == 0, "batch validates all frames before writing");
	n = recv(c[1], buf, sizeof(buf), MSG_DONTWAIT);
	tap_ok(n < 0 && (errno == EAGAIN || errno == EWOULDBLOCK),
	       "batch leaves no partial record on validate failure");

	for (i = 0; i < 17; i++) {
		hdr_base(&h[i], 4);
		memcpy(body[i], "abcd", 4);
		fr[i].hdr = &h[i];
		fr[i].payload = body[i];
	}
	done = 0;
	ret = airy_dmn_batch(dmn, c[0], fr, 17, &done);
	tap_ok(ret == 0 && done == 17, "batch writes all frames across blocks");

	i = 0;
	while (i < 17) {
		n = recv(c[1], buf, sizeof(buf), 0);
		if (n <= 0)
			break;
		got = (struct airy_ipc_msg_hdr *)buf;
		if (got->opcode != AIRY_IPC_OP_SEND_BATCH)
			break;
		if (i + 1 < 17 && (got->flags & AIRY_IPC_FLAG_BATCH_TAIL))
			break;
		if (i + 1 == 17 && !(got->flags & AIRY_IPC_FLAG_BATCH_TAIL))
			break;
		i++;
	}
	tap_ok(i == 17, "batch marks only the last frame as tail");

	close(c[0]);
	close(c[1]);
	airy_dmn_free(dmn);
}

/* ── T13 systemd 上报 ──────────────────────────────────────────────── */

static void t_notify(void)
{
	char buf[256];
	char path[] = "/tmp/airy_dmn_notify_test.sock";
	char abs[] = "@airy_dmn_abs_test";
	struct sockaddr_un a;
	struct timeval tv = { 2, 0 };
	char *big;
	socklen_t alen;
	ssize_t n;
	int fd;
	int fd2;
	int ret;

	unsetenv("NOTIFY_SOCKET");
	tap_ok(airy_dmn_notify(NULL) == -AIRY_EINVAL, "notify rejects NULL");
	tap_ok(airy_dmn_notify("") == -AIRY_EINVAL, "notify rejects empty");
	big = malloc(5000);
	if (!big) {
		tap_ok(false, "notify fixture allocated");
		return;
	}
	memset(big, 'x', 4999);
	big[4999] = '\0';
	tap_ok(airy_dmn_notify(big) == -AIRY_EINVAL,
	       "notify rejects overlong state");
	tap_ok(airy_dmn_notify("READY=1") == 0, "notify is noop without socket");

	fd = socket(AF_UNIX, SOCK_DGRAM, 0);
	if (fd < 0) {
		tap_ok(false, "notify receiver socket");
		free(big);
		return;
	}
	(void)setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
	unlink(path);
	memset(&a, 0, sizeof(a));
	a.sun_family = AF_UNIX;
	strcpy(a.sun_path, path);
	alen = (socklen_t)(offsetof(struct sockaddr_un, sun_path) +
			   strlen(path) + 1);
	if (bind(fd, (struct sockaddr *)&a, alen)) {
		tap_ok(false, "notify receiver bound");
		close(fd);
		free(big);
		return;
	}

	setenv("NOTIFY_SOCKET", path, 1);
	tap_ok(airy_dmn_notify("READY=1") == 0, "notify posts to filesystem socket");
	n = recv(fd, buf, sizeof(buf) - 1, 0);
	if (n > 0)
		buf[n] = '\0';
	tap_ok(n == 7 && strcmp(buf, "READY=1") == 0,
	       "notify delivers state verbatim");

	fd2 = socket(AF_UNIX, SOCK_DGRAM, 0);
	if (fd2 < 0) {
		tap_ok(false, "notify abstract socket");
	} else {
		(void)setsockopt(fd2, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
		memset(&a, 0, sizeof(a));
		a.sun_family = AF_UNIX;
		strcpy(a.sun_path + 1, abs + 1);
		alen = (socklen_t)(offsetof(struct sockaddr_un, sun_path) +
				   strlen(abs));
		ret = bind(fd2, (struct sockaddr *)&a, alen);
		if (ret) {
			tap_ok(false, "notify abstract receiver bound");
		} else {
			setenv("NOTIFY_SOCKET", abs, 1);
			tap_ok(airy_dmn_notify("WATCHDOG=1") == 0,
			       "notify posts to abstract socket");
			n = recv(fd2, buf, sizeof(buf) - 1, 0);
			if (n > 0)
				buf[n] = '\0';
			tap_ok(n == 10 && strcmp(buf, "WATCHDOG=1") == 0,
			       "notify delivers abstract state verbatim");
		}
		close(fd2);
	}

	setenv("NOTIFY_SOCKET", "/tmp/airy_dmn_absent.sock", 1);
	unlink("/tmp/airy_dmn_absent.sock");
	tap_ok(airy_dmn_notify("READY=1") == -AIRY_ENOENT,
	       "notify reports absent socket");

	setenv("NOTIFY_SOCKET", big, 1);
	tap_ok(airy_dmn_notify("READY=1") == -AIRY_EINVAL,
	       "notify rejects overlong socket path");

	close(fd);
	unlink(path);
	free(big);
	unsetenv("NOTIFY_SOCKET");
}

/* ── T14 心跳记账 ──────────────────────────────────────────────────── */

static void t_beat(void)
{
	struct airy_dmn_cfg cfg;
	struct airy_dmn *dmn;

	cfg_init(&cfg, "beat");
	cfg.beat_ms = 0;
	dmn = airy_dmn_new(&cfg);
	if (!dmn) {
		tap_ok(false, "beat fixture created");
		return;
	}
	airy_dmn_beat(dmn);
	tap_ok(!airy_dmn_live(dmn), "live false when beat disabled");
	airy_dmn_free(dmn);

	cfg_init(&cfg, "beat");
	cfg.beat_ms = 1000;
	cfg.miss_max = 3;
	dmn = airy_dmn_new(&cfg);
	if (!dmn) {
		tap_ok(false, "beat window fixture created");
		return;
	}
	tap_ok(!airy_dmn_live(dmn), "live false before first beat");
	airy_dmn_beat(dmn);
	tap_ok(airy_dmn_live(dmn), "live true right after beat");

	atomic_store(&dmn->last_beat, airy_dmn_now() - 2900);
	tap_ok(airy_dmn_live(dmn), "live true inside tolerance window");
	atomic_store(&dmn->last_beat, airy_dmn_now() - 3100);
	tap_ok(!airy_dmn_live(dmn), "live false beyond tolerance window");
	airy_dmn_free(dmn);
}

/* ── T15 主循环生命周期 ────────────────────────────────────────────── */

struct run_ctx {
	_Atomic bool started;
	_Atomic bool stopped;
	_Atomic bool ticked;
};

struct thr_ctx {
	struct airy_dmn *dmn;
	_Atomic pthread_t self;
	_Atomic int ret;
	_Atomic bool done;
};

static int cb_start(struct airy_dmn *dmn, void *data)
{
	struct run_ctx *c = data;

	(void)dmn;
	atomic_store(&c->started, true);

	return 0;
}

static int cb_stop(struct airy_dmn *dmn, void *data)
{
	struct run_ctx *c = data;

	(void)dmn;
	atomic_store(&c->stopped, true);

	return 0;
}

static int cb_start_stop(struct airy_dmn *dmn, void *data)
{
	struct run_ctx *c = data;

	atomic_store(&c->started, true);
	airy_dmn_stop(dmn);

	return 0;
}

static void *thr_main(void *arg)
{
	struct thr_ctx *c = arg;

	atomic_store(&c->self, pthread_self());
	atomic_store(&c->ret, airy_dmn_run(c->dmn));
	atomic_store(&c->done, true);

	return NULL;
}

static void t_run(void)
{
	struct airy_dmn_cfg cfg;
	struct airy_dmn_ops ops;
	struct airy_dmn *dmn;
	struct airy_dmn *dmn2;
	struct run_ctx rc;
	struct thr_ctx tc;
	pthread_t th;

	memset(&rc, 0, sizeof(rc));
	memset(&tc, 0, sizeof(tc));
	cfg_init(&cfg, "run");
	dmn = airy_dmn_new(&cfg);
	if (!dmn) {
		tap_ok(false, "run fixture created");
		return;
	}

	tap_ok(airy_dmn_run(NULL) == -AIRY_EINVAL, "run rejects NULL dmn");
	cfg_init(&cfg, "run2");
	dmn2 = airy_dmn_new(&cfg);
	tap_ok(airy_dmn_run(dmn2) == -AIRY_EINVAL, "run rejects unbound context");
	airy_dmn_free(dmn2);

	memset(&ops, 0, sizeof(ops));
	ops.start = cb_start;
	ops.stop = cb_stop;
	ops.data = &rc;
	tap_ok(airy_dmn_reg(dmn, &ops) == 0, "run binds callbacks");

	airy_dmn_stop(dmn);
	tap_ok(airy_dmn_run(dmn) == 0, "run honours pre-issued stop");
	tap_ok(!atomic_load(&rc.started), "pre-stop skips start callback");
	tap_ok(!atomic_load(&rc.stopped), "pre-stop skips stop callback");
	tap_ok(airy_dmn_state(dmn) == AIRY_DMN_STOPPED, "state settles STOPPED");
	tap_ok(airy_dmn_run(dmn) == -AIRY_EBUSY, "run rejects second entry");
	airy_dmn_free(dmn);

	memset(&rc, 0, sizeof(rc));
	cfg_init(&cfg, "run3");
	dmn = airy_dmn_new(&cfg);
	memset(&ops, 0, sizeof(ops));
	ops.start = cb_start_stop;
	ops.stop = cb_stop;
	ops.data = &rc;
	if (airy_dmn_reg(dmn, &ops)) {
		tap_ok(false, "run stop-in-start bound");
		return;
	}
	tap_ok(airy_dmn_run(dmn) == 0, "run exits when start requests stop");
	tap_ok(atomic_load(&rc.started) && atomic_load(&rc.stopped),
	       "stop-in-start still runs stop callback");
	airy_dmn_free(dmn);

	memset(&rc, 0, sizeof(rc));
	cfg_init(&cfg, "run4");
	dmn = airy_dmn_new(&cfg);
	memset(&ops, 0, sizeof(ops));
	ops.start = cb_start;
	ops.stop = cb_stop;
	ops.data = &rc;
	if (airy_dmn_reg(dmn, &ops)) {
		tap_ok(false, "run worker bound");
		return;
	}
	memset(&tc, 0, sizeof(tc));
	tc.dmn = dmn;
	if (pthread_create(&th, NULL, thr_main, &tc)) {
		tap_ok(false, "run worker created");
		return;
	}
	tap_ok(wait_state(dmn, AIRY_DMN_RUNNING, 1000),
	       "run enters RUNNING on worker");
	tap_ok(atomic_load(&rc.started), "start callback invoked");

	airy_dmn_stop(dmn);
	(void)pthread_join(th, NULL);
	tap_ok(atomic_load(&tc.done) && atomic_load(&tc.ret) == 0,
	       "stop unwinds loop with success");
	tap_ok(atomic_load(&rc.stopped), "stop callback invoked");
	tap_ok(airy_dmn_state(dmn) == AIRY_DMN_STOPPED,
	       "state settles STOPPED after run");
	airy_dmn_free(dmn);
}

/* ── T16 fd 事件分派与运行期变更 ──────────────────────────────────── */

struct io_ctx {
	_Atomic bool in_seen;
	_Atomic bool hup_seen;
	_Atomic bool del_done;
	_Atomic bool mod_ok;
	_Atomic bool add_ok;
	_Atomic bool second;
	int fd2;
};

static int cb_ev2(struct airy_dmn *dmn, int fd, unsigned int events, void *data)
{
	struct io_ctx *c = data;
	uint8_t buf[64];
	ssize_t n;

	(void)dmn;
	(void)events;
	n = read(fd, buf, sizeof(buf));
	(void)n;
	atomic_store(&c->second, true);

	return 0;
}

static int cb_ev(struct airy_dmn *dmn, int fd, unsigned int events, void *data)
{
	struct io_ctx *c = data;
	struct airy_dmn_src s2;
	uint8_t buf[64];
	ssize_t n;

	if (events & AIRY_DMN_IN) {
		n = read(fd, buf, sizeof(buf));
		(void)n;
		if (!atomic_load(&c->in_seen)) {
			atomic_store(&c->in_seen, true);
			atomic_store(&c->mod_ok,
				     airy_dmn_mod(dmn, fd, AIRY_DMN_IN) == 0);
			memset(&s2, 0, sizeof(s2));
			s2.fd = c->fd2;
			s2.events = AIRY_DMN_IN;
			s2.recv = cb_ev2;
			s2.data = c;
			atomic_store(&c->add_ok, airy_dmn_add(dmn, &s2) == 0);
		}
	}

	if (events & AIRY_DMN_HUP) {
		atomic_store(&c->hup_seen, true);
		airy_dmn_del(dmn, fd);
		atomic_store(&c->del_done, true);
	}

	return 0;
}

static void t_evtio(void)
{
	struct airy_dmn_cfg cfg;
	struct airy_dmn_ops ops;
	struct airy_dmn_src src;
	struct airy_dmn *dmn;
	struct io_ctx ic;
	struct thr_ctx tc;
	pthread_t th;
	ssize_t n;
	int p1[2];
	int p2[2];

	memset(&ic, 0, sizeof(ic));
	memset(&tc, 0, sizeof(tc));
	cfg_init(&cfg, "io");
	dmn = airy_dmn_new(&cfg);
	if (!dmn || pipe(p1) || pipe(p2)) {
		tap_ok(false, "io fixture created");
		return;
	}
	ic.fd2 = p2[0];
	memset(&src, 0, sizeof(src));
	src.fd = p1[0];
	src.events = AIRY_DMN_IN;
	src.recv = cb_ev;
	src.data = &ic;
	if (airy_dmn_add(dmn, &src)) {
		tap_ok(false, "io source registered");
		return;
	}
	memset(&ops, 0, sizeof(ops));
	if (airy_dmn_reg(dmn, &ops)) {
		tap_ok(false, "io bound");
		return;
	}
	tc.dmn = dmn;
	if (pthread_create(&th, NULL, thr_main, &tc)) {
		tap_ok(false, "io worker created");
		return;
	}
	if (!wait_state(dmn, AIRY_DMN_RUNNING, 1000)) {
		tap_ok(false, "io loop running");
		return;
	}

	n = write(p1[1], "a", 1);
	(void)n;
	tap_ok(wait_bool(&ic.in_seen, 1000), "fd IN event dispatched");
	tap_ok(atomic_load(&ic.mod_ok), "mod allowed inside callback");
	tap_ok(atomic_load(&ic.add_ok), "add allowed inside callback");

	n = write(p2[1], "b", 1);
	(void)n;
	tap_ok(wait_bool(&ic.second, 1000), "runtime-added source dispatched");

	close(p1[1]);
	tap_ok(wait_bool(&ic.hup_seen, 1000), "HUP dispatched past mask");
	tap_ok(wait_bool(&ic.del_done, 1000) &&
	       airy_dmn_find(dmn, p1[0]) == -AIRY_ENOENT,
	       "callback-side del reclaims slot");

	close(p2[1]);
	airy_dmn_stop(dmn);
	(void)pthread_join(th, NULL);
	tap_ok(atomic_load(&tc.ret) == 0, "io loop unwinds cleanly");
	close(p1[0]);
	close(p2[0]);
	airy_dmn_free(dmn);
}

/* ── T17 周期 tick 固定网格与错过合并 ─────────────────────────────── */

struct tick_ctx {
	_Atomic int n;
	unsigned int hold_us;
};

static int cb_tick(struct airy_dmn *dmn, void *data)
{
	struct tick_ctx *c = data;

	(void)dmn;
	atomic_fetch_add(&c->n, 1);
	if (c->hold_us)
		(void)usleep(c->hold_us);

	return 0;
}

static void tick_run(unsigned int tick_ms, unsigned int hold_us,
		     unsigned int span_ms, struct tick_ctx *tc)
{
	struct airy_dmn_cfg cfg;
	struct airy_dmn_ops ops;
	struct airy_dmn *dmn;
	struct thr_ctx thc;
	pthread_t th;

	memset(tc, 0, sizeof(*tc));
	tc->hold_us = hold_us;
	memset(&thc, 0, sizeof(thc));
	cfg_init(&cfg, "tick");
	cfg.tick_ms = tick_ms;
	dmn = airy_dmn_new(&cfg);
	if (!dmn) {
		tap_ok(false, "tick fixture created");
		return;
	}
	memset(&ops, 0, sizeof(ops));
	ops.tick = cb_tick;
	ops.data = tc;
	if (airy_dmn_reg(dmn, &ops)) {
		tap_ok(false, "tick bound");
		return;
	}
	thc.dmn = dmn;
	if (pthread_create(&th, NULL, thr_main, &thc)) {
		tap_ok(false, "tick worker created");
		return;
	}
	if (!wait_state(dmn, AIRY_DMN_RUNNING, 1000)) {
		tap_ok(false, "tick loop running");
		return;
	}
	nap_ms(span_ms);
	airy_dmn_stop(dmn);
	(void)pthread_join(th, NULL);
	airy_dmn_free(dmn);
}

static void t_tick(void)
{
	struct tick_ctx a;
	struct tick_ctx b;
	int fast;

	tick_run(20, 0, 200, &a);
	tap_ok(atomic_load(&a.n) >= 5, "tick fires on fixed grid");

	tick_run(20, 70000, 200, &b);
	fast = atomic_load(&b.n);
	tap_ok(fast >= 1 && fast <= 5, "tick merges missed periods");
}

/* ── T18 信号收口 ─────────────────────────────────────────────────── */

static _Atomic int g_reloads;

static int cb_reload(struct airy_dmn *dmn, void *data)
{
	(void)dmn;
	(void)data;
	atomic_fetch_add(&g_reloads, 1);

	return 0;
}

static void t_sig(void)
{
	struct airy_dmn_cfg cfg;
	struct airy_dmn_ops ops;
	struct airy_dmn *dmn;
	struct thr_ctx tc;
	pthread_t th;
	pthread_t tid;

	memset(&tc, 0, sizeof(tc));
	atomic_store(&g_reloads, 0);
	cfg_init(&cfg, "sig");
	cfg.flags = AIRY_DMN_F_SIG;
	dmn = airy_dmn_new(&cfg);
	if (!dmn) {
		tap_ok(false, "sig fixture created");
		return;
	}
	memset(&ops, 0, sizeof(ops));
	ops.reload = cb_reload;
	if (airy_dmn_reg(dmn, &ops)) {
		tap_ok(false, "sig bound");
		return;
	}
	tc.dmn = dmn;
	if (pthread_create(&th, NULL, thr_main, &tc)) {
		tap_ok(false, "sig worker created");
		return;
	}
	if (!wait_state(dmn, AIRY_DMN_RUNNING, 1000)) {
		tap_ok(false, "sig loop running");
		return;
	}
	tid = atomic_load(&tc.self);

	(void)pthread_kill(tid, SIGHUP);
	tap_ok(wait_cnt(&g_reloads, 1, 1000), "SIGHUP triggers reload");

	(void)pthread_kill(tid, SIGINT);
	(void)pthread_join(th, NULL);
	tap_ok(atomic_load(&tc.done) && atomic_load(&tc.ret) == 0,
	       "SIGINT stops loop cleanly");
	tap_ok(airy_dmn_state(dmn) == AIRY_DMN_STOPPED,
	       "sig loop settles STOPPED");
	airy_dmn_free(dmn);
}

int main(void)
{
	setvbuf(stdout, NULL, _IONBF, 0);

	t_new();
	t_err();
	t_state();
	t_reg();
	t_evtadd();
	t_evtdel();
	t_evtmod();
	t_ipcreg();
	t_ipcsend();
	t_crc();
	t_ipcrcv();
	t_batch();
	t_notify();
	t_beat();
	t_run();
	t_evtio();
	t_tick();
	t_sig();

	printf("1..%d\n", g_done);
	if (g_fail)
		printf("# %d of %d checks failed\n", g_fail, g_done);

	return g_fail ? 1 : 0;
}
