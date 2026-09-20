> 历史归档（2026-09-21）：本文的执行顺序、首版范围和进度结论已被[当前最小可玩方案](../../../../../nearby/README.md)取代。保留供追溯，不作为新增任务或发布承诺。

# 当前测试资产清单与接入审计

日期：2026-09-13；基线：`661e84bc97023676882d5f8ffbbd97634d25e8b3`（main，1.1.12）。

本文是[测试增强计划](2026-09-13-full-functional-test-enhancement-plan.md)的源码审计附录。数字来自当前源码声明，未代表本轮执行通过数或代码覆盖率。测试数量采用 Java `@Test` 方法、ArkTS `it`、Objective-C XCTest `test…` 方法；独立 main 程序不按 XCTest 方法计数。

## 1. 统计总览

| 层级 | 当前静态数量 | 接入边界 |
|---|---:|---|
| Android JVM | 93 文件 / 448 方法 | `:app:testDebugUnitTest`；实际报告仍需关联同一 SHA |
| Android instrumentation | 49 文件 / 133 方法 | 混合组件、原生集成、UI 与真机认证，不能整体标成 E2E |
| Harmony Hypium | 8 用例文件 / 33 个 it | `List.test.ets` 导入；32 个逻辑用例及 1 个 scaffold，无页面 Driver 操作 |
| iOS Runtime XCTest | 7 文件 / 50 方法 | 仅 simulator + `FLYNES_IOS_BUILD_XCTESTS=ON` 注册 |
| iOS UI XCTest | 2 文件 / 13 方法 | `ProductUITests` 10 + `ProductImportUITests` 3；其中 nearby 页面 5 个 |
| core CTest | 2 项声明 | core_smoke、temporal_interpolator |
| shared CTest | 49 常规 + 1 条件项 | overlay policy 仅设置 NDK 路径时注册 |
| Harmony host CTest | 12 项声明 | 与设备 Hypium 分开；不是页面 E2E |
| iOS host CTest | 9 项声明 | 一个原生 portability 程序及 8 个 Python 脚本 |

已有 Android JVM XML 的 448 个结果含 2 个 symlink 平台条件 skip，不是 448 个成功执行；两项为 `loaderRejectsSymlinkRootWhenSupported`、`loaderRejectsSymlinkAliasesWhenSupported`。应在具备对应文件系统能力的作业补跑。默认 Android runner 排除 `@DeviceCertification`，源码中两类共 3 个认证用例不能算普通模拟器执行。

## 2. Android JVM 单元测试

| 测试文件 | 声明数 |
|---|---:|
| [AudioPumpTest](../../../../../../app/src/test/java/com/flynes/emu/AudioPumpTest.java) | 3 |
| [BuildSmokeTest](../../../../../../app/src/test/java/com/flynes/emu/BuildSmokeTest.java) | 1 |
| [AndroidRetryableMigrationLogTest](../../../../../../app/src/test/java/com/flynes/emu/catalog/android/AndroidRetryableMigrationLogTest.java) | 1 |
| [AndroidUuidSafMapTest](../../../../../../app/src/test/java/com/flynes/emu/catalog/android/AndroidUuidSafMapTest.java) | 2 |
| [FncaNativeMigratorTest](../../../../../../app/src/test/java/com/flynes/emu/catalog/android/FncaNativeMigratorTest.java) | 2 |
| [NativeCatalogProjectorTest](../../../../../../app/src/test/java/com/flynes/emu/catalog/android/NativeCatalogProjectorTest.java) | 7 |
| [Api24ProductionGuardTest](../../../../../../app/src/test/java/com/flynes/emu/catalog/Api24ProductionGuardTest.java) | 1 |
| [DomainModelV2Test](../../../../../../app/src/test/java/com/flynes/emu/catalog/DomainModelV2Test.java) | 8 |
| [GameCatalogTest](../../../../../../app/src/test/java/com/flynes/emu/catalog/GameCatalogTest.java) | 10 |
| [HexEncodingTest](../../../../../../app/src/test/java/com/flynes/emu/catalog/HexEncodingTest.java) | 1 |
| [LegacyLibraryMigratorTest](../../../../../../app/src/test/java/com/flynes/emu/catalog/migration/LegacyLibraryMigratorTest.java) | 3 |
| [CatalogRepositoryTest](../../../../../../app/src/test/java/com/flynes/emu/catalog/persistence/CatalogRepositoryTest.java) | 17 |
| [RomCompatibilityContractTest](../../../../../../app/src/test/java/com/flynes/emu/catalog/RomCompatibilityContractTest.java) | 1 |
| [BoundedZipDescriptorTest](../../../../../../app/src/test/java/com/flynes/emu/catalog/scan/BoundedZipDescriptorTest.java) | 2 |
| [BoundedZipMemoryStressTest](../../../../../../app/src/test/java/com/flynes/emu/catalog/scan/BoundedZipMemoryStressTest.java) | 1 |
| [BoundedZipOpenFixtureParityTest](../../../../../../app/src/test/java/com/flynes/emu/catalog/scan/BoundedZipOpenFixtureParityTest.java) | 4 |
| [BoundedZipPayloadFixtureParityTest](../../../../../../app/src/test/java/com/flynes/emu/catalog/scan/BoundedZipPayloadFixtureParityTest.java) | 3 |
| [ContentIdentityFixtureParityTest](../../../../../../app/src/test/java/com/flynes/emu/catalog/scan/ContentIdentityFixtureParityTest.java) | 2 |
| [RomPackageScannerTest](../../../../../../app/src/test/java/com/flynes/emu/catalog/scan/RomPackageScannerTest.java) | 21 |
| [RomPayloadParserFixtureParityTest](../../../../../../app/src/test/java/com/flynes/emu/catalog/scan/RomPayloadParserFixtureParityTest.java) | 1 |
| [UnsupportedPayloadClassifierFixtureParityTest](../../../../../../app/src/test/java/com/flynes/emu/catalog/scan/UnsupportedPayloadClassifierFixtureParityTest.java) | 7 |
| [DocumentLocatorShapeTest](../../../../../../app/src/test/java/com/flynes/emu/catalog/source/DocumentLocatorShapeTest.java) | 6 |
| [SourceEnumeratorTest](../../../../../../app/src/test/java/com/flynes/emu/catalog/source/SourceEnumeratorTest.java) | 3 |
| [SourceRegistryTest](../../../../../../app/src/test/java/com/flynes/emu/catalog/source/SourceRegistryTest.java) | 8 |
| [SourceRelativePathTest](../../../../../../app/src/test/java/com/flynes/emu/catalog/source/SourceRelativePathTest.java) | 2 |
| [ZipEntryNameDecoderTest](../../../../../../app/src/test/java/com/flynes/emu/catalog/ZipEntryNameDecoderTest.java) | 3 |
| [CoverCaptureCoordinatorTest](../../../../../../app/src/test/java/com/flynes/emu/cover/CoverCaptureCoordinatorTest.java) | 2 |
| [DirectionFeedbackGateTest](../../../../../../app/src/test/java/com/flynes/emu/DirectionFeedbackGateTest.java) | 5 |
| [GameCenterStateTest](../../../../../../app/src/test/java/com/flynes/emu/gamecenter/GameCenterStateTest.java) | 8 |
| [GameTitlePresentationTest](../../../../../../app/src/test/java/com/flynes/emu/gamecenter/GameTitlePresentationTest.java) | 4 |
| [HomeHeaderLayoutPolicyTest](../../../../../../app/src/test/java/com/flynes/emu/gamecenter/HomeHeaderLayoutPolicyTest.java) | 2 |
| [GameTitleLocalizerTest](../../../../../../app/src/test/java/com/flynes/emu/GameTitleLocalizerTest.java) | 3 |
| [ContinuousJoystickSessionTest](../../../../../../app/src/test/java/com/flynes/emu/input/ContinuousJoystickSessionTest.java) | 15 |
| [ControlLayoutV2Test](../../../../../../app/src/test/java/com/flynes/emu/input/ControlLayoutV2Test.java) | 5 |
| [ControlVisualGeometryTest](../../../../../../app/src/test/java/com/flynes/emu/input/ControlVisualGeometryTest.java) | 1 |
| [DirectionSessionTest](../../../../../../app/src/test/java/com/flynes/emu/input/DirectionSessionTest.java) | 16 |
| [GamepadHitMapTest](../../../../../../app/src/test/java/com/flynes/emu/input/GamepadHitMapTest.java) | 6 |
| [GamepadInputStateTest](../../../../../../app/src/test/java/com/flynes/emu/input/GamepadInputStateTest.java) | 8 |
| [HapticPatternTest](../../../../../../app/src/test/java/com/flynes/emu/input/HapticPatternTest.java) | 3 |
| [InputRouterTest](../../../../../../app/src/test/java/com/flynes/emu/input/InputRouterTest.java) | 3 |
| [JoystickDirectionTest](../../../../../../app/src/test/java/com/flynes/emu/input/JoystickDirectionTest.java) | 2 |
| [MinimumTapTest](../../../../../../app/src/test/java/com/flynes/emu/input/MinimumTapTest.java) | 3 |
| [JoystickReturnAnimationTest](../../../../../../app/src/test/java/com/flynes/emu/JoystickReturnAnimationTest.java) | 4 |
| [ExactRomLoaderTest](../../../../../../app/src/test/java/com/flynes/emu/launch/ExactRomLoaderTest.java) | 45 |
| [LaunchCoordinatorTest](../../../../../../app/src/test/java/com/flynes/emu/launch/LaunchCoordinatorTest.java) | 21 |
| [StateHeaderReaderTest](../../../../../../app/src/test/java/com/flynes/emu/save/StateHeaderReaderTest.java) | 2 |
| [EmulationSessionTest](../../../../../../app/src/test/java/com/flynes/emu/session/EmulationSessionTest.java) | 3 |
| [SessionSchemaRegistryTest](../../../../../../app/src/test/java/com/flynes/emu/session/SessionSchemaRegistryTest.java) | 1 |
| [ControlLayoutRepositoryTest](../../../../../../app/src/test/java/com/flynes/emu/settings/ControlLayoutRepositoryTest.java) | 3 |
| [FlySettingsMapperTest](../../../../../../app/src/test/java/com/flynes/emu/settings/FlySettingsMapperTest.java) | 2 |
| [MotionRiskConsentStoreTest](../../../../../../app/src/test/java/com/flynes/emu/settings/MotionRiskConsentStoreTest.java) | 3 |
| [NativeSettingsStoreTest](../../../../../../app/src/test/java/com/flynes/emu/settings/NativeSettingsStoreTest.java) | 2 |
| [SettingsBatchTest](../../../../../../app/src/test/java/com/flynes/emu/settings/SettingsBatchTest.java) | 2 |
| [SettingsRepositoryTest](../../../../../../app/src/test/java/com/flynes/emu/settings/SettingsRepositoryTest.java) | 5 |
| [SettingsSectionTest](../../../../../../app/src/test/java/com/flynes/emu/settings/SettingsSectionTest.java) | 1 |
| [VideoSettingsMigrationTest](../../../../../../app/src/test/java/com/flynes/emu/settings/VideoSettingsMigrationTest.java) | 15 |
| [AudioMarkerTimelineTest](../../../../../../app/src/test/java/com/flynes/emu/video/audio/AudioMarkerTimelineTest.java) | 2 |
| [AvSyncMonitorTest](../../../../../../app/src/test/java/com/flynes/emu/video/audio/AvSyncMonitorTest.java) | 4 |
| [DisplayLeaseWatchdogTest](../../../../../../app/src/test/java/com/flynes/emu/video/audio/DisplayLeaseWatchdogTest.java) | 3 |
| [MotionShadowEvidenceTest](../../../../../../app/src/test/java/com/flynes/emu/video/audio/MotionShadowEvidenceTest.java) | 3 |
| [SurfaceRecoveryIntentPolicyTest](../../../../../../app/src/test/java/com/flynes/emu/video/audio/SurfaceRecoveryIntentPolicyTest.java) | 3 |
| [TemporalAudioDelayTest](../../../../../../app/src/test/java/com/flynes/emu/video/audio/TemporalAudioDelayTest.java) | 7 |
| [TemporalTransitionControllerTest](../../../../../../app/src/test/java/com/flynes/emu/video/audio/TemporalTransitionControllerTest.java) | 8 |
| [ClockDomainCalibratorTest](../../../../../../app/src/test/java/com/flynes/emu/video/ClockDomainCalibratorTest.java) | 2 |
| [DisplayModeControllerTest](../../../../../../app/src/test/java/com/flynes/emu/video/DisplayModeControllerTest.java) | 2 |
| [DisplayModeSelectorTest](../../../../../../app/src/test/java/com/flynes/emu/video/DisplayModeSelectorTest.java) | 5 |
| [DisplayRequestLifecycleTest](../../../../../../app/src/test/java/com/flynes/emu/video/DisplayRequestLifecycleTest.java) | 6 |
| [FrameBufferPoolTest](../../../../../../app/src/test/java/com/flynes/emu/video/FrameBufferPoolTest.java) | 1 |
| [FrameDispatchExecutorTest](../../../../../../app/src/test/java/com/flynes/emu/video/FrameDispatchExecutorTest.java) | 7 |
| [FramePublisherTest](../../../../../../app/src/test/java/com/flynes/emu/video/FramePublisherTest.java) | 7 |
| [FrameRendererConfigTest](../../../../../../app/src/test/java/com/flynes/emu/video/FrameRendererConfigTest.java) | 1 |
| [InputLatencyTrackerTest](../../../../../../app/src/test/java/com/flynes/emu/video/InputLatencyTrackerTest.java) | 2 |
| [NativeFrameSourceTest](../../../../../../app/src/test/java/com/flynes/emu/video/NativeFrameSourceTest.java) | 2 |
| [NativeTime120ContractTest](../../../../../../app/src/test/java/com/flynes/emu/video/NativeTime120ContractTest.java) | 1 |
| [NativeVideoPresenterTest](../../../../../../app/src/test/java/com/flynes/emu/video/NativeVideoPresenterTest.java) | 6 |
| [AdaptiveQualityControllerTest](../../../../../../app/src/test/java/com/flynes/emu/video/power/AdaptiveQualityControllerTest.java) | 4 |
| [ThermalPowerMonitorTest](../../../../../../app/src/test/java/com/flynes/emu/video/power/ThermalPowerMonitorTest.java) | 2 |
| [BundledAlgorithmAvailabilityTest](../../../../../../app/src/test/java/com/flynes/emu/video/quality/BundledAlgorithmAvailabilityTest.java) | 2 |
| [DisplayMotionLeaseTest](../../../../../../app/src/test/java/com/flynes/emu/video/quality/DisplayMotionLeaseTest.java) | 3 |
| [DisplayQualityResolverBaselineTest](../../../../../../app/src/test/java/com/flynes/emu/video/quality/DisplayQualityResolverBaselineTest.java) | 6 |
| [DisplayQualityResolverQualificationTest](../../../../../../app/src/test/java/com/flynes/emu/video/quality/DisplayQualityResolverQualificationTest.java) | 4 |
| [EvidenceValidityPolicyTest](../../../../../../app/src/test/java/com/flynes/emu/video/quality/EvidenceValidityPolicyTest.java) | 3 |
| [EvidenceValidityWatchdogTest](../../../../../../app/src/test/java/com/flynes/emu/video/quality/EvidenceValidityWatchdogTest.java) | 3 |
| [LegacyVideoRuntimeAdapterTest](../../../../../../app/src/test/java/com/flynes/emu/video/quality/LegacyVideoRuntimeAdapterTest.java) | 4 |
| [PresenterFailureMapperTest](../../../../../../app/src/test/java/com/flynes/emu/video/quality/PresenterFailureMapperTest.java) | 1 |
| [TrustedEvidenceClockTest](../../../../../../app/src/test/java/com/flynes/emu/video/quality/TrustedEvidenceClockTest.java) | 4 |
| [MmpxOracleFixtureTest](../../../../../../app/src/test/java/com/flynes/emu/video/reference/MmpxOracleFixtureTest.java) | 3 |
| [DisplayCapabilitiesReaderTest](../../../../../../app/src/test/java/com/flynes/emu/video/status/DisplayCapabilitiesReaderTest.java) | 2 |
| [DisplayStatusMonitorTest](../../../../../../app/src/test/java/com/flynes/emu/video/status/DisplayStatusMonitorTest.java) | 7 |
| [GlCapabilityProbeTest](../../../../../../app/src/test/java/com/flynes/emu/video/status/GlCapabilityProbeTest.java) | 3 |
| [VideoStatusAccumulatorTest](../../../../../../app/src/test/java/com/flynes/emu/video/status/VideoStatusAccumulatorTest.java) | 5 |
| [VideoStatusRepositoryTest](../../../../../../app/src/test/java/com/flynes/emu/video/status/VideoStatusRepositoryTest.java) | 2 |
| [ViewportLayoutTest](../../../../../../app/src/test/java/com/flynes/emu/video/ViewportLayoutTest.java) | 3 |

