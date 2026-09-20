# 全功能测试增强实施计划

> 后续执行按 `executing-plans` 逐批实施；本文只交付计划与完整用例，不执行产品改动。

**目标：** 为当前三端功能建立可核查的UT覆盖率门槛，使每项常用功能至少具有对应平台模拟器端到端测试，并让每个发布候选包经过明确的真机必测集。  
**架构：** 共享语义测试、平台组件测试、真实UI旅程、物理设备验证分层计量；同一功能ID贯穿源码、用例、runner和证据。  
**技术栈：** 现有C/C++ CTest、JUnit/Espresso、Hypium/ArkTS、XCTest/XCUITest、Python/Pester；逐步补coverage与统一门禁，不为报表强行升级产品工具链。

日期：2026-09-13；审计基线 `main@661e84bc97023676882d5f8ffbbd97634d25e8b3`，`VERSION=1.1.12`。

## 1. 交付物与结论

本计划与两份附件共同构成完整交付：

1. **[测试增强完整用例清单](2026-09-13-test-enhancement-cases.md)**：239条定义，逐条列出ID、平台、优先级、前置、步骤、断言和实现落点；其中97条模拟器E2E展开为288个平台实例，22条真机用例展开为65个平台计划实例。
2. **[当前测试资产清单与接入审计](2026-09-13-test-coverage-inventory.md)**：测试文件、声明数量、CTest注册和未接入原生测试、CI范围。
3. 本文：功能覆盖差距、硬门槛、实施批次、执行频率、证据规则和验收条件。只看本文模块名称不能标任务完成，必须落到附件中的用例ID。

当前最需要增强的不是“再增加一些UT数量”，而是 **Harmony缺少页面E2E、三端存档/授权/生命周期闭环不足、部分iOS测试未注册、覆盖率与测试结果没有统一门禁**。Android已有大量有价值的测试，但instrumentation包含直接调用View/bridge/provider的集成测试，不能整体计为用户E2E。

本次重新做了源码和runner配置盘点，没有全量执行测试、安装应用或生成覆盖率。不存在可引用的当前UT行/分支百分比，统一标为 **UNKNOWN**。本会话上一轮已在同一SHA复现的LF检出与Harmony契约失败仍列入P0基线修复，详见[主干现状与联机计划](2026-09-13-main-status-nearby-device-test-plan.md)。不读取或改变现有签名配置，不提交Git。

## 2. 当前测试覆盖实际含义

| 层级 | 当前声明/接入 | 不能推出的结论 |
|---|---|---|
| Android JVM | 93文件、448个@Test | 不能推算行/分支覆盖率；旧XML的448含2个symlink条件skip |
| Android instrumentation | 49文件、133个@Test | 含UI、组件、native/GPU和3个默认排除的真机认证用例，不等于133个模拟器E2E |
| Harmony Hypium | 8个用例文件、33个it | 32个逻辑用例+1个常量scaffold，没有Driver页面操作；不能称端到端覆盖 |
| iOS XCTest | 当前CMake注册50个Runtime方法、13个UI方法 | Runtime直接step、bridge、audio/GPU测试不能全部算UI；历史49/49结果不代替当前50方法的执行 |
| iOS独立native程序 | 14个源码未接入所查CMake/正式runner | 部分历史手动执行过；缺持续入口，不能算每次回归已覆盖 |
| core/shared CTest | core2项；shared49常规+1个NDK条件项 | 编号总数受configure参数影响；不能复用旧build数量 |
| Harmony/iOS host | 12/9项CTest声明 | 不证明页面、平台权限或物理表现 |
| 覆盖率设施 | 所查build/CI未发现可执行coverage阈值门禁 | 当前值UNKNOWN，不是0%、也不是默认达标 |

CI需区分两件事：`scripts/ci-check.ps1` **已经**包含shared全CTest；`.github/workflows/stage0.yml`只跑core smoke及头文件编译。`ios-stage1.yml`执行portability模拟器smoke；`ios-product.yml`侧重device构建和unsigned IPA，没有开启产品XCTest。因此不能沿用“所有脚本只测core”的旧结论，也不能把构建产物工作流当成三端产品回归。

