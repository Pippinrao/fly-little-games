# 仓库收束记录 — 2026-09-21

## 唯一开发入口

当前开发分支和工作目录为 `main` 与仓库根目录。`git worktree list` 只保留该入口。
先完成仓库收束，再以该主干编写最小可玩方案；本次整理不宣称联机功能完成。

| 原分支 | 收束方式 |
|---|---|
| `codex/nearby-ui-acceptance-fixes` | `e782c6d` 保全此前未提交的产品代码、测试和报告，`19068e3` 合入主干 |
| `codex/nearby-dual-link-control` | 已是上述集成分支的祖先，握手代码随之进入主干 |
| `codex/nearby-dual-runtime` | 已是上述集成分支的祖先，输入与运行器代码随之进入主干 |
| `codex/nearby-dual-e2e-harness` | `c4b2e95` 合入剩余测试成果；4 份未跟踪报告进入历史归档 |

分支引用保留作历史定位。版本主线仍为 `1`；整合采用已有最高 MINOR `1.7`，
PATCH 由现有提交钩子递增，Android/Harmony 元数据由版本脚本同步。

## 文件保全与归档

- 73 份旧设计、计划、任务卡、审计和报告归档到
  `docs/archive/nearby-2026-09-21/`，原位置和内容摘要见其 `manifest.json`。
- 原注册 worktree 的 Git 元数据、未提交差异和未跟踪文件快照保存在本地忽略目录
  `out/consolidation-20260921/`，不进入源码提交。
- 三个已退役联机 worktree，以及此前已失去 Git 注册的 `builtin-content`、
  `main-test-repair` 目录完整保存在该目录的 `retired-worktrees/`。
- `nearby-ui-acceptance-fixes` 因 Windows 目录占用保留在原物理位置，已撤销 worktree
  注册并放置 `RETIRED-WORKTREE.txt`；旧 Git 操作会拒绝执行。它不是继续开发的入口。
  其 Git 元数据也已保全，所有分支提交已经在主干中。
- 旧脑暴页面、根目录本地日志和证书输出移入忽略目录的 `local-evidence/`。
- 原本地 Harmony 签名配置和独立研究/编辑器配置保留，不将签名配置混入提交。

## 本次合并处理

版本冲突通过唯一 VERSION 和同步脚本解决；测试时钟保留较新实现中
`suspend_inclusive = 1` 的布尔语义。CMake 两处分支各自注册了相同的
`flynes_two_engine_tamper_matrix`，收束后只保留一处注册，测试源码仍保留。

## 验证边界

整理期间曾启动主机全量构建和 Android 单元测试。Android 单元任务完成；主机构建
按用户要求停止，未取得全量通过结论，后续没有继续启动全量测试。
仓库清理只检查 Git 合并关系、文件保全、归档索引、冲突标记和活动 worktree 数量。
这些检查不认证 Android/Harmony/iOS 产品行为，不认证双机可玩。
