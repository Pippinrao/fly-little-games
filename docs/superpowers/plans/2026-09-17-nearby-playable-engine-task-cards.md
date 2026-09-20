# Nearby 首局可玩链路：细分执行卡

> For agentic workers: 使用 executing-plans；用户自行启动并行。一次认领一张卡，按依赖实施；产品行为必须先有失败测试，不用扩大 scope 代替完成当前卡。

**Goal:** 先打通真 engine + 真 QUIC + 真 NES core + 原生 UX 的一局 DUAL，再完成三端和原稿全部基本交互。

**Architecture:** 复用已有 V2 组件，集成人独占 ABI/engine/schema，平台 owner 接生产 provider；所有玩法授权留 shared。

**Tech Stack:** C++17、NestopiaUE、Rust Quinn/rustls、三端原生 bridge。

日期：2026-09-17。本文件是[总计划](2026-09-17-nearby-playable-mvp-correction-plan.md)的后端细分；布局按[逐页 UX 卡](2026-09-17-nearby-ux-restoration-task-cards.md)，不能反过来按后端当前方便程度改 UX。

## 1. 里程碑，不混淆完成口径

| 里程碑 | 最小范围 | 不能宣称 |
|---|---|---|
| M0 首局技术可玩 | 首平台两 app、输入码、同 ROM、默认角色、真实核心/网络、双方确认、暂停继续/结束 | 原稿全功能完成、三端可玩、真机通过 |
| M1 首平台原稿基本版 | M0 + 原稿逐页结构、开局前主机/座位、完整错误与返回、好友基本保存；QR 能力状态单列 | 未做的 QR/高级好友/文件传输通过 |
| M2 三端非真机候选 | 三端分别 M1 + 原生一致性 + 可执行的非真机跨端组合 | 物理无线/安全存储/性能认证 |
| 后续 | QR 全链、高级好友、文件传输、故障恢复/迁移/耐久存档；STREAM 另排 | 将入口占位当后续功能完成 |

固定主机/seat 只可作为 M0 测试样本，不是删掉原稿控件的授权。首个 M0 出来即给可运行结果与限制，不再等待复杂恢复完成；未交付原稿条目继续列任务。

## 2. 责任和开工快照

INTEGRATOR 唯一写 `shared/include/flynes/flynes_session.h`、`shared/src/session/engine/*`、`shared/src/session/view/*`、`shared/schema/*`、`shared/CMakeLists.txt`。RUNTIME/TRANSPORT 只写各自组件；平台各自写本平台。新增文件路径是拟建，若开工已存在同职责实现应复用，不另写一套。

| 卡 | 当前已知资产/缺口 | 接手结论 |
|---|---|---|
| ENG-01 | W0 dirty，W3 部分未合入，旧验收冲突 | 先固定清单，不 reset/stash |
| ENG-03/04 | 三端 bridge 有空 V2 装配检查，旧邀请 API | 不能记生产接入完成 |
| ENG-05/06 | dirty controller 能 SELECT/START；双确认缺口 | 加门禁，不推倒 codec/scheduler |
| ENG-08/09 | runtime adapter、Quinn crate 存在 | 缺二者与 engine 同时使用的证据 |
| UX 卡 | N00 正有修复，PAIR/CONFIG 仍偏离 | 按新源码补齐，不照旧审计重复改 |

每张卡统一执行：先写所列负例→确认失败原因→最小实现→正/负例回归→交付文件/命令/结果。不能只交代码无证据，不能将后置恢复卡作为本卡依赖。

## ENG-01：形成可复现的共同基线

Owner INTEGRATOR；依赖无。文件：W0/W3 源树清单、W0验收文档、拟建 `docs/acceptance/2026-09-17-nearby-playable-progress.md`。

- [ ] 记录 HEAD、dirty/untracked 摘要和来源；现有未提交代码逐项标“首版保留/后置隔离/待审”，不删除。
- [ ] 审阅 W3 tip 与 W0 的差异，接入缺失的篡改负例及修复；不要复制过时 fixture。
- [ ] 更正 PASS/NOT_RUN 冲突，把假 runtime 的 600 帧记 L1；保存历史而非篡改成新日期通过。
- [ ] 新目录 configure/build/test，记录已有非本任务失败；确保必要源码均纳入可复现提交。