其他已确认缺口：

- Harmony `CheckpointStore.test.ets`只覆盖key，不覆盖read/write/fsync/rename/quarantine；存档重点补C009与E041–E045。
- Android布局现有测试直接dispatch到View，未完整验证保存后游戏新坐标真正响应；用E064闭环。
- iOS测试bundle目前只在simulator条件编译，runner依赖模拟器容器路径；真机需独立签名/执行配置，不能拿现有脚本直接替换UDID。
- iOS部分Swift/ObjC++文件是`main/@main`程序，不是XCTest类；接入时建独立可执行目标或迁移，不可直接加入bundle造成main冲突。
- 历史UiAutomation失败、Files夹具失败、GPU能力排除、Harmony Release缺exe/裸assert问题先修复或明示阻塞，不能通过跳过必测项制造绿色结果。

## 3. 全功能盘点与差距矩阵

“有测试”仅表示发现相关源码，不代表本轮通过。每行功能的具体UT/C/E/D编号由完整用例附件以同一F-ID关联；新增功能不得以“不在列表”逃避登记。

| 功能ID | 当前功能 | 现有覆盖资产概况 | 必须补足的结果 |
|---|---|---|---|
| F01 | 冷启动、离线单机、中心导航 | A有FirstRunNavigation；H无UI旅程；I有启动/暂停旅程 | 三端补真实产帧与离线冷启动结果，不能只检查容器存在 |
| F02 | 分类、选中详情、导航恢复 | A有GameCenterState/FirstRunNavigation；H只有共享或service逻辑；I有选择与导入重启用例 | 统一四分类、失效选中、冷进程恢复 |
| F03 | 双语标题、别名搜索、热门排序、内容去重 | A/S标题及投影UT较多；H有3条目录service用例；I有title/catalog集成与部分UI | 通过真实导入验证排名、同名异内容、空白名称，不只比较内存模型 |
| F04 | 收藏、最近与持久化 | A有收藏UI和目录UT；H无UI；I有导入收藏重启 | 三端补取消收藏、最近顺序和仅浏览不记最近 |
| F05 | 连续大目录、尾部浏览和响应 | A有2224条native构造集成；H无页面自动化；I有100条Files导入UI | 补三端真实导入、尾部启动与重复导航；区分数据构造与E2E |
| F06 | 目录/单文件导入及包识别 | S/Java parser/ZIP覆盖广；A有provider集成；H无picker旅程；I有3个Files UI | Android现有目录入口；H/I单文件入口单独测，不为统一测试新增产品入口 |
| F07 | 扫描、重扫、取消、进度和失败 | S有scan；H有native queue/executor；A/I各有集成 | 补UI触发→实际扫描→目录发布→重启/取消完整路径 |
| F08 | 来源移除、失效、重授权、目录损坏 | A持久授权/tombstone覆盖较多；I有重复来源清理；H缺I/O与UI | 重点保护原ROM和其他来源；失败不静默覆盖证据 |
| F09 | 精确ROM/ZIP变体启动和可恢复失败 | A strict loader和crash locator回归；I有package测试但部分未接入；H只有契约形态 | 实际游戏必须匹配所选内容；不能退到From Below冒充成功 |
| F10 | 自动封面、质量选择与缓存 | A有capture/repository集成；I有11条title/cover集成及UI；H无封面E2E | 补冷重启缓存、黑白坏帧拒绝、真实画面无overlay |
| F11 | 持续模拟、帧/PCM输出、时钟 | S runtime/PCM测试；A presenter较多；I runtime/soak测试；H host runtime | native loop不代替按实际时钟播放的UI/真机测试 |
| F12 | 方向模式、多指、短按、取消 | A UT和View/root dispatch广；I有13条overlay集成；H只有共享命中/布局逻辑 | 三端补系统输入到诊断画面的端到端；多指驱动缺失不能伪造 |
| F13 | START分离、暂停/继续/设置/退出 | S动作集合；A有START/PAUSE UI；I有主路径；H无UI | 暂停真实停步停音，返回设置不自动继续，退出资源清理 |
| F14 | 自动存档、两游戏隔离、损坏与写失败 | S checkpoint；A SaveRepository与迁移集成；I runtime恢复但UI不足；H只有key UT | 最高风险缺口：实际写盘→进程死亡→用户继续→正确进度 |
| F15 | 设置五分区、原子保存、重置 | A有mapper/store/section及UI；H有helper；I有偏好持久UI | 补真实效果、写失败提示、所有控制字段整体重置 |
| F16 | 预设、自定义四轴、比例、滤镜、CRT、资格回退 | A resolver/GL/golden/认证入口广；H host策略；I Metal金标和锁定UI | iOS自定义刷新/时间当前60/native只读；未认证Extreme/高刷保持锁定 |
| F17 | 声音开关、焦点、队列、音频路由与A/V | A多项queue/clock UT；H native队列；I真实PCM输出及未接入focus/queue程序 | 补策略到真实sink效果；路由/来电/A-V由真机抽测 |
| F18 | 触感等级、A/B区分、预览 | A pattern/持久化测试；H helper预览；I控制重置/overlay部分 | 模拟器测真实请求路径；马达效果必须真机 |
| F19 | 布局拖动/缩放/透明度/撤销/推荐/取消/试用/告警 | A有编辑器组件；H warning/undo逻辑；I草稿程序未接入 | 补保存后实际游戏新坐标命中，取消不落盘及告警确认 |
| F20 | 应用语言、跟随系统、运行标题 | A locale/title instrumentation；H title helper；I语言UI与独立main | 补所有常用页面/弹窗和运行中切换 |
| F21 | 横屏、安全区、大字体、无障碍、系统手势 | A有touch target/contrast及两项UiAutomation历史失败；H无页面；I部分几何 | 每个平台小屏/大字体可操作；语义和物理触控分开验 |
| F22 | 关于、许可证正文与失败重试 | A有licenses UI；H LicenseModel；I有页面但无完整UI用例 | 逐登记资源阅读，缺资源可重试，返回导航正确 |
| F23 | 后台/前台、锁屏、中断、surface、进程死亡 | A presenter/input取消较多；H render mailbox；I lifecycle源码与集成 | 补用户旅程和真机真实中断，输入归零与旧owner撤销 |
| F24 | 覆盖升级、历史配置/目录/存档迁移 | A已有若干迁移测试；H/I缺完整升级旅程 | 上一兼容包→本候选包，保留真实UI产生的数据 |
| F25 | 附近页、配对、好友管理与单机阻塞边界 | A19项nearby instrumentation；H10条纯决策；I5个nearby UI | 当前只验诚实阻塞态；大厅无真实入局路径，不伪造E2E；未来双机另有上线门禁 |
| F26 | 生成物、版本、ABI、测试设施、发布身份 | 有schema/golden/versioning/Pester与分散脚本；无统一coverage门禁 | 新增清单对账、零测试失败、报告同SHA和设备类型检查 |
| F28 | core公开C ABI、ROM生命周期、视频/音频格式、输入、回调、cheat与未实现接口 | core主要为一个较大的smoke程序；本轮发现独立battery仓库API但未找到产品调用 | 新增U073–U084/C021，按API族注册独立回归；FDS/patch/battery_flush/mem接口当前NOT_IMPLEMENTED，不新增产品入口 |
| F27 | 性能、资源、热、功耗和稳定性 | A大目录与显示认证；I加速36000帧soak；H host调度队列 | 非插桩包独立30分钟真实时钟、响应/内存/音频采样 |

