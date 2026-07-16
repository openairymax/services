# `.github/` — services Leaf Repository Automation

> GitHub automation directory for the **services** leaf repository of
> [agentrt-linux (AirymaxOS)](https://atomgit.com/openairymax/agentrt-linux).

Copyright (c) 2025-2026 SPHARX Ltd. All Rights Reserved.

---

## Positioning

This directory hosts services-specific GitHub automation. Management-level
orchestration (governance file integrity, leaf CI aggregation, nightly soak/chaos,
release SBOM, SSoT validation) runs in the management repository's
`.github/workflows/`; this leaf repository hosts language-level CI for the
user-space daemons (C, shell, systemd unit validation).

## Directory Contents

```
services/.github/
└── README.md    # This file
```

## Applicable Management-Repository Workflows

| Workflow | Jobs | Relevance to services |
|----------|------|------------------------|
| `mgmt-orchestrator.yml` | `file-integrity` + `orchestrate-leaf-ci` | Verifies the `services/` submodule dir exists; aggregates this repo's CI status into the management gate |
| `nightly.yml` | `nightly-test-suite` + `nightly-revert-or-budget` | 72h soak and chaos suites exercise the 12 daemons and io_uring IPC paths |
| `release.yml` | `build-and-sign` + `publish-release` | `syft services/` generates the SPDX SBOM fragment for this repo |
| `ssot-validate.yml` | `ssot-syntax-and-rules` + `ssot-cross-ref` | Validates rule IDs referenced in services documentation |

## Development Guide

- Add leaf-local workflows for daemon unit tests, `shellcheck`, and systemd unit
  linting; keep each workflow ≤ 2 jobs.
- Daemon↔kernel IPC uses io_uring with ABI magic `0x41524531` (`'ARE1'`).
- Daemon functions use the `airy_*` prefix (not legacy `airymaxos_*`).
- Reference authority: `docs/AirymaxOS/70-build-system/03-ci-cd-pipeline.md`.

## License

Dual-licensed under **AGPL v3 + Apache 2.0** (SPDX: `AGPL-3.0-or-later OR Apache-2.0`).
See the repository root [LICENSE](../LICENSE) and [NOTICE](../NOTICE).

Copyright (c) 2025-2026 SPHARX Ltd. All Rights Reserved.
