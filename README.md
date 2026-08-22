**Language:** English | [简体中文](README_zh.md)

# services — agent-linux (AirymaxOS) Services

[![Version](https://img.shields.io/badge/version-0.1.1-5a6b7e)](https://atomgit.com/openairymax/services)
[![License](https://img.shields.io/badge/license-AGPL--3.0+Apache--2.0-4a90d9)](LICENSE)

> Services subsystem of [agent-linux (AirymaxOS)](https://atomgit.com/openairymax/agent-linux) — the AI Agent Operating System.
> One of the 8 leaf repositories aggregated by the [agent-linux](https://atomgit.com/openairymax/agent-linux) management repo.
> Reuses and extends the Airymax `daemons` module for OS-level user-space services.

Copyright (c) 2025-2026 SPHARX Ltd. All Rights Reserved.

---

## Positioning

The **services** leaf repository is the user-space services subsystem of
agent-linux (AirymaxOS). It implements the microkernel user-space service
layer: VFS, networking and drivers are moved out of the kernel into user-space
daemons, integrated with systemd via 12 dedicated daemons, and connected to the
kernel through io_uring-based message passing.

## Core Responsibilities

- **VFS / networking / driver user-space migration** following the microkernel separation principle.
- **12 systemd daemons** for service lifecycle, supervision and dependency management (`gateway_d`, `llm_d`, `tool_d`, `sched_d`, `market_d`, `monit_d`, …).
- **io_uring message passing** for high-throughput, low-latency kernel↔user IPC.
- **systemd integration** aligned with Euler-standard service management.
- **Capability-based service access** delegating to the `security` subsystem.

## Relationship with Airymax `daemons`

The services leaf repo reuses and extends the `daemons` module from the Airymax
runtime platform. The daemon framework, IPC contracts and supervision model are
shared between the user-space runtime (`agentrt`) and the OS-level service layer,
ensuring architectural homology with no adaptation layer.

## Document & File List

```
services/
├── README.md           # This file (English)
├── README_zh.md        # Chinese translation
├── LICENSE             # AGPL-3.0 + Apache-2.0 dual license
├── NOTICE              # Copyright, trademark and third-party notices
├── .gitignore
└── .github/
    └── README.md       # GitHub automation for this leaf repo
```

Design documents and reference distribution specifications are maintained in the
`docs/AirymaxOS/` directory of the umbrella documentation repository.

## CI Status

Services changes are governed by management-repository workflows (each ≤ 2 jobs):

| Workflow | Jobs | Applies to services via |
|----------|------|--------------------------|
| `mgmt-orchestrator.yml` | `file-integrity` (8 submodules + governance files) + `orchestrate-leaf-ci` (aggregate leaf CI, markdownlint, copyright) | Aggregates this leaf repo's CI status |
| `nightly.yml` | `nightly-test-suite` (soak + chaos exercise the service set) + `nightly-revert-or-budget` | Nightly cron |
| `release.yml` | `build-and-sign` (SBOM scan of `services/`) + `publish-release` | Release tag |
| `ssot-validate.yml` | `ssot-syntax-and-rules` + `ssot-cross-ref` | When docs reference service rules |

Language-level CI (C / shell / unit tests) is delegated to this leaf repository's
own `.github/workflows/`.

## Development Guide

- **Branch**: `feature/official-hubs-01` (the management repo stays on `main`).
- **DCO**: every commit must be `Signed-off-by` (`git commit -s`).
- **Commit prefix**: `services:`.
- **Code style**: C — tab-8, 80 cols (`.clang-format`); shell — `shellcheck`; run `make format-check`.
- **IPC contract**: daemon↔kernel IPC uses io_uring; ABI magic `0x41524531` (`'ARE1'`).
- **Function prefix**: user-space daemons use the `airy_*` prefix.

## Upstream & Downstream

- **Upstream** — `kernel` (micro-core primitives, sched_tac, io_uring); Airymax `daemons`; Euler 24.03 LTS / 26.03 standards.
- **Downstream** — `system` (packages and configures the service set); `cloudnative` (consumes the service surface).

## License

Dual-licensed under **AGPL v3 + Apache 2.0** (SPDX: `AGPL-3.0-or-later OR Apache-2.0`).
See [LICENSE](LICENSE) for the full text.

Copyright (c) 2025-2026 SPHARX Ltd. All Rights Reserved.
