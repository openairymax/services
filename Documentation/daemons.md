# 12 daemon 详解

> 本文档描述 services 子仓中 12 个 daemon 的职责、依赖与启动顺序。

Copyright (c) 2025-2026 SPHARX Ltd. All Rights Reserved.

License: GPL-2.0-only

---

## 1. daemon 清单

| # | daemon | 职责 | IRON-9 主层 | 启动顺序 |
|---|--------|------|-------------|---------|
| 1 | `macro_d` | Macro-Supervisor 用户态裁决 | [IND] | 1 |
| 2 | `gateway_d` | 网关服务（JSON-RPC 2.0） | [SS] | 1 |
| 3 | `sched_d` | 调度服务（stc_* 策略） | [SS] | 1 |
| 4 | `logger_d` | 日志服务（128B 记录 + PMEM 持久化） | [IND] | 2 |
| 5 | `vfs_d` | VFS 服务 | [IND] | 2 |
| 6 | `net_d` | 网络服务 | [IND] | 2 |
| 7 | `cogn_d` | 认知服务（CoreLoopThree 调度） | [SS] | 2 |
| 8 | `dev_d` | 设备/工具服务 | [IND] | 3 |
| 9 | `config_d` | 配置服务（YAML + inotify 热加载） | [IND] | 3 |
| 10 | `mem_d` | 记忆服务（L1-L4 分层管理） | [IND] | 4 |
| 11 | `audit_d` | 审计服务（哈希链） | [IND] | 4 |
| 12 | `sec_d` | 安全服务（Capability + Vault） | [IND] | 5 |

## 2. 0.1.1 实现状态

- ✅ `macro_d`：最小可编译骨架（决策 F1）
- ⏳ 其余 11 daemon：1.0.1 补齐

## 3. macro_d 详细设计

### 3.1 主循环

```
main(argc, argv)
  → 解析 eventfd 参数
  → macro_main_loop(efd)
      → epoll_create1()
      → epoll_ctl(ADD, efd, EPOLLIN)
      → while (running):
          epoll_wait()
          → read(efd, &val)
          → macro_read_fault_code(&fault)
          → macro_adjudicate(fault) → verdict
          → macro_execute(verdict, fault)
```

### 3.2 裁决矩阵

| 故障码 | 裁决动作 | 说明 |
|--------|---------|------|
| `AIRY_FAULT_CAP_FORGED` | TERMINATE | 安全违规，立即终止 |
| `AIRY_FAULT_CAP_LEAK` | TERMINATE | Capability 泄漏 |
| `AIRY_FAULT_RING_CORRUPT` | PAUSE | 通信故障，暂停恢复 |
| `AIRY_FAULT_TIMEOUT` | DEGRADE | 超时，降级运行 |
| `AIRY_FAULT_ABNORMAL_CAP` | WARN | 异常使用，警告 |
| `AIRY_FAULT_VM_FAULT` | WARN | VM 页错误，警告 |

## 4. 相关文档

- [07-directory-structure.md](../../../docs/AirymaxOS/10-architecture/07-directory-structure.md) §4.2 — 完整目录结构
- [07-directory-structure.md](../../../docs/AirymaxOS/10-architecture/07-directory-structure.md) 附录 B — 12 daemon 完整源文件清单
- [error.h](../../kernel/include/uapi/linux/airymax/error.h) — [SC] 故障码定义