验收：共同 SHA 在新分支可构建；无未跟踪必需源码；每个门禁绑定正确源树。完成后用户可用 repo worktree 脚本创建独立任务分支。

## ENG-02：冻结最小接口缺口，禁止平台自造状态

Owner INTEGRATOR；依赖 ENG-01、UX-00.S。文件：公开头、engine/view/schema；新增 `docs/superpowers/specs/2026-09-17-nearby-playable-seams.md`。

- [ ] 列出现有已定义 action/snapshot/provider 与下列 UX 语义的准确映射：待房主接受、SAS、已连接好友、选择内容、pending config、双确认、暂停/结束。
- [ ] 对已有实现复用原枚举；确有缺口再尾追加 ABI。记录 struct_size/prefix 兼容和 buffer/token 所有权，平台不重传内部 hash 当授权。
- [ ] 确认 config/ready/pause/end 的既有 wire 是否覆盖；缺失部分逐字段冻结长度、字节序、domain、重放/generation 校验及未知值策略，然后才写 serializer。
- [ ] 新增独立 golden 和 C ABI 测试；测试未通过时不发布给平台 worker。

验收：平台能按一个契约读必要字段和发合法 action；无“待实现字段”伪常量；不为了首版增加 STREAM/media 或完整恢复协议。精确 wire 不在本计划凭空编造，由此卡先定稿再供后续实现。

## ENG-03.A/H/I：创建真实 session owner

Owner 各平台；依赖 ENG-02。A 新建 `app/src/main/cpp/nearby/session_owner.{hpp,cpp}`、`app/src/main/java/com/flynes/emu/NearbySessionOwner.java`，修改 `flynes_app_jni.cpp`；H 新建 `harmony/entry/src/main/cpp/nearby_session_owner.{hpp,cpp}`，修改 `napi_init.cpp`/`service/NearbyService.ets`；I 新建 `ios/app/bridge/NearbySessionOwner.{h,mm}`，修改 `FlyNesAppBridge.mm`。构建注册各平台 owner 负责。

- [ ] 创建/持有/销毁真实 V2 engine，clock/executor/platform state 使用生产实现，缺能力明确 UNAVAILABLE。
- [ ] snapshot 和 notices 安全转到 UI 线程；页面来回导航复用 owner，不从本地 bool 恢复已连接。
- [ ] 旧邀请码桥委托该 owner，避免旧流程和 V2 同时拥有邀请。
- [ ] 测旋转/返回/进后台/销毁/迟到回调：一个 owner、无重复事件、无释放后访问。

验收：生产调用真实 V2 API 的计数和生命周期可观测；只有 `verify_nearby_v2_composition_contract()` 不通过；没有测试 provider 注入开关进入 release。

## ENG-04.A/H/I：码加入、身份与连接的生产端口

Owner 各平台；依赖 ENG-03、既有共享握手。文件：各 `session_owner`、现有平台 nearby 目录、共享 provider 接口只由集成人改。

- [ ] 将 discovery/随机与密钥操作/secure store/ObjectStore/bearer/QUIC 绑定生产 API，复用既有实现；每种端口可用性有独立 reason。
- [ ] 创建邀请码与 generation/60秒生命周期绑定真实发现；房主显式接受后走 SAS，再走加密通道和 link ready。
- [ ] 拒绝、过期、重生成、取消及迟到事件实测；未认证不发布好友身份或受保护信息。
- [ ] 已验证且持久化成功才保存好友并报告 saved；保存失败如实显示，不造昵称；两端仍可显示验证完成的连接事实。

验收：真实入口→N04/N05→双端 SAS→N07→N08；单边 SAS/READY 不通过；相机拒绝不影响可用码路径；若模拟器无 BLE 实现则无线部分 BLOCKED，测试 adapter 标 L1/受限场景，不以它认证生产发现。

