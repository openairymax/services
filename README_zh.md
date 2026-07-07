**语言:** [English](README.md) | 简体中文

# agentrt-liunx 服务态（AirymaxOS Services）

[![Version](https://img.shields.io/badge/version-0.1.1-5a6b7e)](https://atomgit.com/openairymax/services)
[![License](https://img.shields.io/badge/license-AGPL--3.0+Apache--2.0-4a90d9)](LICENSE)

> [agentrt-liunx（AirymaxOS）](https://atomgit.com/openairymax/agentrt-linux)（智能体操作系统）的服务子系统。
> 由 [agentrt-linux](https://atomgit.com/openairymax/agentrt-linux) 管理仓聚合的叶子仓之一。
> 复用并扩展 Airymax `daemons` 模块以提供 OS 级用户态服务。

---

## 概述

**agentrt-liunx 服务态（AirymaxOS Services）**（`airymaxos-services`）是 agentrt-liunx（AirymaxOS）（智能体操作系统）的用户态服务子系统。它实现微内核用户态服务层：将 VFS、网络与驱动从内核移至用户态守护进程，通过 12 个专用守护进程与 systemd 集成，并经 io_uring 消息传递与内核通信。

在 agentrt-liunx 0.1.1 中，本仓库为**文档体系完成**，包含设计文档、参考发行版规范及架构草案。实际的内核与 OS 开发在 1.0.1 版本进行。

### 核心技术

- **VFS / 网络 / 驱动用户态化** 遵循微内核分离原则
- **12 个 systemd 守护进程** 负责服务生命周期、监督与依赖管理
- **io_uring 消息传递** 提供高吞吐、低延迟的内核↔用户 IPC
- **systemd 集成** 对齐参考发行版服务管理标准
- **基于 capability 的服务访问** 委托给 agentrt-liunx 安全态（AirymaxOS Security）子系统

### 与 Airymax daemons 的关系

agentrt-liunx 服务态（AirymaxOS Services）复用并扩展了 Airymax 运行时平台的 `daemons` 模块。守护进程框架、IPC 契约与监督模型在用户态运行时（agentrt）与 OS 级服务层（agentrt-liunx（AirymaxOS））之间共享，确保架构同源、无适配层。

## 仓库结构（0.1.1（文档体系完成））

```
services/
├── README.md           # 本文件（英文）
├── README_zh.md        # 中文翻译
├── LICENSE             # AGPL-3.0 + Apache-2.0 双许可证
├── NOTICE              # 版权、商标与第三方声明
└── .gitignore
```

设计文档与参考发行版规范维护在伞仓的 `docs/AirymaxAgentOS/` 目录。

## 上下游依赖

### 上游

- **agentrt-liunx 内核（AirymaxOS Kernel）** — 提供微内核原语、sched_ext 与 io_uring 入口
- **Airymax daemons** — 提供被复用并扩展的守护进程框架与 IPC 契约
- **Euler 24.03 LTS / 26.03** — systemd 与服务标准参考发行版

### 下游

- **agentrt-liunx 系统态（AirymaxOS System）** — 对服务集合进行打包与配置的系统层
- **agentrt-liunx Cloudnative（AirymaxOS Cloudnative）** — 消费服务面的云原生层

## 分支策略

本叶子仓在 **`feature/official-hubs-01`** 分支上开发。聚合管理仓 `agentrt-linux` 保持在 `main` 分支。

## 许可证

采用 **AGPL v3 + Apache 2.0** 双许可证（SPDX：`AGPL-3.0-or-later OR Apache-2.0`）。完整文本见 [LICENSE](LICENSE)。

Copyright (c) 2025-2026 SPHARX Ltd. All Rights Reserved.
