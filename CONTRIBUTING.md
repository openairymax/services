# 贡献指南 - services 子仓（用户态服务）

> **治理依据**：[`docs/AirymaxOS/50-engineering-standards/07-maintainers-and-governance.md`](https://github.com/openairymax/docs/blob/main/AirymaxOS/50-engineering-standards/07-maintainers-and-governance.md)
> **开发流程**：[`docs/AirymaxOS/50-engineering-standards/05-development-process.md`](https://github.com/openairymax/docs/blob/main/AirymaxOS/50-engineering-standards/05-development-process.md)
> **工程哲学**：[`docs/AirymaxOS/50-engineering-standards/04-engineering-philosophy.md`](https://github.com/openairymax/docs/blob/main/AirymaxOS/50-engineering-standards/04-engineering-philosophy.md)

Copyright (c) 2025-2026 SPHARX Ltd. All Rights Reserved.

License: GPL-2.0-only

---

## 1. 子仓职责

services 子仓负责 AirymaxOS 的全部用户态服务，包括：

- **12 daemon**（统一 `_d` 后缀，决策 C1）：
  - `macro_d`（Macro-Supervisor 用户态裁决，[IND]）
  - `logger_d` / `config_d` / `gateway_d` / `sched_d` / `vfs_d` / `net_d`
  - `mem_d` / `cogn_d` / `sec_d` / `audit_d` / `dev_d`
- **用户态子系统**：VFS（FUSE）/ Net（DPDK/XDP）/ Drivers（VFIO）/ IPC（io_uring 用户态库）
- **systemd 集成**：12 个 unit 文件 + `agentrt.target`

### IRON-9 主层

- `[SS]`：`gateway_d` / `sched_d` / `cogn_d`（与 agentrt gateway/atoms/sched 同源 OS 升级）
- `[IND]`：其余 daemon 与用户态子系统（agentrt-linux 专属实现）

## 2. 开发环境要求

- **C 标准**：C11（用户态 daemon）
- **编译器**：gcc ≥ 11 或 clang ≥ 14
- **构建系统**：CMake ≥ 3.20
- **内核头**：Linux 6.6+ UAPI（通过 `-I../kernel/include` 引用 [SC] 头文件）
- **依赖**：libsystemd（systemd 集成）、libfuse3（VFS）、liburing（IPC）

### [SC] 头文件引用约束（OS-IRON-014）

- [SC] 头文件**唯一物理宿主**：`../kernel/include/uapi/linux/airymax/`
- 本子仓通过 `-I` 引用，**禁止物理副本**
- CMake 配置：
  ```cmake
  include_directories(${CMAKE_SOURCE_DIR}/../kernel/include)
  include_directories(${CMAKE_SOURCE_DIR}/../kernel/include/uapi/linux)
  ```

## 3. 构建命令

```bash
# 从子仓根目录构建
cd services
mkdir build && cd build
cmake ..
make -j$(nproc)

# 构建产物
#   daemons/macro_d/macro_d
```

## 4. 代码规范

### 4.1 C 代码风格

- **OS-STD-FMT-001**：Tab-8 缩进（由 `.clang-format` 强制）
- **OS-STD-FMT-002**：80 列硬限制
- **GPL-2.0-only**：所有 `.c` / `.h` 文件必须包含 `SPDX-License-Identifier: GPL-2.0-only`
- **版权头**：`Copyright (c) 2025-2026 SPHARX Ltd.`
- 提交前运行 `clang-format -i` 格式化

### 4.2 注释规范

- 使用**中文注释**（用户偏好）
- 文件头注释：文件职责 + SPDX + 版权
- 函数注释：参数说明 + 返回值 + 副作用

## 5. 提交规范

### 5.1 DCO 签名（OS-IRON-007 / OS-KER-068）

所有提交必须包含 `Signed-off-by:` 行：

```bash
git commit -s
```

### 5.2 提交信息格式（OS-STD-PROD-031）

```
services: 简短描述（≤72 字符）

详细说明 what 和 why，72 字符换行。

Fixes: <commit-sha> ("原始提交描述")  # 修复 bug 时
Closes: #<issue-number>              # 关闭 issue 时

Signed-off-by: Your Name <your.email@example.com>
```

### 5.3 子系统前缀

| 前缀 | 范围 |
|------|------|
| `services:` | services/ 子仓整体 |
| `services: macro_d:` | macro_d daemon |
| `services: gateway_d:` | gateway_d daemon |
| `services: userland:` | 用户态 VFS/Net/Drivers/IPC |
| `services: systemd:` | systemd unit 文件 |

## 6. 分支策略

- **开发分支**：`feature/official-hubs-01`
- 从 `feature/official-hubs-01` 创建 topic 分支
- PR 目标分支：`feature/official-hubs-01`

## 7. 测试要求

- **OS-STD-TEST-***：所有变更必须包含或更新测试
- 单元测试：随代码放在各 daemon 目录
- 集成测试：在 `tests-linux/` 子仓
- daemon 启动顺序测试：见 `tests-linux/integration/test_daemon_startup.c`

## 8. 审查流程

1. 向 `feature/official-hubs-01` 提交 PR
2. CI 运行：SSoT 校验 + CMake 构建 + clang-format 检查 + 测试
3. 至少一名维护者审批
4. Squash-merge

## 9. 报告问题

- **Bug**：在 [services issues](https://github.com/openairymax/services/issues) 提交
- **安全漏洞**：参见顶层 SECURITY.md
- **设计讨论**：使用管理仓的 GitHub Discussions