ENG-04 不作为一次大任务委派，按下面六个子卡交付；各平台在 ENG-03 的 owner 文件接端口，复用既有 nearby 实现。同一平台 owner 文件串行修改，不让六个 worker 同时改它。

| 子卡 | 只做的事情 | 先写的反例/验收 |
|---|---|---|
| ENG-04.1.A/H/I | 真实 discovery/邀请码查找、权限和取消 | 无权限不扫描；取消后停止；同一 code/generation 对应同一临时邀请；7位输入不请求 |
| ENG-04.2.A/H/I | 系统随机、签名/验签、AEAD 和 secure key 引用适配 | 篡改拒绝；跨 generation 不复用；私钥不能以 UI 字符串/日志暴露；不得手写密码算法 |
| ENG-04.3.A/H/I | ObjectStore 持久对象与读取结果、取消/所有权 | hash/尺寸错误拒绝；持久化未完成不能越过门禁；资源重复回调不重复释放 |
| ENG-04.4.A/H/I | bearer/Wi-Fi 网络申请、结果和失败原因 | 同 session 系统确认一次；用户拒绝不循环；取消后网络资源释放；不悄悄换未授权路径 |
| ENG-04.5.A/H/I | Quinn native 包装接 owner + 整条 code/SAS/link 路径 | 单边 SAS/READY 不连；pin/exporter不匹配拒绝；N08必须来自真实快照 |
| ENG-04.6.A/H/I | 已认证好友基本保存和初始列表读取 | 持久化成功才显示已保存；未认证候选不写；重启读回；存储失败不可伪成功 |

依赖：.1/.2/.3 可先按端口测试独立完成；.4 后接 .5（.5 同时依赖 .1–.3 和 ENG-09 provider 适配）；.6 在已验证身份后执行。M0 可以在 .6 未完时标记“连接成功、好友未保存”，M1 不能省略 .6。硬件密钥性质仍需真机后续认证，模拟器结果如实标环境。

## ENG-05：从真实 catalog 形成双方同一 pending config

Owner INTEGRATOR；依赖 ENG-02。修改 `dual_session_controller.*`、engine/view；新测试 `shared/tests/nearby/integration/test_game_config_consent.cpp`。

- [ ] 先测双方不同 ROM/核心配置/未验证 profile 不能启动。
- [ ] typed choice ref 从本机真实 catalog 解析；对端验证内容身份、core/profile/options，不信任 UI 给 hash。
- [ ] 形成可交换且被同意的 pending config/revision/fingerprint；UI 短码仅其投影，不承担完整比较。
- [ ] 配置变更撤销旧 token 与双方确认；缺文件只阻塞开局，不断朋友连接、不自动下载。

验收：同一配置双端同 fingerprint/short code；任一字段变化拒绝旧确认；SELECT 不写 `dual_seats_confirmed=true`；无硬编码游戏 ID。

## ENG-06：双方确认与 runtime ready 开局屏障

Owner INTEGRATOR；依赖 ENG-05；文件与测试同 ENG-05，本卡单独提交/验收。

- [ ] 删除“有 selection 即用户可直接 START_DUAL”的授权路径，将实际启动置于确认屏障之后。
- [ ] 分别记录 A/B 对同一 fingerprint 的确认；A 未确认/B 未确认/同意已过期任何一种都不步进。
- [ ] 两端 runtime 装载和资源就绪、协商同一起始边界后才发布 GAME_RUNNING；任一装载失败两端可见原因，不单边运行。
- [ ] 测重复确认、过期 token、配置替换期间对方确认、关闭后回调不重复 load/step。

验收：按 UX-12 真实点击两次独立确认才启动；测试不得在通用 pump 隐藏自动批准；按到达顺序遍历 ready/confirm，结果相同。

## ENG-07：开局前主机与座位变更

Owner INTEGRATOR；依赖 ENG-05/06；同 controller/config tests。M0 默认组合可先过，M1 本卡必做。