## 3. Android instrumentation 测试

| 测试文件 | 声明数 |
|---|---:|
| [AppLanguageTest](../../../../../../app/src/androidTest/java/com/flynes/emu/AppLanguageTest.java) | 1 |
| [AndroidAtomicCatalogStateStoreTest](../../../../../../app/src/androidTest/java/com/flynes/emu/catalog/android/AndroidAtomicCatalogStateStoreTest.java) | 2 |
| [AndroidBuiltinCatalogAdapterTest](../../../../../../app/src/androidTest/java/com/flynes/emu/catalog/android/AndroidBuiltinCatalogAdapterTest.java) | 2 |
| [AndroidCatalogLaunchRegressionTest](../../../../../../app/src/androidTest/java/com/flynes/emu/catalog/android/AndroidCatalogLaunchRegressionTest.java) | 6 |
| [AndroidCatalogRuntimeTest](../../../../../../app/src/androidTest/java/com/flynes/emu/catalog/android/AndroidCatalogRuntimeTest.java) | 1 |
| [AndroidDocumentTreeGatewayTest](../../../../../../app/src/androidTest/java/com/flynes/emu/catalog/android/AndroidDocumentTreeGatewayTest.java) | 2 |
| [AndroidLargeCatalogPerformanceTest](../../../../../../app/src/androidTest/java/com/flynes/emu/catalog/android/AndroidLargeCatalogPerformanceTest.java) | 1 |
| [AndroidLegacyRomStoreReaderTest](../../../../../../app/src/androidTest/java/com/flynes/emu/catalog/android/AndroidLegacyRomStoreReaderTest.java) | 1 |
| [AndroidNativeSourceRegistrationTest](../../../../../../app/src/androidTest/java/com/flynes/emu/catalog/android/AndroidNativeSourceRegistrationTest.java) | 1 |
| [AndroidPendingReleaseStoreTest](../../../../../../app/src/androidTest/java/com/flynes/emu/catalog/android/AndroidPendingReleaseStoreTest.java) | 2 |
| [PersistedReadPermissionGatewayTest](../../../../../../app/src/androidTest/java/com/flynes/emu/catalog/android/PersistedReadPermissionGatewayTest.java) | 1 |
| [ControlLayoutActivityTest](../../../../../../app/src/androidTest/java/com/flynes/emu/ControlLayoutActivityTest.java) | 4 |
| [AndroidCoverRepositoryTest](../../../../../../app/src/androidTest/java/com/flynes/emu/cover/AndroidCoverRepositoryTest.java) | 1 |
| [CoverCaptureIntegrationTest](../../../../../../app/src/androidTest/java/com/flynes/emu/cover/CoverCaptureIntegrationTest.java) | 1 |
| [DisplayQualitySettingsTest](../../../../../../app/src/androidTest/java/com/flynes/emu/DisplayQualitySettingsTest.java) | 3 |
| [GamepadAccessibilityTest](../../../../../../app/src/androidTest/java/com/flynes/emu/GamepadAccessibilityTest.java) | 1 |
| [GamepadCancelTest](../../../../../../app/src/androidTest/java/com/flynes/emu/GamepadCancelTest.java) | 3 |
| [GamepadJoystickContinuityTest](../../../../../../app/src/androidTest/java/com/flynes/emu/GamepadJoystickContinuityTest.java) | 14 |
| [GamepadTouchDispatchTest](../../../../../../app/src/androidTest/java/com/flynes/emu/GamepadTouchDispatchTest.java) | 12 |
| [GameTitleIndexTest](../../../../../../app/src/androidTest/java/com/flynes/emu/GameTitleIndexTest.java) | 3 |
| [HapticSettingsPersistenceTest](../../../../../../app/src/androidTest/java/com/flynes/emu/HapticSettingsPersistenceTest.java) | 1 |
| [MotionComputeParityTest](../../../../../../app/src/androidTest/java/com/flynes/emu/MotionComputeParityTest.java) | 1 |
| [MotionContextFallbackTest](../../../../../../app/src/androidTest/java/com/flynes/emu/MotionContextFallbackTest.java) | 1 |
| [MotionShadowPresenterTest](../../../../../../app/src/androidTest/java/com/flynes/emu/MotionShadowPresenterTest.java) | 3 |
| [NativePresenterInitializationTest](../../../../../../app/src/androidTest/java/com/flynes/emu/NativePresenterInitializationTest.java) | 1 |
| [NativePresenterIntegrationTest](../../../../../../app/src/androidTest/java/com/flynes/emu/NativePresenterIntegrationTest.java) | 5 |
| [NesFrameSnapshotTest](../../../../../../app/src/androidTest/java/com/flynes/emu/NesFrameSnapshotTest.java) | 1 |
| [ProductOrientationTest](../../../../../../app/src/androidTest/java/com/flynes/emu/ProductOrientationTest.java) | 1 |
| [RomIdentityTest](../../../../../../app/src/androidTest/java/com/flynes/emu/RomIdentityTest.java) | 1 |
| [LegacySaveMigratorTest](../../../../../../app/src/androidTest/java/com/flynes/emu/save/LegacySaveMigratorTest.java) | 1 |
| [SaveRepositoryTest](../../../../../../app/src/androidTest/java/com/flynes/emu/save/SaveRepositoryTest.java) | 2 |
| [SettingsMasterDetailTest](../../../../../../app/src/androidTest/java/com/flynes/emu/SettingsMasterDetailTest.java) | 4 |
| [SettingsPersistenceTest](../../../../../../app/src/androidTest/java/com/flynes/emu/SettingsPersistenceTest.java) | 2 |
| [SpatialCapabilityFallbackTest](../../../../../../app/src/androidTest/java/com/flynes/emu/SpatialCapabilityFallbackTest.java) | 3 |
| [SpatialFilterGoldenTest](../../../../../../app/src/androidTest/java/com/flynes/emu/SpatialFilterGoldenTest.java) | 3 |
| [StartAndPauseSeparationTest](../../../../../../app/src/androidTest/java/com/flynes/emu/StartAndPauseSeparationTest.java) | 2 |
| [SwappyFailClosedTest](../../../../../../app/src/androidTest/java/com/flynes/emu/SwappyFailClosedTest.java) | 1 |
| [BaseVideoCertificationTest](../../../../../../app/src/androidTest/java/com/flynes/emu/test/BaseVideoCertificationTest.java) | 2 |
| [NativeTime120CertificationTest](../../../../../../app/src/androidTest/java/com/flynes/emu/test/NativeTime120CertificationTest.java) | 1 |
| [AppIconResourceTest](../../../../../../app/src/androidTest/java/com/flynes/emu/ui/AppIconResourceTest.java) | 1 |
| [FirstRunNavigationTest](../../../../../../app/src/androidTest/java/com/flynes/emu/ui/FirstRunNavigationTest.java) | 9 |
| [HomeContinuousLibraryTest](../../../../../../app/src/androidTest/java/com/flynes/emu/ui/HomeContinuousLibraryTest.java) | 2 |
| [NearbyFriendsManageTest](../../../../../../app/src/androidTest/java/com/flynes/emu/ui/NearbyFriendsManageTest.java) | 3 |
| [NearbyFriendsTest](../../../../../../app/src/androidTest/java/com/flynes/emu/ui/NearbyFriendsTest.java) | 7 |
| [NearbyInGameStatusTest](../../../../../../app/src/androidTest/java/com/flynes/emu/ui/NearbyInGameStatusTest.java) | 2 |
| [NearbyLobbyTest](../../../../../../app/src/androidTest/java/com/flynes/emu/ui/NearbyLobbyTest.java) | 3 |
| [NearbyPairingTest](../../../../../../app/src/androidTest/java/com/flynes/emu/ui/NearbyPairingTest.java) | 4 |
| [ThemeContrastTest](../../../../../../app/src/androidTest/java/com/flynes/emu/ui/ThemeContrastTest.java) | 3 |
| [GlCapabilityProbeInstrumentedTest](../../../../../../app/src/androidTest/java/com/flynes/emu/video/GlCapabilityProbeInstrumentedTest.java) | 1 |

