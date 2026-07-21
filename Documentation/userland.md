# 用户态子系统

> 本文档描述 services 子仓中用户态 VFS / Net / Drivers / IPC 子系统的职责。

Copyright (c) 2025-2026 SPHARX Ltd. All Rights Reserved.

License: GPL-2.0-only

---

## 1. 子系统清单

| 子系统 | 目录 | 职责 | IRON-9 主层 |
|--------|------|------|-------------|
| VFS | `userland/vfs/` | 用户态 VFS（FUSE 集成） | [IND] |
| Net | `userland/net/` | 用户态网络栈（DPDK / XDP） | [IND] |
| Drivers | `userland/drivers/` | 用户态驱动（VFIO） | [IND] |
| IPC | `userland/ipc/` | io_uring IPC 用户态库 | [IND] |

## 2. 与 kernel 子系统的关系

用户态子系统与 kernel 子仓中的对应模块互补：

- `userland/vfs/` ← 互补 → `kernel/fs/airy/`
- `userland/net/` ← 互补 → `kernel/net/airy/`
- `userland/drivers/` ← 互补 → `kernel/drivers/airy/`
- `userland/ipc/` ← 同源 → `kernel/ipc/airy/`（共享 [SC] ipc.h）

## 3. 0.1.1 实现状态

- ⏳ 所有用户态子系统：1.0.1 补齐

## 4. 相关文档

- [07-directory-structure.md](../../../docs/AirymaxOS/10-architecture/07-directory-structure.md) §4.2 — 完整目录结构
- [ipc.h](../../kernel/include/uapi/linux/airymax/ipc.h) — [SC] IPC 契约