- [ ] 从已验证能力发布主机候选与禁用原因；邀请者仅默认值，不等同 network owner/listener/authority 永久绑定。
- [ ] 开局前允许提出 P1/P2 对换和可用主机变更；两端一致新配置，确认全部失效。
- [ ] 按协商配置给 scheduler 分配 owner/seat，不再 `initiator ? 0 : 1` 硬编码为最终规则。

验收：相同两设备测试两种座位、两种合法主机组合；网络提供设备不变时也能切可用主机；旧 seat 输入拒绝；运行中迁移不在本卡，不实现自动接管。

## ENG-08：真实 NES runtime adapter

Owner RUNTIME；依赖 ENG-02。新建 `shared/src/session/dual/nes_dual_runtime.{hpp,cpp}`、`shared/tests/nearby/integration/test_dual_real_runtime.cpp`；复用 `shared/include/flynes/flynes_runtime.h`、`shared/src/runtime/flynes_runtime.cpp`、`dual_runtime_contract.hpp`。

- [ ] 以真实核心和合法双人样本写 load/step/save/load-state/digest 测试，不复用 fixture 假状态作为被测 runtime。
- [ ] adapter 实现既有端口；复用核心视频/PCM，固定确定性设置；所有容量/加载错误回传明确 result。
- [ ] 每端唯一步进拥有者为 scheduler，禁止单人线程重复推进；逐 seat 输入真实作用到 core。
- [ ] 600帧烟测、状态恢复回放、至少10分钟持续测试；按相同仿真帧比 state/frame/PCM，而非播放墙钟。

验收：日志证明真实 NestopiaUE/core版本和样本身份、P1/P2 独立效果；一端改输入能引出差异；禁止用不同标题/文件名推断内容一致。

## ENG-09：真实 Quinn 的两个 engine 测试

Owner TRANSPORT；依赖 ENG-02，接真游戏依赖 ENG-06/08。新建 `shared/tests/nearby/integration/test_two_engine_real_quic.cpp`；复用 `shared/nearby-quic-provider/`，勿改写 TLS。

- [ ] 新 CMake 目录强制 provider ON/cargo存在，构建后断言测试目标存在；注册由集成人接收。
- [ ] 两 engine 的 listen/connect/pin/exporter/bind/control/input 实际调用产品 provider；应用字节确实走 socket，不能由 fixture LoopbackTransport 转送。
- [ ] 不同分片/读取 credit/断流和关闭顺序下验证资源释放与失败，不虚构 verified event。
- [ ] 接 ENG-08 真 core，再跑同一游戏两路输入；Quinn crate 单测仍单列。

验收：provider=Quinn、runtime=NES 同一测试同时成立；无人工粘贴对端 digest/evidence；网络负例导致冻结/拒绝而非继续；发现若替身清楚标出。

## ENG-10：健康暂停/继续及结束换局

Owner INTEGRATOR；依赖 ENG-06/09；新增 `shared/tests/nearby/integration/test_game_lifecycle.cpp`。

- [ ] 暂停协商同一提交帧边界；UI 按暂停不能只是本端停止显示、另一端继续模拟。
- [ ] 健康连接继续使用共同合法状态，不调用未实现的故障恢复/ActivationFence 假完成；旧按键释放策略有测试。
- [ ] 正常结束关闭旧局输入；保留连接，下一局独立 epoch/config/confirm；旧局 packet 不能影响新局。
- [ ] 断线冻结可以明确结束/断开；不支持自动恢复，不显示可执行恢复动作。

验收：暂停期间两端帧号不增，继续两端同帧推进；连续两局通过；旧包/重复 END 不重复提交；大厅偏好保留。若协议缺口回 ENG-02 最小补充，禁止以此展开所有 durable recovery。

## ENG-11.A/H/I：原生游戏运行链整合

Owner 各平台；依赖 ENG-03/04/06/08/09/10、相应 UX 页；文件平台 owner + PLAY 组 + 已有 renderer/audio/input bridge。