## 4. Harmony Hypium 用例

| 测试文件 | 声明数 |
|---|---:|
| [CatalogProductService.test](../../../../../../harmony/entry/src/ohosTest/ets/test/CatalogProductService.test.ets) | 3 |
| [CheckpointStore.test](../../../../../../harmony/entry/src/ohosTest/ets/test/CheckpointStore.test.ets) | 1 |
| [ControlLayoutWarnings.test](../../../../../../harmony/entry/src/ohosTest/ets/test/ControlLayoutWarnings.test.ets) | 7 |
| [LicenseModel.test](../../../../../../harmony/entry/src/ohosTest/ets/test/LicenseModel.test.ets) | 3 |
| [NearbyService.test](../../../../../../harmony/entry/src/ohosTest/ets/test/NearbyService.test.ets) | 10 |
| [PauseParams.test](../../../../../../harmony/entry/src/ohosTest/ets/test/PauseParams.test.ets) | 2 |
| [SettingsHelpers.test](../../../../../../harmony/entry/src/ohosTest/ets/test/SettingsHelpers.test.ets) | 6 |
| [Smoke.test](../../../../../../harmony/entry/src/ohosTest/ets/test/Smoke.test.ets) | 1 |

## 5. iOS 原生测试接入

“未接入”指未被三个 iOS CMake 文件列为测试源码，且未在 `ios/scripts` / `scripts` / workflow 找到正式执行入口；部分有历史手动运行命令，不能称为从未运行。应注册独立 executable 或迁入合适测试 bundle，不把含 `main/@main` 的程序直接塞进 XCTest。

| 文件 | XCTest 方法数 | 当前 CMake 接入 |
|---|---:|---|
| [AppLocalizationTests.mm](../../../../../../ios/tests/AppLocalizationTests.mm) | 0 | 未接入 |
| [catalog_bridge_test.mm](../../../../../../ios/tests/catalog_bridge_test.mm) | 0 | 未接入 |
| [catalog_presentation_test.mm](../../../../../../ios/tests/catalog_presentation_test.mm) | 0 | 未接入 |
| [catalog_source_service_test.mm](../../../../../../ios/tests/catalog_source_service_test.mm) | 0 | 未接入 |
| [CatalogSourceImportTests.mm](../../../../../../ios/tests/CatalogSourceImportTests.mm) | 12 | 已列入 |
| [CatalogTitleCoverTests.mm](../../../../../../ios/tests/CatalogTitleCoverTests.mm) | 11 | 已列入 |
| [ControlLayoutDraftTests.swift](../../../../../../ios/tests/ControlLayoutDraftTests.swift) | 0 | 未接入 |
| [direction_session_test.cpp](../../../../../../ios/tests/direction_session_test.cpp) | 0 | 未接入 |
| [frame_input_latch_test.cpp](../../../../../../ios/tests/frame_input_latch_test.cpp) | 0 | 未接入 |
| [game_cover_policy_test.cpp](../../../../../../ios/tests/game_cover_policy_test.cpp) | 0 | 未接入 |
| [GamepadOverlayTests.mm](../../../../../../ios/tests/GamepadOverlayTests.mm) | 13 | 已列入 |
| [metal_renderer_test.mm](../../../../../../ios/tests/metal_renderer_test.mm) | 0 | 未接入 |
| [MetalPixelParityTests.mm](../../../../../../ios/tests/MetalPixelParityTests.mm) | 6 | 已列入 |
| [PlaybackAudioPlayerTests.mm](../../../../../../ios/tests/PlaybackAudioPlayerTests.mm) | 1 | 已列入 |
| [PlaybackRuntimeBridgeTests.mm](../../../../../../ios/tests/PlaybackRuntimeBridgeTests.mm) | 5 | 已列入 |
| [PlaybackSoakTests.mm](../../../../../../ios/tests/PlaybackSoakTests.mm) | 2 | 已列入 |
| [portability_smoke_test.cpp](../../../../../../ios/tests/portability_smoke_test.cpp) | 0 | 已列入 |
| [ProductImportUITests.mm](../../../../../../ios/tests/ProductImportUITests.mm) | 3 | 已列入 |
| [ProductUITests.mm](../../../../../../ios/tests/ProductUITests.mm) | 10 | 已列入 |
| [rgb565_upload_test.cpp](../../../../../../ios/tests/rgb565_upload_test.cpp) | 0 | 未接入 |
| [rom_package_test.cpp](../../../../../../ios/tests/rom_package_test.cpp) | 0 | 未接入 |
| [test_playback_audio_focus.cpp](../../../../../../ios/tests/test_playback_audio_focus.cpp) | 0 | 未接入 |
| [test_playback_audio_queue.cpp](../../../../../../ios/tests/test_playback_audio_queue.cpp) | 0 | 未接入 |
| [test_playback_clock.cpp](../../../../../../ios/tests/test_playback_clock.cpp) | 0 | 未接入 |

## 6. CTest 注册清单

以下为 `add_test(NAME …)` 的源码声明，正式执行须使用新 configure 产生的 CTest discovery 清单核对，不能复用旧目录的数量。

### [core/CMakeLists.txt](../../../../../../core/CMakeLists.txt)

- `core_smoke`
- `temporal_interpolator`

### [shared/CMakeLists.txt](../../../../../../shared/CMakeLists.txt)

- `flynes_game_title_data_check`
- `flynes_game_title_generator`
- `flynes_runtime`
- `flynes_runtime_pcm_contention`
- `flynes_android_hmos_overlay_policy`（设置 `FLYNES_ANDROID_NDK_ROOT` 后才注册）
- `flynes_game_title_index`
- `flynes_app_contract`
- `flynes_product_game_center`
- `flynes_product_control_layout`
- `flynes_product_gamepad_hit_map`
- `flynes_product_pause_actions`
- `flynes_catalog_scan`
- `flynes_catalog_persist`
- `flynes_catalog_user`
- `flynes_catalog_source_remove`
- `flynes_settings`
- `flynes_control_layout_persist`
- `flynes_c_header`
- `flynes_rom_payload_parser`
- `flynes_content_identity`
- `flynes_identity_fixture_loader`
- `flynes_unsupported_payload_classifier`
- `flynes_unsupported_payload_fixture_loader`
- `flynes_rom_fixture_loader`
- `flynes_bounded_zip_archive`
- `flynes_bounded_zip_archive_edges`
- `flynes_bounded_zip_payload`
- `flynes_bounded_zip_payload_edges`
- `flynes_zip_payload_fixture_loader`
- `flynes_zip_open_fixture_loader`
- `flynes_rom_fixture_generator`
- `flynes_zip_open_fixture_generator`
- `flynes_zip_open_fixture_corpus_check`
- `flynes_zip_payload_fixture_generator`
- `flynes_zip_payload_fixture_corpus_check`
- `flynes_identity_fixture_corpus_check`
- `flynes_identity_fixture_generator`
- `flynes_unsupported_payload_fixture_corpus_check`
- `flynes_unsupported_payload_fixture_generator`
- `flynes_session_schema_registry`
- `flynes_session_codec`
- `flynes_pair_capability`
- `flynes_initial_plan_lock`
- `flynes_session_codec_loopback`
- `flynes_session`
- `flynes_session_initial_plan`
- `flynes_session_public_path`
- `flynes_session_receive_seam`
- `flynes_session_app_frame`
- `flynes_session_app_frame_consistency`

### [harmony/tests/CMakeLists.txt](../../../../../../harmony/tests/CMakeLists.txt)

- `flynes_harmony_catalog_smoke`
- `flynes_harmony_session_schema`
- `flynes_harmony_runtime_smoke`
- `flynes_harmony_play_session`
- `flynes_harmony_product_bridge`
- `flynes_harmony_scan_job_queue`
- `flynes_harmony_scan_job_executor`
- `flynes_harmony_render_mailbox`
- `flynes_harmony_display_policy`
- `flynes_harmony_native_play_support`
- `flynes_harmony_motion_frame_scheduler`
- `flynes_harmony_nearby_adapter`

### [ios/tests/CMakeLists.txt](../../../../../../ios/tests/CMakeLists.txt)

- `flynes_ios_portability_smoke_host`
- `flynes_ios_offline_title_contract`
- `flynes_ios_stage1_contract`
- `flynes_ios_product_catalog_contract`
- `flynes_ios_product_shell_contract`
- `flynes_ios_product_android_parity_contract`
- `flynes_ios_product_metal_contract`
- `flynes_ios_product_ipa_contract`
- `flynes_ios_simulator_result_validator`

## 7. CI 与证据缺口

| 入口 | 已有覆盖 | 未涵盖 / 风险 |
|---|---|---|
| [stage0.yml](../../../../../../.github/workflows/stage0.yml) | Ubuntu core smoke；macOS C ABI 头文件编译 | 没有 shared CTest、Android UT/E2E 或 Harmony 页面 UI 作业；没有运行 core 全 CTest |
| [ios-stage1.yml](../../../../../../.github/workflows/ios-stage1.yml) | 固定 SDK 编译和 portability 模拟器 smoke | 不是产品 UI XCTest |
| [ios-product.yml](../../../../../../.github/workflows/ios-product.yml) | 主机源码契约、device 编译和 unsigned IPA 产物 | 未启用 simulator XCTest；打包不证明可玩或真机覆盖 |
| [ci-check.ps1](../../../../../../scripts/ci-check.ps1) | ABI、core CTest、shared 全 CTest、Android assembleDebug | 已包含 shared，不能沿用旧文档“仅 core”的判断；不含 Android unit/instrumentation |
| [run_harmony_completion_gate.ps1](../../../../../../tools/quality/run_harmony_completion_gate.ps1) | host、Hypium、Android unit/package/instrumentation、分阶段包哈希 | Hypium 仅匹配零错误文字，需补 test count/ID、设备类型和 SHA 校验；没有 iOS 分支 |

在当前检索的 build/CI 配置中未发现 JaCoCo、LLVM/gcov 或 Xcode coverage 的报告与阈值门禁。真实行/分支覆盖率应标 UNKNOWN，不能用 448/133/33/63 个测试声明推算。

## 8. 具体容易高估的覆盖

