> 历史归档（2026-09-21）：本文的执行顺序、首版范围和进度结论已被[当前最小可玩方案](../../../../nearby/README.md)取代。保留供追溯，不作为新增任务或发布承诺。

# 附近联机三端统一 UX — 验收记录（2026-09-13）

分支 `codex/main-test-repair`，基线 `3086a3f`，设计基线见
[设计](../superpowers/specs/2026-09-13-nearby-ui-parity-design.md)，实现按
[实现计划](../superpowers/plans/2026-09-13-nearby-ui-parity-implementation-plan.md)
P1–P8 逐任务提交（见 git log：P1 `2c8d6ca`、P2 `cab8d7d`、P3 `ec7b54c`、P4
`a84e18a`、P5 `e07558b`、P6 `3da4105`）。原始日志在 ignored
`out/evidence/nearby-ui-parity/`。

## 1. 已验证项（含环境）

| 项 | 环境 | 结果 |
|---|---|---|
| P1 合同+投影 | Windows host，shared CTest | 50/50（含 `nearby_product_ui`）+ 合同检查器 OK（112 keys × 3 端 zh 相等）|
| P2 邀请查找 route/wire | Windows host | 51/51 CTest（向量 1–8 全过；golden 41 组；schema registry 四树一致）|
| P3 能力筛选 | Windows host + Android JVM | shared 52/52；Android 单测 454/454（含 fixture 回放）|
| P4 Android UI | 3 台模拟器（en locale） | 全量 instrumented 140 例；除 4 例 GPU 门禁用例（见 §3）全部通过；新增 NearbyUiParity/NearbyInviteCode/HomeMultiplayerFilter 10 例 |
| P5 Harmony | Host CTest + 模拟器 127.0.0.1:5557 | Host 12/12；HarmonyEmulator gate PASS，Hypium 33/33（含 2 条新向量）|
| 合同资源一致性 | 检查器 | `check_nearby_ui_contract.py` OK（默认+zh 三端键覆盖、zh 文本相等、C01–C18 fixture 在位）|

## 2. 未验证/不支持项（明确声明，不冒充完成）

- **iOS 模拟器证据已补齐**（2026-09-13 晚，ssh apple / macOS 13.7.8 / Xcode 14.3.1 /
  iPhone 14 模拟器 / iOS 16.4 / en-US）：产品构建 BUILD SUCCEEDED；
  FlyNESUITests **TEST EXECUTE SUCCEEDED**（含 3 例 NearbyUiParityTests：N00 三动作、
  C05 输入门禁+前导零、N01 生命周期重生成/取消）；FlyNESRuntimeTests
  **TEST EXECUTE SUCCEEDED**。运行中发现并修复 3 个问题（Registry init、
  N00 动作按 content-priority 移到设备页顶部、XCUITest 多类型查询/聚焦）。
  **真机限制**：Xcode 14.3.1 无法向 iPhone 16 Pro Max（iOS 18）部署，真机阶段前
  需升级 Xcode 16.x。
- **实体机配对矩阵未验证**：Android↔Harmony、Android↔iOS、Harmony↔iOS 及三种
  同平台组合、双入口完整闭环（N00→验证→大厅→选游戏→双方确认→双机游戏）——
  需要实体设备与签名配置，全部记为未验证。
- **GPU 门禁用例**：MotionComputeParityTest / MotionShadowPresenterTest 4 例在
  三台模拟器上均 EGL_CONTEXT_UNAVAILABLE（host 无可用 GPU EGL）；本分支 diff
  不含任何 video/Motion 文件（`git diff 3475d5c..HEAD --stat` 为证）。需在
  GPU-enabled 模拟器或实体设备复跑。
- **物理刷新率/功耗/时延/温度**：模拟器证据不认证这些项（仓库政策）。
- **端到端会话阶段**：发现承载、真实房主接受、SAS、QUIC 建网、N09 配置确认、
  N11 文件补齐的设备级闭环在 backend 轨道/P8；本分支 UI 在无承载时如实显示
  阻塞原因，不伪造成功。

## 3. P7 三端同用例对比状态

`tools/quality/check_nearby_ui_parity.py` 已交付并在当前证据目录运行：输出
`3 missing, 0 semantic mismatch, 0 layout violations`（退出码 1）。该工具按设计
在缺任一端原生证据时 FAIL 而非跳过；三端 `nearby-parity-<platform>.json` 齐备后
才会给出 0/0/0 结论。当前"三端同结果"由共享投影 + 相同 fixture + 各端在测用例
支撑，尚无原生证据矩阵。

## 4. 安装限制与签名声明

- Harmony 模拟器使用 unsigned HAP 安装（门禁已有 sign-info 检查并要求先卸载旧
  签名包）；物理设备必须使用既有签名配置（P8），未输出任何 profile/密码。
- Android 使用 debug 签名（模拟器 instrumentation）；测试签名不称为生产证书。
- iOS 未安装任何包。

## 5. 结论

按 [实现计划](../superpowers/plans/2026-09-13-nearby-ui-parity-implementation-plan.md)
的完成标准衡量：**尚未达到"三端用例同结果 + 已声明支持组合的实体机闭环"**。
已交付并验证的是：共享合同与投影（P1）、邀请查找协议/route/ABI（P2）、能力筛选
（P3）、Android 页面接入与模拟器证据（P4）、Harmony Host+模拟器证据（P5）、iOS
代码接入（未执行）、P7 工具。剩余缺口集中在：macOS 上的 iOS 执行、三端原生证据
矩阵（P7 输入）、实体机配对闭环（P8）。