常用功能范围明确为 **F01–F23及F25，共24个功能族，三端72个功能-平台项**。F24升级、F26发布/测试设施、F27性能、F28底层公开API是额外必需质量门禁。E014的单文件入口不适用Android，但F06在Android仍必须由真实目录导入E013达标，不能移出功能分母。

范围排除仅针对当前没有的产品能力：不新增游戏内手动Save/Load、联网ROM下载、外设手柄支持或新的高刷承诺来满足本计划。附近联机目前测真实入口/阻塞态；未来开放双机玩法之前强制接入原联机实机方案全部门禁。

## 4. 三道独立质量门槛

### 4.1 UT覆盖率目标与口径

以下是**本计划拟建立的验收门槛**，不是声称仓库已达到：

| 对象 | UT行覆盖率 | UT分支覆盖率 | 补充要求 |
|---|---:|---:|---|
| 每个第一方可单测业务模块 | ≥85% | ≥80% | 分模块检查，不以高覆盖模块拉高平均掩盖低覆盖模块 |
| 关键模块：内容/ZIP校验、身份、存档/持久化决策、输入状态、资格与session安全状态机 | ≥95% | ≥90% | 拒绝路径、边界、取消、旧generation和事务失败必须有具名用例 |
| 本次变更新增/修改可执行行 | ≥90% | ≥85% | 按base/head差异与UT-only数据计算；无可执行差异记N/A |
| 生产胶水/UI生命周期等不能合理纯单测的代码 | 独立报告 | 独立报告 | 纳入组件+E2E联合覆盖，第一方生产代码联合行覆盖目标≥80%；不混入UT专用门槛 |
| Swift/ArkTS暂时没有可靠分支数据的范围 | 同上行门槛 | NOT_MEASURED | 具名决策表分支场景100%作为过渡要求，工具缺口继续登记，绝不称统一分支率已达标 |