- Android `ControlLayoutActivityTest.dragSaveIsRestoredByNextEditorAndGameHitMap` 直接向 View dispatch MotionEvent，重开 editor 的断言主要是显示；尚不能替代“保存后在实际游戏按新坐标生效”的系统输入 E2E。
- Android `AndroidCatalogLaunchRegressionTest` 使用真实 resolver/provider/native 等组件验证路径，价值明确，但不覆盖用户从系统文件选择器授权到游戏的完整流程。
- Android `AndroidLargeCatalogPerformanceTest` 通过 native scanner 构造隔离目录，属于性能/集成测试；不能替代真实导入流程。
- Harmony `CheckpointStore.test.ets` 当前只测 checkpointKey，未测试读写、fsync/rename、损坏隔离或游戏恢复；`Smoke.test.ets` 仅比较常量。
- iOS `PlaybackSoakTests` 直接循环 step/pull/copy（默认 36000 帧），并非屏幕按真实时钟运行 30 分钟，也不能认证设备音频路由、触控延迟或温控。
- iOS `ProductImportUITests` 确实操作 Files，但需要专用模拟器、夹具可见性和稳定来源定位；source rows 暂无稳定 ID，不能用导入成功的历史记录掩盖新环境夹具缺失。
- 静态 Python 契约检查仅证明源码形态；unittest discovery 没发现的脚本、未注册的 native main、默认排除的真机认证均需明确执行入口。

本次只生成审计文档；未运行全量测试、生成覆盖率、修改代码或进行设备安装。

## 9. 当前已声明的方法级用例

以下补充完整方法名，便于实施时精确选择复用对象。仍是源码清单，不是执行报告；native独立程序与CTest目标见上文。新增增强用例与此清单分开，不能把待实现ID当成当前方法。

### Android JVM

