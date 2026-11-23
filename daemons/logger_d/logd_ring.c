// SPDX-License-Identifier: AGPL-3.0-or-later OR Apache-2.0
/*
 * Copyright (c) 2025-2026 SPHARX Ltd. All Rights Reserved.
 *
 * logd_ring.c —— /dev/airy_log 的只读消费端
 *
 * 环由内核生产者推进 head / committed_head / tail，用户态只读映射区，
 * 用私有 cursor 逐槽前进，不写任何共享状态。记录是否可读由槽位里的
 * release 序号判定：序号不等于 cursor+1 说明该槽已被后来的写入覆盖，
 * 计入丢失并跳过，绝不把半新半旧的记录写进日志。
 */
#define _GNU_SOURCE

#include <errno.h>
#include <fcntl.h>
#include <stdint.h>
#include <string.h>
#include <sys/eventfd.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <unistd.h>

#include <linux/airymax/error.h>

#include "logd.h"

#define LOGD_DRAIN_MAX	4096

static uint64_t slot_seq(const struct airy_log_record *rec)
{
	uint64_t *p = (uint64_t *)(void *)rec->reserved;
	uint64_t seq;

	__atomic_load(p, &seq, __ATOMIC_ACQUIRE);
	return seq;
}

int logd_ring_open(struct logd_ring *ring, const char *dev)
{
	struct airy_log_geom geom;
	uint64_t need;
	void *map;
	int efd;
	int fd;

	if (!ring || !dev)
		return -AIRY_EINVAL;
	memset(ring, 0, sizeof(*ring));
	ring->dev_fd = -1;
	ring->efd = -1;

	fd = open(dev, O_RDONLY | O_CLOEXEC);
	if (fd < 0)
		return errno == ENOENT ? -AIRY_ENOENT : -AIRY_EIO;
	if (ioctl(fd, AIRY_LOG_IOC_GEOM, &geom) < 0) {
		close(fd);
		return -AIRY_EIO;
	}
	need = (uint64_t)geom.hdr_size +
	       (uint64_t)geom.capacity * geom.record_size;
	if (geom.abi != AIRY_LOG_RING_ABI ||
	    geom.record_size != AIRY_LOG_RECORD_SIZE ||
	    geom.capacity < 2 ||
	    (geom.capacity & (geom.capacity - 1)) ||
	    geom.hdr_size < sizeof(struct airy_log_ring_header) ||
	    need > geom.ring_bytes ||
	    (uint64_t)(size_t)geom.ring_bytes != geom.ring_bytes) {
		close(fd);
		return -AIRY_EINVAL;
	}

	map = mmap(NULL, (size_t)geom.ring_bytes, PROT_READ, MAP_SHARED, fd, 0);
	if (map == MAP_FAILED) {
		close(fd);
		return -AIRY_EIO;
	}

	efd = eventfd(0, EFD_CLOEXEC | EFD_NONBLOCK);
	if (efd < 0) {
		munmap(map, (size_t)geom.ring_bytes);
		close(fd);
		return -AIRY_ENOMEM;
	}
	if (ioctl(fd, AIRY_LOG_IOC_EFD, &efd) < 0) {
		close(efd);
		munmap(map, (size_t)geom.ring_bytes);
		close(fd);
		return -AIRY_EIO;
	}

	ring->dev_fd	= fd;
	ring->efd	= efd;
	ring->map	= map;
	ring->map_len	= (size_t)geom.ring_bytes;
	ring->capacity	= geom.capacity;
	ring->hdr_size	= geom.hdr_size;
	ring->rec_size	= geom.record_size;
	return 0;
}

void logd_ring_close(struct logd_ring *ring)
{
	if (!ring)
		return;
	if (ring->efd >= 0) {
		close(ring->efd);
		ring->efd = -1;
	}
	if (ring->map) {
		munmap(ring->map, ring->map_len);
		ring->map = NULL;
	}
	if (ring->dev_fd >= 0) {
		close(ring->dev_fd);
		ring->dev_fd = -1;
	}
	ring->map_len = 0;
}

int logd_ring_drain(struct logd_ring *ring, logd_emit emit, void *ctx,
		    unsigned int *nr)
{
	struct airy_log_ring_header *hdr;
	const unsigned char *base;
	unsigned int n = 0;
	uint64_t head;
	uint64_t tail;
	int ret;

	if (!ring || !ring->map || !emit)
		return -AIRY_EINVAL;

	hdr = ring->map;
	base = (const unsigned char *)ring->map + ring->hdr_size;
	head = __atomic_load_n(&hdr->committed_head, __ATOMIC_ACQUIRE);
	tail = __atomic_load_n(&hdr->tail, __ATOMIC_ACQUIRE);

	if (ring->cursor < tail) {
		ring->lost += tail - ring->cursor;
		ring->cursor = tail;
	}
	if (ring->cursor > head)
		ring->cursor = head;

	while (ring->cursor < head) {
		const struct airy_log_record *rec = (const void *)
			(base + (ring->cursor % ring->capacity) *
				ring->rec_size);

		if (slot_seq(rec) != ring->cursor + 1) {
			ring->lost++;
			ring->cursor++;
			continue;
		}
		ret = emit(ctx, rec);
		if (ret)
			return ret;
		ring->cursor++;
		n++;
		if (n >= LOGD_DRAIN_MAX)
			break;
	}

	/* 未取空时保留 eventfd 计数，让下一轮 epoll 立即再次唤醒 */
	if (ring->cursor >= head) {
		uint64_t v;

		while (read(ring->efd, &v, sizeof(v)) == sizeof(v))
			;
	}
	if (nr)
		*nr = n;
	return 0;
}