业务模块至少按catalog/ROM、settings/layout、input、runtime/media、display资格、session、各平台source/save bridge决策、构建/质量工具拆分。位于Activity/View/ArkUI组件里的业务分支仍属于业务分母，需要可测试接缝；不能把整目录标为UI后全部排除。

分母规则：

- 纳入所有当前第一方可执行生产源文件，包括未被测试加载/链接的文件；未触达文件计0，不能悄悄消失。
- 第三方NestopiaUE vendor、生成表、纯资源、纯ABI声明、测试本身单列；自动生成代码以生成器测试和golden验证替代行百分比。排除清单逐路径登记理由，禁止排除整个native/bridge/Activity目录。
- UT、组件、UI三份覆盖报告分开。可额外展示同SHA同构建族联合报告，但不能用一次UI遍历补齐UT缺口。
- 跨编译器/平台/语言报告各自计量；共享源码canonical path去重。不要平均Java与C++百分比，也不要把同一C++源在多个二进制中重复计分。
- coverage缺失、0个测试、错SHA、解析失败、分母异常下降都令门禁失败；skipped和blocked不贡献覆盖。

实施节奏：B1先得到完整真实基线，再按本计划补测；迁移期禁止UT覆盖倒退、对新增代码立即执行diff门槛。**阶段性“不倒退”不等于最终85/80达标**；B7必须达到目标或明确报告尚未完成。不得将阈值改低作为修复手段。

### 4.2 模拟器常用功能E2E门槛

两张表同时达标：

1. **功能覆盖**＝有完整成功E2E的常用功能-平台项 / 72，目标100%。只打开页面、源码grep、假repository或直接注入runtime结果均不计入。
2. **计划执行覆盖**＝成功的required平台用例实例 / 288，目标100%。测试用例存在但未接入、未执行、skip、环境/工具阻塞均保留在分母，不能宣称计划完成。

每个常用功能至少有“从正式入口操作→真实服务/native/storage→可见结果”的正向流程；有持久化的再含冷重启；有关键错误的含拒绝/失败后继续。所有常用功能P1同样必须实施，P0只是先后次序。

模拟器不能认证马达力度、真实刷新率、物理触控延迟、温控或功耗；它应验证逻辑路径和正确的能力回退。对已宣称支持的GPU路径，至少在一个能力满足的模拟器环境跑真实路径；若所选平台模拟器确实无法提供，标BLOCKED_ENV，使用真机增加证据但不将模拟器覆盖表冒记PASS。

### 4.3 真机覆盖门槛

每个候选版本，Android/Harmony/iPhone各至少一台物理设备运行以下展开ID：

`D001 D002 D003 D004 D005 D008 D010 D011 D012 D019 D020 D022`。

共12条/平台、36个平台必测实例：安装启动、真实文件权限、升级保数据、30分钟实际播放、多指、焦点、存档、中断、GPU、写失败、联机阻塞边界、发布测试钩子隔离。D022的release包不得带IOFAULT等测试入口；D019用同SHA的受控instrumented debug包，分别记录variant/hash，不可合并为一个发布包结果。