| 文件 | 已声明测试方法 / it 名称 |
|---|---|
| `AudioPumpTest.java` | `advancesExactlyOneFrameAndCompletesPartialWrites`<br>`propagatesCoreAndSinkFailuresWithoutSpinning`<br>`optionalTemporalPathDelaysPcmWithoutDelayingFrameSignal` |
| `BuildSmokeTest.java` | `java17IsActive` |
| `AndroidRetryableMigrationLogTest.java` | `failedAttemptKeepsOldDataEligibleUntilOneSuccess` |
| `AndroidUuidSafMapTest.java` | `storesPersistableUriByUuidHexAndNeverAcceptsAllZeroUuid`<br>`persistsBuiltinUuidOnceAndRemovesSourceRowsWithoutTouchingIt` |
| `FncaNativeMigratorTest.java` | `replaysSourcesFavoritesAndPlaysThenStopsRetryingAfterSuccess`<br>`failedCommitKeepsFncaEligibleAndReusesSourceUuid` |
| `NativeCatalogProjectorTest.java` | `indexTitlesReplaceRenamedPackageMetadataWithoutChangingIdentity`<br>`aWhitespaceBasenameDoesNotPoisonTheWholeCatalogProjection`<br>`chineseArchiveTitleIsPreservedSeparatelyFromTheEnglishZipEntry`<br>`projectsNativeRowsOntoJavaCatalogStateWithoutEmbeddingSafInCanonicalIds`<br>`favoriteRevisionDefinesProjectedCatalogRevisionBeforeAnyGameIsPlayed`<br>`anUnresolvablePlatformLocatorIsNeverFabricatedIntoATreeUriWithAnAppendedPath`<br>`derivesAnOpenableDocumentLocatorWhenNoScannedLocatorSurvivedTheRestart` |
| `Api24ProductionGuardTest.java` | `touchedProductionScopeUsesApi24AvailableLibraryMethods` |
| `DomainModelV2Test.java` | `hashesAreTypedValidatedAndDefensivelyNormalized`<br>`stableIdsUseLocationAndRawLocatorWithoutLeakingInputs`<br>`stableIdsRejectUnpairedUtf16ButPreserveValidUnicodeAndNul`<br>`stableIdBlankSetIsFrozenAcrossJavaUnicodeVersions`<br>`variantHashValidationPrecedesBlankIdInputValidation`<br>`titleCandidatesKeepLanguageOriginConfidenceAndReviewStateSeparate`<br>`catalogGroupsDifferentPayloadsOnlyWhenCanonicalIdMatches`<br>`catalogRejectsConflictingVerifiedMetadataForOneCanonicalId` |
| `GameCatalogTest.java` | `constructorsRejectBlankIdsUrisAndMismatchedPhysicalHashes`<br>`packageFormatEnforcesExactEntryLocatorRules`<br>`zipEntryIdentityAndSourceStateAreProjected`<br>`domainCollectionsAreDefensivelyCopied`<br>`fatalScanDoesNotReplaceLastGoodCatalog`<br>`sameCanonicalIdGroupsDifferentPayloadHashesAndMergesSearchMetadata`<br>`identicalPayloadDoesNotGroupDistinctCanonicalIds`<br>`searchMatchesTitlesAliasesOriginalFilenameAndUnknownCandidate`<br>`replacementPreservesUserStateOnlyByStableCanonicalId`<br>`favoriteAndRecentViewsAreFilteredOrderedAndImmutable` |
| `HexEncodingTest.java` | `preservesEveryByteAndLeadingZerosForExistingPersistentIds` |
| `LegacyLibraryMigratorTest.java` | `importsOnlyUniqueRawMatchAndMarksAfterCommit`<br>`missingPermissionRegistersReauthorizeAndCommitFailureNeverMarks`<br>`fatalMatchedCandidateReturnsTypedPermissionLossWithoutAnyMutation` |
| `CatalogRepositoryTest.java` | `nativeProjectionPublishesWithoutSerializingOrReadingLegacyStore`<br>`rootEnumerationTruncationPreservesLastGoodPackageAsStale`<br>`fullScanReplacesOnlyTargetFatalPreservesAndPackageErrorBecomesStale`<br>`repositoryDoesNotPublishOnStoreFailureAndPersistsUserState`<br>`codecIsDeterministicChecksIntegrityAndHandlesLargeCatalog`<br>`fullDeletionStaleRevisionAndBuiltinRemovalAreExplicit`<br>`canonicalOverrideMigratesUserStateByPayloadAndRestartRestoresIt`<br>`concurrentScanAndRemovalAreSerializedWithoutResurrectingSource`<br>`partialScanDowngradesUnmentionedPackagesAndRefreshesOnlyIndexedOnes`<br>`scanSourceIdentityIsExactAndReauthorizeInvalidatesInFlightScan`<br>`reauthorizeRetainsPackagesAsStaleUntilNewIdentityScansThem`<br>`canonicalLineageRoundTripKeepsLatestStateWithoutResurrectionOrDoubleCount`<br>`canonicalMergeCombinesDistinctLineagesOnceAndUsesLatestFavorite`<br>`loadRejectsChecksumValidGlobalDuplicateWithoutChangingLiveState`<br>`sourceScanRejectsDuplicatePackageAndEntryOutcomes`<br>`sourceRemovalDropsOrphanedCanonicalUserState`<br>`reauthorizeCannotChangeSourceTypeAndLeavesAllStateUnchanged` |
| `RomCompatibilityContractTest.java` | `formatAndCompatibilityPairsAreValidatedAtEveryConstructionBoundary` |
| `BoundedZipDescriptorTest.java` | `acceptsSignedAndUnsignedDescriptorsForStoredAndDeflatedEntries`<br>`rejectsMissingTruncatedAndMismatchedDescriptors` |
| `BoundedZipMemoryStressTest.java` | `scannerAndLoaderFitDefaultLimitsIn64MiBFork` |
| `BoundedZipOpenFixtureParityTest.java` | `currentJavaZipOpenMatchesVersionOneSharedFixtures`<br>`fixtureManifestNumbersRequireCanonicalUnsignedDecimal`<br>`fixtureManifestUsesCanonicalCaseAndBlobNames`<br>`fixtureManifestRejectsEmptySuccessNamesAndUnstableErrorMessages` |
| `BoundedZipPayloadFixtureParityTest.java` | `currentJavaPayloadReadMatchesVersionOneSharedFixtures`<br>`manifestRequiresCanonicalUnsignedNumbersAndJavaSignedRanges`<br>`manifestRejectsUnsafeIdentityAndIncoherentOutcomeFields` |
| `ContentIdentityFixtureParityTest.java` | `productionHasherMatchesVersionOneSharedFixtures`<br>`stableIdsMatchVersionOneSharedFixtures` |
| `RomPackageScannerTest.java` | `rawInesIsSniffedWithoutExtensionAndCarriesHashesMetadataAndLowTitle`<br>`nes2ParsesExtendedAndExponentialSizesWithTrainerAndTrailingWarning`<br>`truncatedZeroPrgAndOverflowNes2AreIndexedInvalidWithStableReasons`<br>`validFdsAndUnifAreIndexedButExplicitlyUnsupported`<br>`zipIndexesEveryRomByExactRawLocatorAndLoaderUsesPayloadSha256`<br>`opaqueLocatorRoundTripsAndExecutableNamesNeverEnterTheCatalog`<br>`zipAccountsDirectoriesUnsafePathsNestedArchivesExecutablesGbAndSidecars`<br>`corruptAndOversizePackagesAreErrorsAndEveryCandidateIsAccounted`<br>`oversizeZipEntryIsAccountedWithoutInflateAndDoesNotDropPlayableSibling`<br>`laterPayloadIntegrityFailureRollsBackStagedIndexedOutcomes`<br>`zeroLengthBulkReadCannotHangAndProviderOrderCannotChangeResult`<br>`legacyZipNameEncodingsAreDisplayOnlyAndNeverChangeLocator`<br>`locationIdentityPhysicalIdentityAndOverrideGroupingStaySeparate`<br>`invalidCanonicalResolverOutputsHaveDedicatedRawAccountingWithoutLeakage`<br>`invalidCanonicalResolverOutputsHaveDedicatedZipAccountingWithoutLeakage`<br>`unavailableSourceAndDuplicateDocumentKeysAccountEveryCandidateWithoutOpening`<br>`parserCoversDirtyHeaderNes2MultiplierOverflowAndInvalidDiskFormats`<br>`rawNamesSupportMixedEncodingDuplicatesAndCaseCollisionWithoutBecomingLocators`<br>`unsafeRawPathsAreAccountedAndCentralLocalNameMismatchAndEncryptionAreRejected`<br>`zipAndPayloadLimitsUseStrictGreaterThanBoundaries`<br>`compressionRatioGuardStartsOnlyAboveThresholdAndUsesDeclaredRatio` |
| `RomPayloadParserFixtureParityTest.java` | `currentJavaParserMatchesVersionOneSharedFixtures` |
| `UnsupportedPayloadClassifierFixtureParityTest.java` | `productionClassifierMatchesVersionOneSharedFixtures`<br>`loaderRejectsMalformedManifestAndCorpus`<br>`loaderRejectsHardlinkAliasesWhenSupported`<br>`loaderRejectsHardlinkToFileOutsideCorpusWhenSupported`<br>`loaderRejectsSymlinkAliasesWhenSupported`<br>`loaderRejectsSymlinkRootWhenSupported`<br>`loaderRejectsWindowsJunctionRootWhenSupported` |
| `DocumentLocatorShapeTest.java` | `rejectsTheExactLocatorShapeThatCrashedTheDevice`<br>`rejectsATreeLocatorWithAnyAppendedPath`<br>`rejectsATreeLocatorItself`<br>`acceptsTreeScopedDocumentLocators`<br>`acceptsPlainDocumentLocators`<br>`rejectsEverythingThatIsNotAContentDocument` |
| `SourceEnumeratorTest.java` | `recursivelyEnumeratesInStableOrderAndKeepsOpaqueLocators`<br>`cycleAndDepthOrFileLimitAreFatalAndPrivacySafe`<br>`unifiedNodeBudgetCountsDirectoriesAndBoundsProviderReads` |
| `SourceRegistryTest.java` | `sameTreeReusesIdPermissionLossIsTypedAndRemoveCommitsBeforeRelease`<br>`failedRepositoryWriteReleasesOnlyGrantAcquiredByThisAttempt`<br>`orphanGrantTombstoneClosesTakeToRepositoryCrashWindow`<br>`orphanTombstoneAfterSuccessfulCommitNeverDeletesRegisteredSource`<br>`failedRepositoryWriteKeepsOrphanTombstoneWhenCompensationReleaseFails`<br>`pendingRemovalSurvivesRepositoryFailureAndRestartThenRetries`<br>`pendingRemovalRetriesBothCrashWindowsAndNeverReleasesInUseLocator`<br>`retryAttemptsEveryTombstoneAndAggregatesFailures` |
| `SourceRelativePathTest.java` | `acceptsNestedRelativePathsWithForwardSlashes`<br>`rejectsPathsThatAreNotRelativeToTheTreeRoot` |
| `ZipEntryNameDecoderTest.java` | `efsAlwaysUsesStrictUtf8`<br>`verifiedUnicodePathWinsAndBadCrcFallsBack`<br>`nonEfsFallbackIsExplicitCp437OrGb18030` |
| `CoverCaptureCoordinatorTest.java` | `skipsBlankCandidateAndStoresLaterInformativeFrame`<br>`capturedBytesDoNotChangeWhenNativeBufferIsReused` |
| `DirectionFeedbackGateTest.java` | `firstRevisionChangeEmits`<br>`unchangedRevisionDoesNotEmit`<br>`suppressedRevisionIsConsumedAndNotReplayed`<br>`resetConsumesCurrentRevisionAndClearsCooldown`<br>`terminalConsumptionDoesNotEmitOrMoveTheCooldownClock` |
| `GameCenterStateTest.java` | `zipRankingUsesBothLeafNamesAndExcludesDirectories`<br>`explicitPackageScoreTakesPrecedenceOverSearchAliases`<br>`filtersEveryCategoryAndSearchesAllLocalizedFields`<br>`remembersSelectionIndependentlyPerCategory`<br>`restoreRepairsMissingSelectionWithoutPageState`<br>`returnsTheEntireFilteredLibraryForContinuousScrolling`<br>`allSortsByPopularityThenContentIdAndDeduplicatesOnlyIdenticalContent`<br>`cachedOrderRefreshesWhenSameListChangesTitlesOrUserState` |
| `GameTitlePresentationTest.java` | `explicitManualTitleWinsOverOtherVerifiedMetadata`<br>`chineseUiUsesChineseNameAndEnglishAsSecondary`<br>`englishUiUsesEnglishNameAndChineseAsSecondary`<br>`missingChineseNameKeepsReadableEnglishWithoutPretendingItWasTranslated` |
| `HomeHeaderLayoutPolicyTest.java` | `reservesCategorySpaceForExpandedPseudoLanguageAndLargeText`<br>`selectedGameTitleCanWrapWhenLargeTextIsEnabled` |
| `GameTitleLocalizerTest.java` | `filenamesAloneNeverGuessASeriesTitle`<br>`englishAndUnknownNamesAreNeverRewritten`<br>`existingChineseNamesAreNotDuplicated` |
| `ContinuousJoystickSessionTest.java` | `centerDownIsCapturedBeforeDirectionBegins`<br>`downOutsideIdleBaseIsCapturedNeutralThenMoves`<br>`edgeClampedDownStartsNeutralAndUsesPhysicalMovement`<br>`dragPastFiveRadiiStaysDirectionalAndReversesWithoutLift`<br>`secondLeftPointerCannotStealWhileAStillCombines`<br>`leftSideSelectTargetWinsOverJoystickActivation`<br>`joystickOwnerKeepsRoleAcrossActionButtonCoordinates`<br>`baseFollowsOnlyExcessAndKnobNeverExceedsTravel`<br>`dynamicCenterIsClampedInsideSafeLeftHalf`<br>`insetReconfigurationPreservesVectorAndFutureMotion`<br>`repeatedSameMapReconfigureDoesNotRefollowBase`<br>`repeatedStationaryMovesDoNotRefollowBase`<br>`directionModeReconfigurationCancelsOwner`<br>`ownerUpAndCancelAlwaysClearVisualAndBits`<br>`reusedPointerIdReleasesOldJoystickOwnership` |
| `ControlLayoutV2Test.java` | `codecRoundTripsDeterministically`<br>`malformedOrOldPayloadMigratesToVerifiedRecommendedLayout`<br>`warningsIdentifySmallOverlapGestureAndCentralProtection`<br>`hitMapAppliesNormalizedPositionsAndScale`<br>`recommendedAndExtremeLayoutsRemainInsideAllSafeMatrices` |
| `ControlVisualGeometryTest.java` | `dpadArmTracksPlacementScaleAtBothValidatedExtremes` |
| `DirectionSessionTest.java` | `fixedBaseNeverMovesAndKnobClampsAtOneThreeAndFiveRadii`<br>`activatedDirectionCrossesCenterAndReversesWithoutZeroFrame`<br>`directionOwnerKeepsRoleAcrossActionZoneWhileButtonsRemainMultitouch`<br>`fixedCaptureUsesExactSixteenDpBufferAndButtonWinsOverlap`<br>`dpadCaptureIncludesCenterAndCompleteExpandedOuterBounds`<br>`dpadContinuesFarOutsideAndOrbitsAllEightSectorsWithoutNeutralizing`<br>`secondDirectionPointerCannotTakeOwnership`<br>`exactSectorsUseSevenPointFiveDegreeHysteresis`<br>`repeatedMoveAndEquivalentConfigurationAreIdempotent`<br>`sameModeReconfigurePreservesOwnerAnchorAndEffectiveVector`<br>`exactModeChangeCancelsDirectionButPreservesButtons`<br>`feedbackRevisionOnlyTracksActivationAndStableSectorChanges`<br>`targetedCancelDropsOnlyRequestedRoleWithoutFeedbackSignal`<br>`nonFiniteDownIsRejectedBeforeExistingRolesAreMutated`<br>`nonFiniteDirectionMovesAreIgnoredBeforeAndAfterActivation`<br>`randomizedVectorsNeverProduceOppositesOrPostActivationNeutral` |
| `GamepadHitMapTest.java` | `standardLayoutUsesClassicDpadAndErgonomicActionSizes`<br>`aAndBAreDiagonalWithAtLeastTwentyFourDpGap`<br>`selectAndStartStayOutsideCentralSeventyPercent`<br>`dpadDirectionsAndDiagonalAreResolvedWithoutOpposites`<br>`dpadHysteresisRetainsDirectionNearThresholdThenCancelsOnExit`<br>`fromLayoutRejectsEveryNonFiniteDeadZone` |
| `GamepadInputStateTest.java` | `bRollToAProducesCombinationWithoutEarlyBRelease`<br>`selectAndStartUseTrueHoldUntilUpOrCancel`<br>`nonFiniteButtonMovesAreIgnoredAndFiniteRollStillWorks`<br>`sharedButtonOwnersAndRollCombinationsDecrementExactly`<br>`opposingDirectionsFromDifferentPointersNeverCoexist`<br>`explicitButtonWinsWhenDpadCaptureOverlaps`<br>`joystickPointerStaysOwnedAcrossLongReverseDrag`<br>`fixedSeedTenThousandPointerSequencesAlwaysTerminateCleanly` |
| `HapticPatternTest.java` | `x100ProfileGivesEveryOnScreenControlADistinctIdentity`<br>`distinctABUsesDifferentCadenceAndCanBeDisabled`<br>`strongerLevelsIncreaseAmplitudeWithoutChangingButtonIdentity` |
| `InputRouterTest.java` | `mergesSourcesAndCancelAllPublishesZero`<br>`appPauseNeverGeneratesNesStart`<br>`cancelingTouchPreservesControllerInput` |
| `JoystickDirectionTest.java` | `deadZoneSectorAndReleaseHysteresisMatchX100Preset`<br>`distanceOutsideVisualBaseSaturatesInsteadOfClearing` |
| `MinimumTapTest.java` | `quickTapKeepsButtonPressedForTheRemainingMinimumDuration`<br>`normalHumanPressNeedsNoExtraPulse`<br>`clockAnomalyCannotExtendBeyondTheConfiguredMinimum` |
| `JoystickReturnAnimationTest.java` | `samplesStartMidpointAndCenterAtEightyMilliseconds`<br>`centeredKnobDoesNotStart`<br>`cancellationClearsAnimationImmediately`<br>`aNewStartInterruptsThePreviousReturn` |
| `ExactRomLoaderTest.java` | `launchRequestMapCodecRoundTripsExactLocation`<br>`launchRequestMapCodecRoundTripsRawZipEntryIdentity`<br>`explicitCp437AndGb18030NamesLoadByRawIdentity`<br>`legacySidecarsBeforeAndAfterTargetDoNotAffectExactLoading`<br>`unicodePathDisplayMetadataTravelsWithExactRawLocator`<br>`displayMetadataCannotChangeExactRawSelection`<br>`wrongRawNameOrOffsetCannotSelectAnotherEntry`<br>`caseCollidingNamesRemainDistinctRawEntries`<br>`launchRequestRejectsBlankLocationAndNonPlayableCompatibility`<br>`zipLoadsOnlyTheExactNamedEntry`<br>`loaderPropagatesExactSourceIdAndUriToStreamOpener`<br>`zipRejectsMissingExactEntry`<br>`exactLocatorAllowsDuplicateRawNamesWithoutAmbiguity`<br>`rawLocatorDisambiguatesDuplicateNamesByLocalHeaderOffset`<br>`zipRejectsDirectoryAsPayload`<br>`rawPayloadOverScannerLimitIsRejected`<br>`rawPayloadAtExactScannerLimitIsAccepted`<br>`zipPayloadAtExactScannerLimitIsAccepted`<br>`compressedZipBombPayloadOverScannerLimitIsRejected`<br>`largeSkippedEntryCountsAgainstTotalInflatedLimit`<br>`oversizedSkippedEntryAfterTargetStillCountsAgainstInflatedLimit`<br>`tooManyZipEntriesAreRejectedEvenWhenTargetAppearsFirst`<br>`declaredEntryCountLimitShortCircuitsBeforeCentralDirectoryTraversal`<br>`zipEntryCountAtExactLimitIsAccepted`<br>`cumulativeInflatedBytesAtExactLimitAreAccepted`<br>`zipSourceBytesAreBoundedBeforeTraversal`<br>`zipSourceAtExactByteLimitIsAccepted`<br>`zipWithLocalEntryButNoCentralDirectoryIsInvalid`<br>`fakeEndRecordAndSplitMarkerAreInvalidZip`<br>`validEmptyZipReportsMissingEntry`<br>`centralCrcMustMatchSelectedAndSkippedInflatedPayloads`<br>`corruptedNonTargetPayloadIsNotInflatedOrRetainedDuringExactLoad`<br>`centralCompressedAndUncompressedSizesMustMatchStreamedEntry`<br>`centralCompressionMethodMustMatchLocalHeader`<br>`centralFlagsMustMatchLocalHeader`<br>`malformedNonZipSourceIsClassifiedAsInvalidZip`<br>`sourceZipExceptionIsClassifiedAsIoErrorNotInvalidArchive`<br>`sourceCloseZipExceptionIsClassifiedAsIoError`<br>`sourceReadSecurityExceptionIsClassifiedAsIoError`<br>`malformedUtf8EntryNameIsClassifiedAsInvalidZip`<br>`sha1MismatchIsRejected`<br>`physicalPackageSha256MismatchIsRejectedEvenWhenPayloadMatches`<br>`executableEntryIsNeverTreatedAsPlayable`<br>`rawExeNameCannotBeHiddenByDisplayMetadata`<br>`executablePayloadIsRejectedWhenRawContentUriIsOpaque` |
| `LaunchCoordinatorTest.java` | `successfulGatewayCommitRecordsHistoryAndCatalogExactlyOnce`<br>`hashFailurePreservesHistoryCatalogAndSession`<br>`unexpectedSourceFailureIsReportedInsteadOfKillingTheLaunchThread`<br>`sessionFailurePreservesHistoryAndCatalog`<br>`uncheckedGatewayFailureReturnsTypedSessionFailure`<br>`unresolvedAndNonPlayableVariantsReturnTypedFailuresWithoutOpening`<br>`scanReusingIdsForDifferentIdentityRejectsStaleLaunchBeforeSessionCommit`<br>`scanRenamingSameIdentityCreditsCurrentCanonicalEntry`<br>`movedSourceUriRejectsStaleLaunchBeforeSessionCommit`<br>`removedVariantRejectsStaleLaunchBeforeSessionCommit`<br>`revokedPermissionAndIncompatibleReplacementRejectStaleLaunch`<br>`initiallyUnavailableSourceDoesNotOpenOrLaunch`<br>`changedZipOffsetRejectsLoadedVariantAsStale`<br>`coordinatorPropagatesExactZipLocatorAndEncoding`<br>`concurrentLaunchesLoadTogetherButCommitSessionCatalogAndHistorySerially`<br>`coordinatorsSharingCatalogSerializeHistoryBeforeNextSessionCommit`<br>`gatewayRunsWithoutHoldingPublicCatalogMonitor`<br>`scanPublicationWaitsForGatewayAndCatalogCommit`<br>`slowHistoryDoesNotBlockCatalogReadFavoriteOrScan`<br>`checkedHistoryFailureReturnsTypedPostCommitResult`<br>`uncheckedHistoryFailureDoesNotEscapeCoordinator` |
| `StateHeaderReaderTest.java` | `extractsSha1AtOffset28`<br>`rejectsTruncatedOrUnknownState` |
| `EmulationSessionTest.java` | `stopBeforeStartCannotResurrectSession`<br>`pausePublishesZeroInputBeforeStoppingClock`<br>`resumeIsLegalOnlyAfterPause` |
| `SessionSchemaRegistryTest.java` | `schemaCopyMatchesFrozenRegistryHash` |
| `ControlLayoutRepositoryTest.java` | `persistsAndRecoversMalformedDataToRecommended`<br>`loadMigratesLegacyPrefsIntoNativeBackendOnce`<br>`loadDecodesNativeStringAndRoundTripsRecommendedEncode` |
| `FlySettingsMapperTest.java` | `frozenCEnumsAreOneBasedNotJavaOrdinals`<br>`roundTripMapsEveryFieldThroughNativeSnapshot` |
| `MotionRiskConsentStoreTest.java` | `clickUsesFreshTrustedSnapshotForBothAcceptanceTimes`<br>`dialogCrossingExpiryAndGenerationCasFailureSaveNothing`<br>`everyRequestRechecksIdentityClockAndValidity` |
| `NativeSettingsStoreTest.java` | `settingsRepositoryRoundTripsThroughNativeSnapshotBackend`<br>`failedApplyLeavesPreviousNativeSnapshotUnchanged` |
| `SettingsBatchTest.java` | `exposesImmutableTypedValuesAndRemovals`<br>`rejectsNullDuplicateAndCrossTypeKeys` |
| `SettingsRepositoryTest.java` | `defaultsMatchApprovedDesign`<br>`persistedLegacyDirectionNamesAndDeadZonesKeepTheirMeaning`<br>`roundTripPersistsEverySetting`<br>`migrationKeepsLegacyNonVideoPreferences`<br>`controlsResetShapeCanPreserveVideoAndGeneralSettings` |
| `SettingsSectionTest.java` | `frozenMasterDetailOrderMapsEveryCategoryToOneRoot` |
| `VideoSettingsMigrationTest.java` | `cleanInstallCreatesBalancedCanonicalSettings`<br>`migratesEveryEvidencedLegacyFilterAndRefreshSpelling`<br>`markerlessLegacyFootprintMigratesButNoDisplayFootprintUsesBalanced`<br>`partialLegacyDisplayUsesHistoricalAutoRefreshDefault`<br>`ignoresInterruptedSchemaFourFieldsDuringLegacyMigration`<br>`corruptSchemaFourVideoAtomicallyDefaultsAndPreservesOtherSettings`<br>`wrongRawTypesAreCorruptAndMissingGenerationRepairsOnlyMetadata`<br>`futureSchemaUsesMemoryDefaultsWithoutWriting`<br>`failedCanonicalCommitIsRetriedWithoutExposingPartialState`<br>`failedCommitThatChangedMemoryIsRetriedForDurability`<br>`newerSuccessfulSaveSupersedesAnOlderFailedIntent`<br>`pendingMigrationNeverOverwritesAFutureSchema`<br>`pendingSaveNeverOverwritesANewerSchemaFourGeneration`<br>`migrationVocabularyIsSchemaAwareAndInvalidRefreshUsesAuto`<br>`balancedRoundTripPreservesCustomRequestWithoutInjectingMmpx` |
| `AudioMarkerTimelineTest.java` | `markerUsesSegmentStartInsteadOfOneFrameBlockEnd`<br>`crossingBlockKeepsDistinctSequenceStartMarkers` |
| `AvSyncMonitorTest.java` | `usesMappedPlaybackAndPresentationTimestampsNotQueueDepth`<br>`uncalibratedOrNegativeTimestampsFailClosed`<br>`contentGateRejectsDifferentAudioAndVideoFrames`<br>`oldOrExplicitlyInvalidatedSampleFailsClosed` |
| `DisplayLeaseWatchdogTest.java` | `deadlineExpiresIndependentlyOfMonitorProgress`<br>`staleEpochOrGenerationCannotReuseFreshDeadline`<br>`independentDeadlineCallbackFiresWhenObservationThreadIsStalled` |
| `MotionShadowEvidenceTest.java` | `derivesStrictArtifactContinuityGpuAndLeaseGates`<br>`unavailableTimingNewFailureOrStaleLeaseFailsClosed`<br>`earlierOverBudgetGpuSampleCannotBeHiddenByFastLastSample` |
| `SurfaceRecoveryIntentPolicyTest.java` | `generationQualificationAbortCannotConsumeResumeIntent`<br>`repeatedSurfaceLossCannotLetOldEpochConsumeNewRecoveryIntent`<br>`sameEpochSafeMotionOrNativeFallbackCanCompleteRecovery` |
| `TemporalAudioDelayTest.java` | `oneFrameDelayWritesSilenceThenPreviousFrameWithPartialWrites`<br>`flushDropsTimestampedPcmAndRestartsWithSilence`<br>`removalDropsOneFrameAndAppliesEightMillisecondEqualPowerCrossfade`<br>`sinkFailureStopsWithoutSpinning`<br>`partialWriteFailureCommitsOnlyPlayedPcm`<br>`incompleteDelayRemovalIsRejectedWithoutChangingMode`<br>`reportsEveryRealContentSpanAndExcludesInsertedSilence` |
| `TemporalTransitionControllerTest.java` | `allOrdinaryMotionFailuresEnterBufferedHold`<br>`surfaceAndContextLossFreezeQueuesUntilPausedRecoveryTransaction`<br>`thermalDrainAndNoRunningFailureJumpsStraightToImmediateNative`<br>`holdWaitsFullCooldownBeforeStartingNonPresentedShadow`<br>`recoveryRequiresTwoSecondShadowThenThreeSecondFreshLease`<br>`badShadowResetsFullCooldown`<br>`discontinuityGpuFailureOrLeaseLossFailsClosedImmediately`<br>`shadowHeartbeatOutlivesTwoSecondWindowAndThreeSecondQualification` |
| `ClockDomainCalibratorTest.java` | `mapsNativeTimeUsingTheLowestUncertaintyPairedSample`<br>`rejectsMoreThanOneMillisecondUncertaintyAndTimeAnomalies` |
| `DisplayModeControllerTest.java` | `gamesUseDefaultFrameRateCompatibilityRatherThanVideoFixedSource`<br>`reportsARealModeFallbackAfterTheDisplaySettles` |
| `DisplayModeSelectorTest.java` | `autoChoosesHighestNativeResolutionRefreshRate`<br>`request120ChoosesMatchingNativeResolution`<br>`unsupported90FallsBackToSystemAuto`<br>`neverChoosesAHighRefreshModeAtTheWrongResolution`<br>`autoPrefersStable60Over90When120IsUnavailable` |
| `DisplayRequestLifecycleTest.java` | `fixed120ClearsOldVoteThenSelectsModeThenQueuesNativeSourceVote`<br>`followSystemAndPauseClearNativeBeforeClearingWindowPreference`<br>`palUsesCoreConfirmed50FpsAndNeverSelectsWrongResolution`<br>`failedNativeClearLeavesExistingWindowModeUntouched`<br>`failedNativeVoteWithConfirmedClearFallsBackToSystem`<br>`failedRollbackClearRetainsWindowModeUntilNativeIsConfirmedClear` |
| `FrameBufferPoolTest.java` | `leasesBoundDirectStorageAndReturnItExactlyOnce` |
| `FrameDispatchExecutorTest.java` | `nativePolicyCoalescesToLatestAndReportsSkippedSequences`<br>`closeDropsPendingWorkAndOldOrDuplicateSequences`<br>`motionPolicyPreservesEveryAdjacentSequenceInOrder`<br>`motionPolicyFailsClosedOnSequenceGap`<br>`motionPolicyFailsClosedBeforeOverwritingBoundedRing`<br>`productionMotionCaptureCompletesBeforeOfferReturns`<br>`productionMotionFailureNotifiesAndCanBeReprimed` |
| `FramePublisherTest.java` | `emitsOnlyWhenSequenceChanges`<br>`resetAcceptsASequenceFromANewCoreInstance`<br>`rejectsIncompleteOrRegressingFrames`<br>`mapsValidatedNativeMetadataToAReadOnlyFrame`<br>`rejectsNativeMetadataThatExceedsTheDirectBuffer`<br>`nativeFrameSourceReturnsNullOnBridgeErrorAndMapsSuccess`<br>`observersReceiveOnlyAcceptedUniqueFrames` |
| `FrameRendererConfigTest.java` | `everyFilterUsesARealPurposeBuiltShader` |
| `InputLatencyTrackerTest.java` | `endpointIsCalibratedCoreSampleNotJavaWriteTime`<br>`unknownGenerationOrUncalibratedClockProducesNoMetric` |
| `NativeFrameSourceTest.java` | `newNoChangeAndErrorAreNotConfusedWithSequenceValues`<br>`resultFactoriesRejectAmbiguousStatusValues` |
| `NativeTime120ContractTest.java` | `oneHundredTwentyDisplayTicksDoNotDuplicateSixtySourceFrames` |
| `NativeVideoPresenterTest.java` | `copiesEachPublishedSequenceAtMostOnce`<br>`destroyRejectsFramesAndStaleLifecycleCallbacks`<br>`frameRateVotesAreBoundToTheActiveSurfaceEpoch`<br>`destroyTimeoutKeepsEpochClearableForCompensation`<br>`actualPresentationLookupIsSequenceAddressed`<br>`shadowPrimingIsBoundToActiveEpochAndReturnsTransitionId` |
| `AdaptiveQualityControllerTest.java` | `criticalPauseHasPriorityOverEveryGraphDowngrade`<br>`severeAndBatteryPoliciesWalkExactlyOneExplicitEdge`<br>`optionalDegradationRequiresOptInAndRecoveryNeedsContinuousSafeWindow`<br>`unsafeSampleResetsRecoveryTimer` |
| `ThermalPowerMonitorTest.java` | `lifecycleRegistrationIsIdempotentAndHeadroomPollingIsBounded`<br>`unsupportedHeadroomNeverInventsAHotterOrCoolerBand` |
| `BundledAlgorithmAvailabilityTest.java` | `currentBuildPublishesPinnedSpatialImplementationsOnly`<br>`publishedHashesMatchThePackagedImplementationSources` |
| `DisplayMotionLeaseTest.java` | `motionWaitsForThreeSecondsOfFreshSameGenerationOneTwentyHertz`<br>`activeMotionRequiresContinuousFreshLeaseAndRetainsDelayOnLoss`<br>`previousGenerationAndIncompatibleActiveModeImmediatelyStopActiveMotion` |
| `DisplayQualityResolverBaselineTest.java` | `balancedWithoutQualifiedMmpxIsFullyAppliedSharp`<br>`bundledMmpxWithUnknownGlCapabilityDoesNotUnlockMmpx`<br>`fixedNinetyRequestRetainsSystemReportedSixtyAsFallbackFact`<br>`advancedCodeAndGlWithoutCertificateStillFailClosed`<br>`nativeTime120WithoutPhysicalCertificateFallsBackToSixty`<br>`severeAndCriticalThermalSignalsRequestSafetyWithoutFakingActiveMode` |
| `DisplayQualityResolverQualificationTest.java` | `balancedUsesExactQualifiedMmpxConfiguration`<br>`changedAspectAndAlgorithmHashDoNotReuseCertificate`<br>`expiredOrUntrustedEvidenceNeverUnlocksAdvancedTuple`<br>`qualifiedNative120RequestsDesiredModeThenFallsBackAtomically` |
| `EvidenceValidityPolicyTest.java` | `deviceLabUsesExactHalfOpenExpiryAndOneHundredEightyDayMaximum`<br>`compatibilityEvidenceAllowsAtMostThreeHundredSixtyFiveDays`<br>`invalidRangesAndFutureIssuanceFailClosedBeforeExpiry` |
| `EvidenceValidityWatchdogTest.java` | `monotonicDeadlineCanOnlyMoveEarlier`<br>`tombstoneMustPersistBeforeExpiryIsPublished`<br>`untrustedClockCannotScheduleOrValidateCertificate` |
| `LegacyVideoRuntimeAdapterTest.java` | `projectsPresetModesWithoutMutatingPreservedCustomSettings`<br>`customProjectionKeepsFollowSystemDistinctAndFallsBackHonestly`<br>`legacyAutoAndFixedPoliciesProjectExactly`<br>`effectiveProjectionUsesResolvedAxesInsteadOfRequestedAdvancedAxes` |
| `PresenterFailureMapperTest.java` | `nativeSpatialFailuresRemainTypedForResolverFeedback` |
| `TrustedEvidenceClockTest.java` | `sameBootAdvancesFromAuthenticatedAnchorDespiteSmallWallRollback`<br>`lastValidatedTimeRemainsAOneWayFloor`<br>`missingAnchorDifferentBootAndElapsedRollbackFailClosed`<br>`clockDivergenceBeyondTwentyFourHoursAndOverflowAreUntrusted` |
| `MmpxOracleFixtureTest.java` | `matchesOfficialReferenceFixturesExactly`<br>`preservesTheSourcePalette`<br>`exposesPinnedReferenceIdentity` |
| `DisplayCapabilitiesReaderTest.java` | `retainsFullModeIdentityAndFiltersDifferentResolution`<br>`missingCurrentModeFailsClosed` |
| `DisplayStatusMonitorTest.java` | `freshnessUsesHalfOpenLeaseAndReadFailureIsImmediatelyUnknown`<br>`blockedPollCannotPreventIndependentLeaseDeadline`<br>`newGenerationInvalidatesOldDeadlineAndIncompatibleModeFailsImmediately`<br>`fixedMismatchBecomesPersistentAtThreeSecondsButFollowSystemDoesNot`<br>`nativeFixedModePollsWithoutMotionAndComparesFullModeIdentity`<br>`sameModeToleratesRefreshReportingJitter`<br>`timeRollbackPublishesUnknown` |
| `GlCapabilityProbeTest.java` | `publishesUnknownThenKnownAndDestroysBothOwnedContexts`<br>`failedProbeRemainsUnknownAndStillDestroysEs2Session`<br>`contextLossInvalidatesPreviouslyKnownCapabilities` |
| `VideoStatusAccumulatorTest.java` | `palNativeMetricsNeverAssumeSixtyFramesPerSecond`<br>`activeIdentityIsPublishedAtomicallyAndClearedDuringTransition`<br>`requestedAndSystemReportedModesRemainSeparateFacts`<br>`freshnessIsDerivedAtReadTimeWithHalfOpenLease`<br>`avSyncEvidencePublishesValiditySkewAndUncertainty` |
| `VideoStatusRepositoryTest.java` | `observerGetsCurrentAndFutureValuesUntilClosed`<br>`repositoryIsInProcessAndStartsWithoutInventedRuntimeState` |
| `ViewportLayoutTest.java` | `fourByThreeUsesFullHeightWithoutEnteringNavigationInset`<br>`squarePixelsPreserveCoreAspect`<br>`integerScaleDoesNotExceedSafeArea` |