- [ ] 原界面动作驱动共享 engine；双确认之后进入既有游戏视图，不用测试界面代替产品。
- [ ] 本地 P1/P2 输入经 session 到双方 core；两端真实帧/音频给既有播放器，移除同 runtime 的第二个 step 时钟。
- [ ] 验证暂停/继续/静音/结束/返回/再选游戏；后台不可继续假运行，有真实 session 状态。
- [ ] 当前平台两 app 至少10分钟可交互；提交全过程录像或逐状态截图、日志、输入轨迹与网络来源说明。

验收：L3 平台真实流程通过才完成；有画面但输入只本机有效/另一端只是播视频不通过；没有真机试验只能称非真机候选。

## ENG-12：首局与原稿基本版分别验收

Owner 验收者；依赖 M0/M1 对应任务。新建 `docs/acceptance/2026-09-17-nearby-playable-progress.md` 的运行记录，不给旧 600帧 PASS 换标题。

| Case | 操作 | 必须结果 |
|---|---|---|
| PLAY-01 | 无选中游戏创建→码加入→接受→双 SAS→大厅 | 先连接后选游戏；匿名到 verified 的边界正确 |
| PLAY-02 | A确认、B未确认 | 无任何核心步进；A等待，B可操作 |
| PLAY-03 | 相同ROM/配置双确认→两路输入10分钟 | 两端真运行、输入各有效、同帧状态一致 |
| PLAY-04 | 运行→暂停→继续→结束→第二局 | 健康继续成功、连接保留、第二局重确认 |
| PLAY-05 | 不同ROM/profile不匹配/缺资源 | 明确不可用，不开局不传文件 |
| PLAY-06 | 断流/非法包/迟到旧局输入 | 冻结或拒绝，无静默单人/STREAM |
| PLAY-07 | 开局前换主机/seat再确认 | 配置同步、旧确认无效；这是 M1 要求 |
| PLAY-08 | 原生全部 UX 卡 + 原单人回归 | 不因联机毁掉大厅/导入/播放/存档 |

M0 = 首平台 PLAY-01–06 + 原单人关键回归；M1 还须 PLAY-07–08 和好友基本保存证据；M2 三端分别 M1 + UX-18 一致性。二维码/完整好友/文件等后置项逐项列 DEFERRED，物理项 NOT_RUN。

## 3. 可复制的任务委派提示

```text
任务：仅执行 ENG-08（或 UX-12.A 等一个准确 ID）。
基线：使用 ENG-01 实际交付的共同 SHA；先核对 dirty 状态，保留他人变更。
先读：获批 UX、09-17 总计划和该任务卡。禁止更改原稿或放宽验收。
只写任务卡列出的文件；共享 ABI/engine/schema/CMake 交由集成人。
按失败测试→最小实现→验收执行；报告真实 provider/runtime 与测试范围。
不得自动启动其他代理或展开后置复杂功能。
套餐核心余量低于25%立即停止当前及本线程队列，留下准确断点。
交付：提交/dirty指纹、文件清单、命令/退出码/用例数、截图/日志、剩余缺口。
```

这是任务提示格式，实际委派时填写一个准确任务 ID 和 ENG-01 的 SHA；不要将示例文字当作已存在的基线或已通过证据。

## 4. 并行批次及交付约束

- 批次 A：ENG-01/02 集成人；三端 UX-01–07 可做布局，真实接线不抢跑。
- 批次 B：ENG-05/06 集成人；ENG-08 runtime；ENG-09 transport；首平台 ENG-03/04。四方文件互不覆盖。
- 批次 C：集成人 ENG-07/10；首平台 ENG-11；其他平台做同一 frozen seam 的 owner/页面，不自行扩展 ABI。
- 批次 D：ENG-12 M0/M1 先交付；再其他平台 ENG-11 与 UX-18 收口 M2。

任务遇到上游缺口输出“缺的输入/契约/可复现测试”，回交相应 owner；不要偷偷补第二套实现。一个任务编译或测试失败时不得靠关 provider、跳过用例或删除旧测试声明完成。构建/平台命令沿用总计划和 DEVELOPMENT，native parity 的正确调用见 UX 执行卡。