其余D006/D007/D009/D014/D015/D016/D017/D018/D021按周轮换；输入/音频/存储/显示修改触发相关项立即执行。D013只在Android/Harmony宣称高刷或资格逻辑变化时做，缺匹配资格证据保持锁定，不能为跑测试授权Extreme。

正式发布前完成本计划所有适用真机用例。D014没有自然达到严重热状态时记录“该条件未覆盖”，用UT/注入验证策略另列，不能加热设备或伪造物理触发证据。没有设备/签名就报告缺口，不能用APK/HAP/IPA构建替代。

## 5. 测试环境与夹具策略

| 环境 | 主要任务 | 配置要求 |
|---|---|---|
| Windows host | MSVC回归、Git CRLF/路径/reparse、Pester、Android JVM | 独立ignored build；Debug确保assert有效，Release验证优化/编译问题；固定zlib |
| Linux host | Clang coverage、ASan/UBSan、symlink/hardlink、共享C ABI及golden | 编译全部第一方目标；asan与coverage/性能分开 |
| Android主模拟器 | 全部A-E实例 | API36、x86_64、固定屏幕/locale/动画与可用GPU；与项目compile/target36一致 |
| Android兼容模拟器 | 关键主链+存储/生命周期 | minSdk24；对API24不支持的系统能力测正确回退；API35可保留为历史失败复现环境 |
| Harmony官方模拟器 | 全部H-E实例与Hypium/C组件 | 以当前DevEco API20工程可安装镜像为主；记录实际镜像版本/ABI；最低兼容API12缺镜像则真机补充并保留差异 |
| iOS Simulator | 全部I-E实例、Runtime XCTest、Files真实导入 | deployment target当前16.4；至少16.4兼容环境及所锁定现代SDK环境；arm64/x86_64按实际宿主分离 |
| 物理设备池 | D系列 | 每平台至少1台；Android增加不同GPU/性能第二台作周轮换；iPhone普通刷新/高刷新机型分别记录；Harmony按实际支持名单抽样 |

所有数据正向准备通过正式入口；大库也用真实导入，不在E2E中直接seed产品数据库。组件性能测试仍可构造内存/native目录，但分层标识。完整夹具定义见用例文档§2，不留“找几个ROM”的开放步骤。

生命周期、保存/损坏、磁盘/资源错误需要测试接缝时，只在实际I/O边界注入单次失败；发布构建不含可触发入口。测试仅操作run-id所属数据；失败保留证据，成功只清理本次创建的资源。设备用户已有内容不得通过clear/uninstall消除。

## 6. 测试设施的具体增强

### 6.1 用例注册与结果对账

新增 `tools/quality/test-matrix.json`，登记附件全部239条及平台实例。每条必须有：

`case_id, feature_id, layer, platforms, priority, fixture_id, source_files, test_file, framework_selector, required_environments, automation_kind, status, evidence`。

校验链：**源码声明 → runner discovery → 实际启动 → 实际结束/断言 → 同SHA证据**。必须报告未注册测试、零测试、选择器不存在、重复ID、missing/skipped/blocked、错误variant和设备类型；不能只匹配“Failure: 0, Error: 0”。

新增入口及回归测试在完整清单G001–G015、U068–U072中逐条定义：
`run_test_plan.py`、`check_test_matrix.py`、`check_test_results.py`、`check_coverage.py`、`check_device_target.py`。统一入口调用现有Gradle/CMake/Hvigor/Xcode命令，不隐藏原始退出码或替代现有平台工具。

### 6.2 UT覆盖率采集