### Android instrumentation

| 文件 | 已声明测试方法 / it 名称 |
|---|---|
| `AppLanguageTest.java` | `chineseAppLocaleAppliesToTheLauncher` |
| `AndroidAtomicCatalogStateStoreTest.java` | `roundTripsAndOversizeFailurePreservesLastGood`<br>`recoversAtomicBackupWithoutOverwritingIt` |
| `AndroidBuiltinCatalogAdapterTest.java` | `licensedBuiltinUsesSharedScannerVerifiedBilingualTitlesAndStrictLoader`<br>`strictOpenerReportsPersistedPermissionLossBeforeProviderOpen` |
| `AndroidCatalogLaunchRegressionTest.java` | `nativeZipScanReopensANonFirstEntryAfterRestart`<br>`theStorageProviderRejectsATreeLocatorWithAnAppendedPath`<br>`derivesAnOpenableDocumentLocatorFromTheTreeLocator`<br>`derivesTheCanonicalTreeRelativePathFromAChildDocumentId`<br>`coldStartProjectionLaunchesTheRomFromTheRebuiltLocator`<br>`aCatalogCarryingTheCrashLocatorFailsInsteadOfKillingTheLaunchThread` |
| `AndroidCatalogRuntimeTest.java` | `restartRestoresCatalogAndCorruptionDoesNotOverwriteFile` |
| `AndroidDocumentTreeGatewayTest.java` | `childCursorReadsAtMostBudgetPlusOneAndDoesNotMaterializeOverflow`<br>`nonTreeLocatorBecomesFatalWithoutProviderWriteAccess` |
| `AndroidLargeCatalogPerformanceTest.java` | `coldLibraryAndRepeatedNavigationWith2224Games` |
| `AndroidLegacyRomStoreReaderTest.java` | `readsWithoutClearingLegacyKeysAndMarkerIsSeparate` |
| `AndroidNativeSourceRegistrationTest.java` | `registeredSourceSurvivesProjectionRestartAndCanBeScanned` |
| `AndroidPendingReleaseStoreTest.java` | `tombstoneSurvivesNewInstanceUntilExplicitClear`<br>`legacyRemovalTombstoneCanBeClearedByTypedActionId` |
| `PersistedReadPermissionGatewayTest.java` | `pickerRequestsOnlyReadPersistablePrefix` |
| `ControlLayoutActivityTest.java` | `settingsIsTheOnlyEntryAndOpensEditor`<br>`dragSaveIsRestoredByNextEditorAndGameHitMap`<br>`editorExposesFiveIndependentVirtualControlsAndClick`<br>`editorHandlesAllDirectionModesNeutralCentersAndButtonPriority` |
| `AndroidCoverRepositoryTest.java` | `storesAtomicHashedFourByThreePngAndLoadsIt` |
| `CoverCaptureIntegrationTest.java` | `gameplayPublishesAGameOnlyCoverWithoutTouchOverlay` |
| `DisplayQualitySettingsTest.java` | `presetCardsStatusLayersAndTouchTargetsAreVisible`<br>`customCardRevealsAxesAndPersistsOneAtomicPreset`<br>`lockedExtremeRemainsFocusableAndExplainsWhy` |
| `GamepadAccessibilityTest.java` | `gamepadExposesEightControlsAndVirtualAProducesInput` |
| `GamepadCancelTest.java` | `resetClearsEveryPointerAndPublishesZero`<br>`zeroDurationATapSurvivesLongEnoughForTheCoreToSampleIt`<br>`minimumTapReleaseDoesNotWaitForTheMainLooper` |
| `GamepadJoystickContinuityTest.java` | `dragCanLeaveOriginalBaseAndReverseWithoutAnotherDown`<br>`coalescedMoveHistoryIsProcessedChronologicallyAndPublishedOnce`<br>`coalescedMoveProcessesEveryPointerBeforeCurrentPositions`<br>`pointerDownAppliesSurvivorCoordinatesBeforeAddingNewRole`<br>`actionUpUsesFinalCoordinateBeforeMinimumTapDecision`<br>`pointerUpAppliesSurvivorCoordinatesBeforeRemovingRole`<br>`insetsUpdateKeepsActiveDirectionVisualAndPublishedSession`<br>`longMoveExposesTheSameBoundedVisualSessionUsedForDrawing`<br>`systemGestureExclusionTracksIdleActiveInsetsAndSize`<br>`dpadModeHasBoundedLocalSystemGestureExclusion`<br>`farRightExclusionRemainsLocalWithoutTrimmingActiveBase`<br>`directionReleaseIsImmediateAndNeverMinimumTapPulsed`<br>`acceptedTouchOwnsParentInterceptionUntilEveryTerminalPath`<br>`pointerUpKeepsParentInterceptionUntilTheFinalPointerEnds` |
| `GamepadTouchDispatchTest.java` | `fixedRootDispatchStaysOwnedAtFiveRadiiAndReversesWithoutZero`<br>`historicalSamplesProduceOneDirectionTickForTheWholeEvent`<br>`newPrimaryDownClearsStaleTouchButPreservesKeyboardSource`<br>`flaggedPointerUpAndFrameworkCancelResetWithoutReturnAnimation`<br>`stablePointerIdsSurviveIndexReorderingAndKeepDirectionRoleAcrossButtons`<br>`secondDirectionPointerCannotStealThroughRootDispatch`<br>`dpadRootDispatchOrbitsThreeRadiiAndReturnsWithoutDroppingOwner`<br>`directionHapticsTickOnActivationAndSectorChangesWithEightyMsCooldown`<br>`insetsAreIdempotentModeSwitchCancelsDirectionAndResizeEndsSession`<br>`cancelClearsMinimumTapPulseImmediatelyButPreservesKeyboardSource`<br>`releasingButtonDoesNotConsumeSurvivingDirectionSectorFeedback`<br>`dpadHighlightsAccessibilityAndKeyboardDirectionFeedback` |
| `GameTitleIndexTest.java` | `newlyLoadedBytesCannotReuseAStaleLegacyTitle`<br>`everyBundledFingerprintIgnoresNamesAndSwitchesBothLanguages`<br>`changingLanguageKeepsTheRunningGameTitle` |
| `HapticSettingsPersistenceTest.java` | `hapticLevelAndDistinctABPersistAcrossRecreation` |
| `MotionComputeParityTest.java` | `es31IntermediateMatchesPortableRgb565Oracle` |
| `MotionContextFallbackTest.java` | `es31CreationFailureIsExplicitAndNeverReturnsSyntheticPixels` |
| `MotionShadowPresenterTest.java` | `shadowComputesAdjacentPairsWithoutPresentingMidpoints`<br>`queuedHoldCanUpgradeToDrainWithExactTransitionGeneration`<br>`shadowSurfaceLossRejectsOldEpochAndRecreatesNativeOwner` |
| `NativePresenterInitializationTest.java` | `repeatedConstructionNeverExposesUninitializedWorkerState` |
| `NativePresenterIntegrationTest.java` | `resolvedBaselineConfigurationIsPublishedAtomically`<br>`gameplayUsesOneEventDrivenNativeSurfaceOwner`<br>`nativeCoordinatorOwnsSourceRateVoteAndPauseClearsIt`<br>`everyBaselineFilterCompilesAndSubmitsOnDevice`<br>`pauseStopsCallbacksAndResumeRecreatesAWorkingPresentationPath` |
| `NesFrameSnapshotTest.java` | `copiesACompleteNativeFrameAfterOneEmulatedFrame` |
| `ProductOrientationTest.java` | `libraryAndLicensesKeepTheLandscapeHandPosition` |
| `RomIdentityTest.java` | `builtinRomUsesCoreSha1` |
| `LegacySaveMigratorTest.java` | `validLegacyStateIsCopiedOnceAndOriginalIsRetained` |
| `SaveRepositoryTest.java` | `savesAreIsolatedBySha1`<br>`failedWritePreservesPreviousState` |
| `SettingsMasterDetailTest.java` | `resetControlsRestoresLayoutAndFeedbackDefaultsTogether`<br>`allFiveMasterSectionsSwitchTheDetailPane`<br>`twoHundredPercentFontKeepsMasterTargetsVisible`<br>`licensesSelectReturnAndReadFailureRetry` |
| `SettingsPersistenceTest.java` | `directionModesAreOrderedAndCleanDefaultThenFollowPersist`<br>`hapticAndAspectPersistAcrossRecreation` |
| `SpatialCapabilityFallbackTest.java` | `ownedProbePublishesConcreteDriverFacts`<br>`mmpxRequiresHighpAndSufficientTextureSizeButNotFloatTargets`<br>`scaleFxFailsClosedWithoutRenderableAndFilterableFloatIntermediates` |
| `SpatialFilterGoldenTest.java` | `nearest2xIsPixelExact`<br>`mmpx2xMatchesTheFrozenIndependentOraclePixelForPixel`<br>`scaleFx3xMatchesPinnedUpstreamReferenceWithinOneLsb` |
| `StartAndPauseSeparationTest.java` | `nesStartDoesNotOpenPauseButPauseButtonDoes`<br>`controlsKeepErgonomicBoundsAndPauseIsAppOnly` |
| `SwappyFailClosedTest.java` | `incompatibleModeAndDisabledPacerCannotActivateMotion` |
| `BaseVideoCertificationTest.java` | `native60NearestBaseRow`<br>`balancedSharpBaseRow` |
| `NativeTime120CertificationTest.java` | `nativeTimeAt120HzSubmitsOnlyUniqueSourceFrames` |
| `AppIconResourceTest.java` | `manifestUsesFlynesLauncherIcons` |
| `FirstRunNavigationTest.java` | `firstRunShowsBuiltinLibraryAndSettingsActions`<br>`primaryActionsMeetAndroidTouchTargetMinimum`<br>`sourceManagerStaysInsideTheGameCenter`<br>`searchAndCategorySurviveActivityRecreation`<br>`builtinUsesTheUnifiedLaunchPath`<br>`selectedGameCanBeAddedToAndSeenInFavorites`<br>`homeActivityIsTheLauncher`<br>`largeFontKeepsStatusCardAndCtaFullyVisible`<br>`bothLandscapeSensorDirectionsAreAllowed` |
| `HomeContinuousLibraryTest.java` | `libraryIsTwoAdaptiveRowsWithHorizontalScrollingOnly`<br>`paginationControlsNoLongerExist` |
| `NearbyFriendsManageTest.java` | `allActionsArePresentAndDisabledWithAReason`<br>`friendsTabOpensTheManagePage`<br>`settingsRowOpensTheManagePageWithoutAddingASixthSection` |
| `NearbyFriendsTest.java` | `gameCenterShowsTheNearbyEntry`<br>`nearbyEntryOpensTheNearbyPage`<br>`pageLandsOnDevicesTabWhileNoFriendIsSaved`<br>`onlyTheFirstFailingStageExplainsItself`<br>`discoveryControlsArePresentButDisabledWithAReason`<br>`devicesTabCarriesTheOnePermittedNavigateOnlyEntryIntoPairing`<br>`friendsTabShowsOnlyTheEmptyStateAndItsBlockedReason` |
| `NearbyInGameStatusTest.java` | `theGuardKeepsEveryNearbyRowOutWithoutASession`<br>`localSinglePlayerShowsNoNearbyStatusOnTheRunSurfaceOrInTheDrawer` |
| `NearbyLobbyTest.java` | `everyLobbyFieldIsRendered`<br>`eachFieldStatesWhyItIsUnavailable`<br>`confirmIsOneDisabledPrimaryActionWithAReason` |
| `NearbyPairingTest.java` | `sixDigitCodePathIsPresentAndCannotConfirmWithoutASession`<br>`wifiPathIsExplainedAndStatesTheSingleSystemPrompt`<br>`pipelineMarksOnlyTheFirstFailingStage`<br>`anonymousJoinControlSetIsNotBuilt` |
| `ThemeContrastTest.java` | `primaryTextMeetsWcagAaOnEveryDarkSurface`<br>`mutedTextAndAccentRemainLegible`<br>`primaryButtonLabelMeetsWcagAa` |
| `GlCapabilityProbeInstrumentedTest.java` | `boundedOwnedProbeReturnsARealEs2Identity` |

