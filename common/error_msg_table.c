// SPDX-License-Identifier: AGPL-3.0-or-later OR Apache-2.0
/*
 * Copyright (c) 2025-2026 SPHARX Ltd. All Rights Reserved.
 *
 * error_msg_table.c — 错误码到消息标识的映射表（自动生成，请勿手改）
 *
 * 唯一数据源：kernel/include/uapi/linux/airymax/error.h [SC]
 * 生成器：services/tools/error-msg/error_msg.py
 * 重新生成：python3 services/tools/error-msg/error_msg.py --gen
 *
 * 错误码或注释变更后必须重新生成；ci/gates/i18n.sh 比对签入产物，
 * 漂移即判负，避免消息目录与 [SC] 契约各说各话。
 */
#include <stddef.h>

#include "error_msg_int.h"

struct airy_err_msg {
	unsigned int code;
	const char *msgid;
	const char *text;
};

static const struct airy_err_msg g_err_msgs[] = {
	{ 0u, "AIRY_EOK", "Success" },
	{ 1u, "AIRY_EACCES", "Operation not permitted" },
	{ 2u, "AIRY_EEXIST", "File exists" },
	{ 3u, "AIRY_EFAULT", "Bad address" },
	{ 4u, "AIRY_EINTR", "Interrupted system call" },
	{ 5u, "AIRY_EINVAL", "Invalid argument" },
	{ 6u, "AIRY_EIO", "I/O error" },
	{ 7u, "AIRY_EISDIR", "Is a directory" },
	{ 8u, "AIRY_ENOENT", "No such file or directory" },
	{ 9u, "AIRY_ENOMEM", "Out of memory" },
	{ 10u, "AIRY_ENOSPC", "No space left on device" },
	{ 11u, "AIRY_ENOTSUP", "Operation not supported" },
	{ 12u, "AIRY_EPERM", "Operation not permitted (POSIX)" },
	{ 13u, "AIRY_ERANGE", "Result too large" },
	{ 16u, "AIRY_EBUSY", "Device or resource busy" },
	{ 19u, "AIRY_ECANCELED", "Operation canceled" },
	{ 35u, "AIRY_EAGAIN", "Try again" },
	{ 41u, "AIRY_EIPC_MAGIC", "C-S1:  Invalid IPC magic" },
	{ 42u, "AIRY_EIPC_OPCODE", "C-S2:  Unknown IPC opcode" },
	{ 43u, "AIRY_EIPC_PAYLOAD", "C-S3:  payload_len out of bounds" },
	{ 44u, "AIRY_EIPC_HDRSIZE", "C-S4:  Header size != 128 bytes" },
	{ 45u, "AIRY_EIPC_RESERVED", "C-S4:  reserved[72] not all zero" },
	{ 46u, "AIRY_EIPC_FLAGS", "C-S10: flags invalid (reserved bits nonzero)" },
	{ 47u, "AIRY_EIPC_NOTSUPP",
	  "C-S10: opcode/flag not supported (e.g. ENCRYPT/COMPRESS)" },
	{ 48u, "AIRY_EIPC_KFIFO", "C-S6:  kfifo enqueue failed" },
	{ 49u, "AIRY_EIPC_RECLAIM", "C-S7:  reclaim flag set" },
	{ 50u, "AIRY_EIPC_CONTEXT", "C-S8:  context check failed (!in_task)" },
	{ 51u, "AIRY_EIPC_CRC32",
	  "C-S12: CRC32 check failed (header[0:52) + payload)" },
	{ 52u, "AIRY_EIPC_TIMEOUT", "SLOW_SEND timeout" },
	{ 53u, "AIRY_EIPC_FROZEN",
	  "C-S0:  Ring frozen (fastpath freeze check, A-ULS controlled)" },
	{ 71u, "AIRY_ECAP_MISSING", "Capability not found" },
	{ 72u, "AIRY_ECAP_REVOKED", "Capability revoked" },
	{ 73u, "AIRY_ECAP_EXPIRED", "Capability expired" },
	{ 74u, "AIRY_ECAP_MISMATCH", "Capability mismatch" },
	{ 75u, "AIRY_ECAP_LSM_DENIED", "Pure-C LSM denied" },
	{ 76u, "AIRY_ECAP_RADIX_MISS", "[DSL] radix tree lookup miss" },
	{ 77u, "AIRY_ECAP_STATIC_KEY", "[DSL] static_key disabled" },
	{ 78u, "AIRY_ECAP_BADGE",
	  "Badge invalid / RandomTag mismatch / CAP_CARRY but badge=0" },
	{ 79u, "AIRY_ECAP_EPOCH", "Badge Epoch mismatch (revoked or expired)" },
	{ 80u, "AIRY_ECAP_FORGED",
	  "Badge forgery detected (also triggers AIRY_FAULT_CAP_FORGED)" },
	{ 81u, "AIRY_ECAP_PERM", "Badge permissions insufficient for opcode" },
	{ 82u, "AIRY_ECAP_FROZEN",
	  "Capability badge frozen (badge revocation, A-ULS controlled)" },
	{ 83u, "AIRY_ESEC_D_THROTTLED", "sec_d throttle rejected (queue full)" },
	{ 84u, "AIRY_ECAP_OVERFLOW",
	  "Capability slot table overflow (agent_id >= AIRY_CAP_MAX_AGENTS)" },
	{ 101u, "AIRY_ECFGVERSION", "Configuration version mismatch" },
	{ 102u, "AIRY_ECFGSCHEMA", "Configuration schema invalid" },
	{ 103u, "AIRY_ECFGBASE64", "Base64 decode failure" },
	{ 104u, "AIRY_ECFGJSON", "JSON parse failure" },
	{ 105u, "AIRY_ECFGIO", "Config I/O error" },
	{ 121u, "AIRY_ESCHED_POLICY", "Invalid scheduling policy" },
	{ 122u, "AIRY_ESCHED_BUDGET", "Runtime budget exceeded" },
	{ 123u, "AIRY_ESCHED_DEADLINE", "Deadline missed" },
	{ 124u, "AIRY_ESCHED_PERIOD", "Invalid period" },
	{ 125u, "AIRY_ESCHED_PRIO", "Invalid priority" },
	{ 126u, "AIRY_ESCHED_WEIGHT", "Invalid EEVDF weight" },
	{ 127u, "AIRY_ELIFECYCLE_STATE", "Invalid agent lifecycle state" },
	{ 128u, "AIRY_ELIFECYCLE_TRANS", "Illegal state transition" },
	{ 129u, "AIRY_ELIFECYCLE_AGENT", "Agent not found" },
	{ 130u, "AIRY_ELIFECYCLE_ZOMBIE", "Agent in zombie state" },
	{ 141u, "AIRY_EMEM_TIER", "Invalid memory tier" },
	{ 142u, "AIRY_EMEM_GFP", "Invalid GFP flags" },
	{ 143u, "AIRY_EMEM_PMEM", "PMEM operation failed" },
	{ 144u, "AIRY_EMEM_CXL", "CXL operation failed" },
	{ 145u, "AIRY_EMEM_PAGE_CLASS", "Invalid page classification" },
	{ 146u, "AIRY_EMEM_MMAP", "mmap failed" },
	{ 147u, "AIRY_EMEM_ALLOC", "alloc_pages failed" },
	{ 148u, "AIRY_EMEM_OOM", "Out of memory (agent-scoped)" },
	{ 161u, "AIRY_ECOG_PHASE", "Invalid cognition phase" },
	{ 162u, "AIRY_ECOG_MODE", "Invalid think mode" },
	{ 163u, "AIRY_ECOG_Q16", "Q16.16 overflow/underflow" },
	{ 164u, "AIRY_ECOG_TIMEOUT", "Cognition loop timeout" },
	{ 165u, "AIRY_ECOG_ITERATIONS", "Max think iterations exceeded" },
	{ 166u, "AIRY_ECOG_CONFIDENCE", "Confidence threshold not met" },
	{ 181u, "AIRY_ELOG_RING", "Ring Buffer write failed" },
	{ 182u, "AIRY_ELOG_FULL", "Ring Buffer full" },
	{ 183u, "AIRY_ELOG_LEVEL", "Invalid log level" },
	{ 184u, "AIRY_ELOG_FACILITY", "Invalid facility code" },
	{ 185u, "AIRY_ELOG_PERSIST", "Log persistence failed" },
	{ 186u, "AIRY_ELOG_MAGIC", "Log record magic mismatch" },
	{ 201u, "AIRY_EOBJ_HANDLE", "Invalid object handle" },
	{ 202u, "AIRY_EOBJ_REFCOUNT", "Reference count overflow/underflow" },
	{ 203u, "AIRY_EOBJ_TYPE", "Object type mismatch" },
	{ 204u, "AIRY_EOBJ_GONE", "Object already destroyed" },
	{ 221u, "AIRY_ESYS_NUMBER", "Invalid syscall number" },
	{ 222u, "AIRY_ESYS_ARGS", "Invalid syscall arguments" },
	{ 223u, "AIRY_ESYS_DISABLED", "Syscall disabled in [DSL] mode" },
	{ 224u, "AIRY_ESYS_ABI", "ABI mismatch" },
	{ 241u, "AIRY_EENC_INVALID", "Malformed UTF-8 sequence" },
	{ 242u, "AIRY_EENC_OVERLONG", "Overlong encoding" },
	{ 243u, "AIRY_EENC_SURROGATE", "UTF-16 surrogate code point" },
	{ 244u, "AIRY_EENC_NONCHAR", "Unicode noncharacter" },
	{ 245u, "AIRY_EENC_RANGE", "Code point beyond U+10FFFF" },
	{ 246u, "AIRY_EENC_BOM", "U+FEFF byte order mark" },
	{ 261u, "AIRY_ELOCKDOWN", "Interface blocked at the active lockdown level" },
	{ 4097u, "AIRY_FAULT_CAP_FORGED", "Badge forgery (security breach)" },
	{ 4098u, "AIRY_FAULT_CAP_LEAK", "Capability leak detected" },
	{ 4099u, "AIRY_FAULT_RING_CORRUPT", "IPC ring corruption" },
	{ 4100u, "AIRY_FAULT_TIMEOUT", "Agent heartbeat timeout" },
	{ 4101u, "AIRY_FAULT_ABNORMAL_CAP", "Abnormal capability usage" },
	{ 4102u, "AIRY_FAULT_VM_FAULT", "VM page fault in Agent" },
};

static const struct airy_err_msg *find_msg(unsigned int code)
{
	size_t i;

	for (i = 0; i < sizeof(g_err_msgs) / sizeof(g_err_msgs[0]); i++) {
		if (g_err_msgs[i].code == code)
			return &g_err_msgs[i];
	}

	return NULL;
}

const char *airy_err_msgid(unsigned int code)
{
	const struct airy_err_msg *msg = find_msg(code);

	return msg != NULL ? msg->msgid : NULL;
}

const char *airy_err_text(unsigned int code)
{
	const struct airy_err_msg *msg = find_msg(code);

	return msg != NULL ? msg->text : NULL;
}
