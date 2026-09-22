# 2026-09-23 三端 Nearby 最小同步验证

范围：只验证 Android、HarmonyOS NEXT、iOS 对同一 `fly_lan_mvp` owner 的当前接线同步，
以及现有 Nearby UX 没有被新增页面或流程替换。本记录不把六方向跨端试玩、真机摄像头、
热点、触控、扬声器或完整恢复矩阵写成已完成。

## 通过

- 内容：`tools/content/verify-builtin-content.ps1`，7 个内置游戏，单一来源门禁通过。
- QUIC：`cargo test --manifest-path shared/nearby-quic-provider/Cargo.toml`，30/30。
- 共享 LAN MVP：Windows host 构建后运行 `ctest -R ^flynes_lan_mvp_`，11/11。
- Android：`:app:testDebugUnitTest :app:assembleDebug :app:assembleDebugAndroidTest` 通过；
  `NearbyPairingTest` 排除需要外部 Harmony 对端的专用驱动后全套 41/41，通过新增真实邀请
  guest/P2 用例；客机游戏行不可点击。
- Harmony host：完整 CTest 14/14；产品 `assembleHap` 通过（未签名）。
- iOS：`ios/scripts/build_simulator.sh` 通过，产物为 x86_64 simulator app；
  `NearbyMvpBridgeTests/testHostPublishesSharedInviteAsPlayerOne` 通过；Nearby/UX 相关 UI 14/14；
  `run_product_simulator.sh` 安装并启动 `com.flynes.app` 成功。

## 明确未通过或未执行

- Harmony `entry@ohosTest assembleHap` 在执行测试前被既有测试源的 29 处 ArkTS
  `arkts-no-any-unknown` / `arkts-no-implicit-return-types` 错误阻塞；新增
  `NearbyMvpHost.test.ets` 不在报错列表。工程无 `signingConfigs`，未安装本次 HAP。
- iOS 完整 RuntimeTests 为 51/52；唯一失败是既有
  `NearbySessionV2BridgeTests.testMissingDiscoveryKeepsTheProductionV2OwnerFailClosed`，其断言等待
  尚未实现的 `nearbySessionSnapshotV2` selector。新增 LAN MVP bridge 定向测试通过。
- Android `crossAppHostWaitsForHarmonyGuest` 在未启动 Harmony 客体时按设计等待并报
  “Harmony guest did not join”；它不是单端回归用例，未计入 7/7。
- A→H、H→A、A→I、I→A、H→I、I→H 的本轮跨端产品试玩未执行；不得据本记录推断通过。
- Android/iOS 产品尚未接入相机取景；测试只注入平台扫码回调的邀请载荷。新增房主方向
  自动选择首个合格内置游戏，未验证导入 ROM 对称查找或新增方向的同连接换游戏。