### Harmony Hypium

| 文件 | 已声明测试方法 / it 名称 |
|---|---|
| `CatalogProductService.test.ets` | `projects_cached_titles_using_simulator_setting_before_system`<br>`keeps_maximum_popularity_across_exact_content_copies`<br>`publishes_one_card_per_canonical_game_and_keeps_builtin_first` |
| `CheckpointStore.test.ets` | `keeps_per_rom_keys_isolated_and_path_safe` |
| `ControlLayoutWarnings.test.ets` | `flags_too_small_when_scale_below_067`<br>`flags_gesture_zone_when_outside_safe_bounds`<br>`flags_central_protection_when_center_x_in_middle`<br>`flags_overlap_when_controls_too_close`<br>`clean_recommended_layout_has_no_warnings`<br>`push_caps_at_20_and_undo_pops_newest`<br>`undo_on_empty_returns_undefined` |
| `LicenseModel.test.ets` | `maps_known_license_files`<br>`resolveBody_throws_when_asset_missing`<br>`lists_four_entries` |
| `NearbyService.test.ets` | `renders_the_seven_pipeline_stages_in_canonical_order`<br>`marks_only_the_first_failing_stage_and_never_annotates_later_rows`<br>`shows_earlier_stages_as_passed_when_a_later_stage_fails`<br>`falls_back_to_permission_because_no_session_stage_exists_yet`<br>`renders_every_lobby_field_with_its_blocked_key_and_one_confirm_control`<br>`covers_every_in_game_field_and_keeps_the_no_auto_merge_invariant_unblocked`<br>`offers_the_three_authority_timeout_options_with_a_reason_each`<br>`pins_no_banner_in_local_single_player_and_no_banner_when_nothing_demands_action`<br>`pins_a_non_dismissible_banner_only_for_the_action_demanding_states`<br>`keeps_every_friends_management_action_disabled_on_the_friend_store` |
| `PauseParams.test.ets` | `uses_title_param_when_present`<br>`falls_back_when_title_missing_or_blank` |
| `SettingsHelpers.test.ets` | `maps_aspect_mode_values`<br>`haptic_preview_plays_a_then_b_with_260ms_gap`<br>`computes_physical_4_3_square_and_integer_viewports`<br>`maps_saved_app_language_to_runtime_language`<br>`uses_only_explicit_loopback_debug_sideload_endpoints`<br>`encodes_chinese_sideload_names_as_utf8_uri_components` |
| `Smoke.test.ets` | `hypium_scaffold_runs` |