- Android：当前AGP为8.13.2。使用该版本支持的coverage开关和JaCoCo报告任务；先通过`tasks --all`验证具体任务，再锁定CI selector。官方说明支持分别采集UT与instrumentation，新的聚合任务要求更高版本，**本计划不为了聚合报表升级到alpha AGP**。[Android官方覆盖率说明](https://developer.android.com/studio/test/coverage-report)
- C/C++：Linux Clang专用Debug coverage构建，编译/链接加`-fprofile-instr-generate -fcoverage-mapping`，使用包含进程/二进制区分的`LLVM_PROFILE_FILE`，运行后由`llvm-profdata`与`llvm-cov`导出line/branch；所有测试二进制覆盖的源码去重并补未触达文件。MSVC构建继续作为兼容门禁。[Clang官方说明](https://clang.llvm.org/docs/SourceBasedCodeCoverage.html)
- iOS：在所选测试配置开启Xcode coverage，UT/Runtime/UI test plan分组；从xcresult提取源文件行覆盖。Swift branch若导出不支持则标NOT_MEASURED，不能推算。[Apple覆盖率文档](https://developer.apple.com/documentation/xcode/determining-how-much-code-your-tests-cover)、[Test Plan文档](https://developer.apple.com/documentation/xcode/organizing-tests-to-improve-feedback)
- Harmony：复用DevEco/Hypium的单元测试和UI测试能力；B1在当前安装SDK先验证覆盖率导出格式及source map，未验证前不写“具备百分比门禁”。ArkTS纯逻辑coverage、native Clang host coverage分列。[Huawei测试服务说明](https://developer.huawei.com/consumer/cn/testing/get-started/)

数值门槛由项目在本计划提出，以上官方资料仅支持工具能力，不是85/80等阈值的外部来源。

### 6.3 三端E2E实现规则

附件§6给出 **19个旅程组、57个精确新增文件路径**，覆盖288个平台用例实例。

- Android复用Espresso处理app UI，系统文件选择器/系统中断用系统UI驱动；新类与现有组件测试分包。改善UiAutomation生命周期，使用有条件等待与管道完成，不靠固定sleep或循环重跑。
- Harmony新增真正页面UI驱动与稳定`.id()`，用Hypium组织旅程；不要只向`List.test.ets`再加纯函数测试后宣称E2E完成。
- iOS新UI类接入CMake，给来源行/控件增加稳定accessibilityIdentifier；消除以英文label或firstMatch猜来源的定位。Files夹具preflight自动验证，不直接种app状态。
- 多指必须从系统输入层到游戏可见结果；现有直接View/UITouch方法仍是组件测试。B2先验证各模拟器驱动能否注入真实多指/短按，不能实现时保持对应ID阻塞，不降低验收定义。
- UI等待条件使用“scan idle且目录计数正确”“presentation已前进”“source操作完成”等真实条件和有界超时；超时捕获当前UI树/截图/日志。必要诊断为只读，不替代用户操作或改变产品判断。

### 6.4 独立性能作业

性能/功耗用无coverage、无sanitizer的同SHA构建；与覆盖率结果用variant区分。新增稳定机器基线后，建议把以下作为项目性能预算，首批如无法达到应登记产品问题，不能默默放宽：

| 项目 | 建议预算 / 判断方式 |
|---|---|
| 普通可见UI操作 | p95≤200ms出现可见反馈；不要求后台扫描在200ms内完成 |
| 100条库冷启动 | 固定参考模拟器/真机p95≤3s到可交互，10次完整冷启动；分别记录首次复制内置资源与后续冷启动 |
| 2224条库搜索/分类 | 固定环境p95≤500ms到稳定结果；30次查询/切类；不在CI繁忙机器跨环境比较 |
| 长扫描 | 进度持续更新且UI仍响应；取消2s内被确认，后续不发布假完整结果 |
| 内存/句柄 | 30分钟采样曲线无持续增长；重复开关50轮后不累积runtime/surface/音频owner/FD |
| 物理A/V | D021新增单机目标abs p95≤50ms，≥100次标记，标注测量不确定性；原联机数值门槛不变 |

任何预算变化需记录固定测试环境、实测分布和理由。模拟器结果不认证真实刷新、温度、耗电或触控延迟；iOS36000帧紧循环不能写成“30分钟真实播放”。

## 7. 实施批次与逐项退出条件

本节只排序，完整断言在239条用例中，不能以完成本节描述替代用例实现。每批都在授权执行后通过仓库版本分配工具建立隔离worktree；主版本维持1，保留本地用户状态。

| 批次 | 具体文件/工作 | 对应用例 | 退出条件 |
|---|---|---|---|
| B0 可信基线 | 精确LF属性及generator回归；Harmony契约第三参数；UiAutomation/Files前置修复；native先build再test | U065；原文F1–F5；G002/G003/G007/G008 | 同一SHA各平台声明/执行/skip清单完整；已知失败消除或有具体阻塞，未把skip当PASS |
| B1 注册与coverage | 新增test-matrix/coverage-policy/结果与coverage校验器及其tests；接入分层采集 | U068–U072、G001–G009/G012–G015；所有当前测试及U073–U084/C021 | 得到真实百分比及分母；零测试、错SHA、遗漏源文件能稳定触发RED；iOS14个独立程序有正式入口 |
| B2 夹具与驱动 | product fixture generator/stager、系统picker、稳定ID、条件等待、多指/短按可行性；三端runner注册 | G009–G011；先打通E001/E013/E030/E034 | 夹具可重建、有许可/哈希；三端各跑一个真正纵向流程；多指缺口明确解决或标阻塞 |
| B3 目录与存档 | Library/Source/Cover/Autosave/Upgrade五旅程组；补source/save真实I/O组件 | F01–F10/F14/F24相关U/C/E；D001–D003/D010/D017/D019 | 三端导入→正确启动→收藏/进度→冷重启→删除/重授权完整，错误不毁旧数据 |
| B4 输入与生命周期 | Playback/Input/Pause/Layout/Lifecycle旅程；native输入/时钟/取消回归 | F11–F13/F19/F23/F28相关U/C/E；D005/D006/D011/D018 | 系统输入到实际画面；暂停/取消/恢复/布局持久闭环；无旧owner/粘键 |
| B5 设置与显示音频 | Settings/Display/Audio/Haptics/Localization/Accessibility/About/Nearby/Offline其余旅程 | F15–F18/F20–F22/F25所有E；相关U/C/D | 全部288个E实例接入并成功，当前功能族72/72；受保护模式仍锁定且理由可见 |
| B6 真机与性能 | device manifest/runner、iOS物理测试签名目标、指标采集；稳定环境性能预算 | D001–D022、C020、G009/G012 | 完成每候选36项必测；其余65项总体适用集有证据或明确未完成；同SHA非插桩性能结果 |
| B7 持续门禁 | 现有workflow接入test plan，增加Android模拟器、Harmony官方runner、iOS产品XCTest任务；失败报告归档 | G001–G015及全清单 | UT门槛达标、E2E全覆盖、真机要求满足；新改动能触发正确作业，三轮顺序独立验收成功 |

具体native注册：修改`ios/tests/CMakeLists.txt`为跨平台可移植C++独立测试增加target；Apple Foundation/Metal/Swift程序放Apple host test目标，注册到`ios/app/CMakeLists.txt`或新增`ios/tests/apple/CMakeLists.txt`适合的配置。由各程序实际依赖决定target，不将全部文件混编。CMake必须保证Debug/Release断言生效；改坏一个断言的临时副本在两个配置都要失败。

现有Android `SingleDeviceCertificationRunner` 不改成自动授权。iOS新增Device bundle/runner时使用正确development签名配置，不复制simulator ad-hoc设置；物理设备测试不访问模拟器私有container枚举路径。所有源码/测试修复执行RED→最小修改→相关回归。

## 8. 执行频率与CI阻断规则

| 时机 | 必跑集合 | 阻断规则 |
|---|---|---|
| 开发者提交前 | 变更模块UT/C、generator/schema/版本检查、受影响E2E | 失败不得称本改动验证完成 |
| PR | 全UT/静态契约/coverage；三端核心模拟器E2E；受影响功能全部E2E | shared/设置schema/fixture改动触发三端；平台UI改动触发对应全部相关旅程 |
| 合入main后 | 三端全部288个E实例及当前注册组件套件 | 未完成全套之前，该SHA不能成为可发布基线 |
| 每夜 | 全套+兼容系统/大库/随机顺序/长时模拟器、ASan/UBSan；文件系统skip补跑 | P0 flaky不静默隔离；缺设备/SDK标阻塞 |
| 每个候选包 | 真机每平台12条必测展开36项+对应修改触发项 | 缺任一平台真机结果不得称三端候选已验证 |
| 每周/发布前 | 全部适用D实例、第二机型/GPU、音频route/无障碍/权限边界；未来联机另完整矩阵 | 支持名单按实际设备证据，不外推 |

PR核心E2E明确为：E001/E002/E003/E005/E009/E013/E015/E025/E030/E036–E043/E046–E050/E056/E058/E063/E064/E069/E073/E080/E085/E088/E091/E094，均在适用三端运行；E014另在H/I有相关改动时加入。它是快速发现回归的集合，不能替代main全288项。

新建的主链E2E入门禁前在干净专用环境连跑3轮；nightly做10轮顺序变化以查状态依赖。失败后最多一次有记录的诊断重跑，首次失败仍保留并影响门禁；不允许“重跑直到绿”。P1隔离需有issue、责任人、恢复期限且不从功能覆盖分母消失；不能靠隔离把100%覆盖显示出来。

## 9. 执行入口与报告规范

现有可执行入口保留：

- Android：`gradlew.bat :app:testDebugUnitTest`、明确设备后的`:app:connectedDebugAndroidTest`。
- native：先configure/build，再`ctest --test-dir <本次目录> -C Debug/Release --output-on-failure`；目录参数由实际run-id创建，不能指向另一个worktree缓存。
- Harmony：`tools/quality/run_harmony_completion_gate.ps1`，扩展其计数/目标/结果校验并纳入真正UI用例。
- iOS：`ios/scripts/build_simulator.sh`后开启`FLYNES_IOS_BUILD_XCTESTS=ON`构建Runtime/UI目标，`run_simulator_tests.py`运行；Files先stage/preflight。物理设备用新增device入口。

新增统一入口的约定命令（**计划新增，目前不存在**）：

```powershell
python tools/quality/run_test_plan.py --plan tools/quality/test-matrix.json --stage unit --evidence-root out/evidence/test-plan
python tools/quality/run_test_plan.py --plan tools/quality/test-matrix.json --stage emulator --platform android --case A-E041 --target emulator-5554 --evidence-root out/evidence/test-plan
python tools/quality/check_test_results.py --run-dir out/evidence/test-plan/current-run
python tools/quality/check_coverage.py --policy tools/quality/coverage-policy.json --run-dir out/evidence/test-plan/current-run
```

这里`current-run`是实施runner为当前执行创建的目录别名，必须解析到manifest记录的实际run-id，不能读取历史最后一次PASS。targets必须由preflight枚举并明确选取；示例emulator-5554不是本轮已连接设备的声明。

每条结果包括：case/feature/platform/layer、实际framework selector、源码SHA/dirty、VERSION、工具链/镜像/设备、包/fixture哈希、操作起止、PASS/FAIL/ERROR/NOT_RUN/BLOCKED/SKIP、首次失败断言、截图/UI树/日志、coverage或物理指标。原始文件写ignored `out/evidence/`；脱敏摘要进入文档。证据最少保留到相应候选版本验证结束；失败证据不能被成功重跑覆盖。

## 10. 可验收的完成定义

- [ ] 239条用例定义全部进入机器可读清单，平台展开ID与runner discovery一一对应，当前未接入资产得到处理。
- [ ] UT真实分母和line/branch报告可重复生成；最终85/80、关键95/90、diff90/85门槛达标；未测分支能力明确，不伪造数字。
- [ ] 三端常用功能72/72有真实模拟器E2E；288个required平台实例全部成功，或明确报告计划尚未完成，不删除阻塞项。
- [ ] 每平台候选包12条真机必测成功；所有适用真机扩展项按发布周期有可追溯证据。
- [ ] 保存、授权、取消、坏输入、surface/进程死亡、系统音频等关键负测保留真实断言；没有删测试/扩大skip来消除红灯。
- [ ] 新检出、全新build和固定工具链可复现；没有把源码检查、unsigned包、模拟器或加速loop当真机证据。
- [ ] 测试钩子/夹具/签名与用户数据隔离；遵循main干净树、版本匹配tag和包hash的既有发布规则。

本轮完成的是**功能审计、测试增强计划与完整用例清单**。所有新测试实现、coverage采集、平台构建和真机操作均留待后续执行，没有通过文档任务隐式执行。

