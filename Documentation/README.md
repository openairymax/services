# services 子仓文档索引

> **子仓定位**：AirymaxOS 用户态服务层 —— 12 daemon + 用户态 VFS/Net/Drivers/IPC + systemd 集成。

Copyright (c) 2025-2026 SPHARX Ltd. All Rights Reserved.

License: GPL-2.0-only

---

## 文档清单

### 已有文档

| 文档 | 说明 |
|------|------|
| [daemons.md](daemons.md) | 12 daemon 详解（macro_d / logger_d / config_d / ...） |
| [userland.md](userland.md) | 用户态子系统（VFS / Net / Drivers / IPC） |

### 待编写文档（0.1.1 → 1.0.1）

| 文档 | 说明 | 计划版本 |
|------|------|---------|
| `systemd.md` | 12 systemd unit 文件与启动顺序 | 0.1.1 |
| `macro_d-design.md` | Macro-Supervisor 用户态裁决设计 | 0.1.1 |
| `gateway_d-design.md` | 网关服务与 JSON-RPC 2.0 | 0.1.1 |
| `sched_d-design.md` | 调度服务与 stc_* 策略 | 0.1.1 |
| `ipc-userland.md` | io_uring IPC 用户态库设计 | 0.1.1 |

## 相关文档

- [07-directory-structure.md](../../../docs/AirymaxOS/10-architecture/07-directory-structure.md) §4.2 — services 子仓完整目录结构
- [04-engineering-philosophy.md](../../../docs/AirymaxOS/50-engineering-standards/04-engineering-philosophy.md) — IRON-1~15 工程铁律
- [05-development-process.md](../../../docs/AirymaxOS/50-engineering-standards/05-development-process.md) — 开发流程
- 顶层 [CONTRIBUTING.md](../CONTRIBUTING.md) — services 子仓贡献指南