### iOS XCTest

| 文件 | 已声明测试方法 / it 名称 |
|---|---|
| `CatalogSourceImportTests.mm` | `testDirectoryImportIndexesZipAndChineseFilenamesAndSurvivesRestart`<br>`testZipPackageUsesOuterNameForEnglishAndEntryNameForChinese`<br>`testRepeatedFileImportNeedsOnlyOneRemoval`<br>`testRepeatedFolderImportNeedsOnlyOneRemoval`<br>`testRemovingLegacyDuplicateSourceClearsAllCopies`<br>`testRemovingLegacySourceWithMissingIndexStillClearsDuplicates`<br>`testDistinctSourcesWithSameGameRemainIndependent`<br>`testDuplicateGamesAreCanonicalizedToOneRow`<br>`testTwoDistinctGamesPresentAndReopenWithoutCollectionMutation`<br>`testBuiltinPlusOneImportedGamePresentsWithoutCollectionMutation`<br>`testHundredDistinctGamesWithDuplicateVariantsSearchFavoriteAndRestart`<br>`testCancelledAndUnsupportedImportsAreRefused` |
| `CatalogTitleCoverTests.mm` | `testIndexedMetadataSurvivesRenameAndProjectsBothLocales`<br>`testTrustedBuiltinPresentsAndroidBilingualTitle`<br>`testExternalFilenamesNeverBecomeTranslations`<br>`testZipOuterFilenameOutranksSameLanguageEntryName`<br>`testAliasSearchCoversFilenameEntryAndBothLanguages`<br>`testUnclassifiedScriptStillHasAPlaceholderTitle`<br>`testFirstObservedFrameOnlyStartsTheSessionClock`<br>`testQualityGateRejectsBlackAndRequiresARealImprovement`<br>`testExposurePenaltyRejectsWhiteAndBlackFrames`<br>`testStoredCoverIsPersistedResizedAndReloadable`<br>`testMalformedFramesAndIdentifiersAreRefused` |
| `GamepadOverlayTests.mm` | `testTwoFingersOnOneButtonRetainPressUntilBothReleased`<br>`testSlidingOffButtonEndsOwnershipUntilNextTouchDown`<br>`testRollFromBToAAccumulatesBothButtons`<br>`testFollowingJoystickEdgeCaptureIsNeutral`<br>`testEndingOnAConsumesFinalCoordinatesOfBRoll`<br>`testLocalCancellationDoesNotInvokeGlobalInputReset`<br>`testAllEightAccessibleControlsCanActivate`<br>`testResizingCancelsHeldInput`<br>`testPressedFaceButtonChangesRenderedAppearance`<br>`testCompletedShortTapSurvivesAnotherPointersCancellation`<br>`testCoalescedSamplesRetainTheDeliveredTouchOwner`<br>`testDirectionHapticsHaveAnEightyMillisecondCooldown`<br>`testDpadCornerIsTransparentInsteadOfSolidSquare` |
| `MetalPixelParityTests.mm` | `testMmpxDiagonalMatchesIndependentBinaryGolden`<br>`testMmpxCheckerboardMatchesIndependentBinaryGolden`<br>`testMmpxOddThinLineMatchesIndependentBinaryGolden`<br>`testScaleFxDiagonalMatchesIndependentBinaryGolden`<br>`testScaleFxCheckerboardMatchesIndependentBinaryGolden`<br>`testScaleFxOddThinLineMatchesIndependentBinaryGolden` |
| `PlaybackAudioPlayerTests.mm` | `testRealGamePCMReachesRunningAudioOutput` |
| `PlaybackRuntimeBridgeTests.mm` | `testFramesProduceVideoAndBoundedPCMWithoutInputChanges`<br>`testCheckpointBeforeFirstFrameCanResume`<br>`testCheckpointRestoresStableNextFrameAndClearsAudio`<br>`testUnconsumedAudioCannotAccumulateAcrossFrames`<br>`testCheckpointRestoresAnExternalTimelineEpoch` |
| `PlaybackSoakTests.mm` | `testSustainedPlaybackKeepsFramesAudioAndMemoryBounded`<br>`testRepeatedPauseAndResumeCyclesKeepCatalogStable` |
| `ProductImportUITests.mm` | `testSingleFilesImportWithBuiltinFavoriteRestartAndRemove`<br>`testHundredGameDirectoryDuplicateAliasesSearchRescanRestartAndRemove`<br>`testCancelFilesPickerPreservesExistingLibrary` |
| `ProductUITests.mm` | `testControlsResetRestoresDistinctButtons`<br>`testAndroidGameCenterSelectionStaysBesideGrid`<br>`testAndroidBilingualTitlesAndAutomaticCoverCapture`<br>`testSettingsPersistAcrossRestartAndFollowInterfaceLanguage`<br>`testLibraryLaunchPauseSettingsResumeAndReturn`<br>`testNearbyEntryOpensThePageOnTheDevicesTabWithDisabledDiscovery`<br>`testNearbyPipelineMarksOnlyTheFirstFailingStage`<br>`testNearbyFriendsTabOpensTheManagePageWithDisabledActions`<br>`testNearbyPairingEntryOpensThePairingPage`<br>`testSettingsRowOpensTheNearbyManagePage` |
