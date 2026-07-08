**Language:** English | [简体中文](README_zh.md)

# agentrt-liunx Services (AirymaxOS Services)

[![Version](https://img.shields.io/badge/version-0.1.1-5a6b7e)](https://atomgit.com/openairymax/services)
[![License](https://img.shields.io/badge/license-AGPL--3.0+Apache--2.0-4a90d9)](LICENSE)

> Services subsystem of [agentrt-liunx（AirymaxOS）](https://atomgit.com/openairymax/agentrt-linux) — the AI Agent Operating System.
> One of the leaf repositories aggregated by the [agentrt-linux](https://atomgit.com/openairymax/agentrt-linux) management repo.
> Reuses and extends the Airymax `daemons` module for OS-level user-space services.

---

## Overview

The **agentrt-liunx Services (AirymaxOS Services)** (`airymaxos-services`) is the user-space services subsystem of agentrt-liunx（AirymaxOS）, the AI Agent Operating System. It implements the microkernel user-space service layer: VFS, networking and drivers are moved out of the kernel into user-space daemons, integrated with systemd via 12 dedicated daemons, and connected to the kernel through io_uring-based message passing.

In agentrt-liunx 0.1.1, this repository is **documentation complete** (文档体系完成) containing design documents, reference distribution specifications, and architectural drafts. Actual kernel and OS development takes place in version 1.0.1.

### Core Technologies

- **VFS / networking / driver user-space migration** following the microkernel separation principle
- **12 systemd daemons** for service lifecycle, supervision and dependency management
- **io_uring message passing** for high-throughput, low-latency kernel↔user IPC
- **systemd integration** aligned with reference distribution service management standards
- **Capability-based service access** delegating to the agentrt-liunx Security (AirymaxOS Security) subsystem

### Relationship with Airymax daemons

The agentrt-liunx Services (AirymaxOS Services) reuses and extends the `daemons` module from the Airymax runtime platform. The daemon framework, IPC contracts and supervision model are shared between the user-space runtime (agentrt) and the OS-level service layer (agentrt-liunx（AirymaxOS）), ensuring architectural homology with no adaptation layer.

## Repository Structure (0.1.1 Documentation Complete)

```
services/
├── README.md           # This file (English)
├── README_zh.md        # Chinese translation
├── LICENSE             # AGPL-3.0 + Apache-2.0 dual license
├── NOTICE              # Copyright, trademark and third-party notices
└── .gitignore
```

Design documents and reference distribution specifications are maintained in the `docs/AirymaxAgentOS/` directory of the umbrella repository.

## Upstream & Downstream Dependencies

### Upstream

- **agentrt-liunx Kernel (AirymaxOS Kernel)** — provides the micro-core primitives, sched_ext and io_uring entry points
- **Airymax daemons** — provides the daemon framework and IPC contracts that are reused and extended
- **Euler 24.03 LTS / 26.03** — reference distribution for systemd and service standards

### Downstream

- **agentrt-liunx System（AirymaxOS System）** — system layer that packages and configures the service set
- **agentrt-liunx Cloudnative（AirymaxOS Cloudnative）** — cloud-native layer that consumes the service surface

## Branch Strategy

This leaf repository is developed on **`feature/official-hubs-01`**. The aggregating `agentrt-linux` management repo stays on `main`.

## License

Dual-licensed under **AGPL v3 + Apache 2.0** (SPDX: `AGPL-3.0-or-later OR Apache-2.0`). See [LICENSE](LICENSE) for the full text.

Copyright (c) 2025-2026 SPHARX Ltd. All Rights Reserved.
