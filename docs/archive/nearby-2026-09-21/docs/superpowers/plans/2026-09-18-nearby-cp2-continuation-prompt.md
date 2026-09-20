> 历史归档（2026-09-21）：本文的执行顺序、首版范围和进度结论已被[当前最小可玩方案](../../../../../nearby/README.md)取代。保留供追溯，不作为新增任务或发布承诺。

# Nearby CP2 继续推进交接

> **2026-09-19更新：** CP2主流程及双SessionEngine＋真NES（Loopback传输）已通过定向回归。继续执行请转到[真Quinn联合验收交接](2026-09-19-nearby-real-quinn-continuation-prompt.md)，不要再从本文历史两条失败断言开始。

> **For agentic workers:** 使用 executing-plans 按下列检查点执行；用户自行启动并行会话，本文件不授权自动派发子代理。本文更新上一份交接的起点，不改原始 UX 或降低 ENG/UX 验收要求。

**Goal:** 从当前失败用例出发打通真实对端配置确认与双方就绪屏障，恢复逻辑层运行验收，再继续真核心、真网络及首平台可玩闭环。

**Architecture:** 复用 V2 engine、既有 Control 帧和握手校验、dual controller/scheduler、NestopiaUE adapter 与 Quinn。一个 Control 读流拥有者负责握手及握手后的路由；不另建可绕过认证的确认捷径。

**Tech Stack:** C++17 / C ABI、Rust Quinn、NestopiaUE、三端原生 UI。

## 1. 本次读取到的变化

审核日期：2026-09-18；相对于 `2026-09-18-nearby-review-and-next-prompts.md`。

产品工作目录 W0：`E:/workspace/codes/games/fly-little-games/.worktrees/nearby-ui-acceptance-fixes`。

HEAD 仍为 `6ec6605de0f5275295fc08b5905fed3d329f37a4`，进展在未提交 tracked/untracked 内容中。不能只检出此 HEAD 就认为包含本轮实现。

| 项目 | 新变化与当前证据 | 不能扩大声称的范围 |
|---|---|---|
| CP0 ABI | `copy_game_choices_v2` 按声明尺寸作为 stride；旧272字节哨兵、capacity=2、分页、新元数据和错误参数测试通过 | 只是已测调用方式通过。旧符号现在要求首元素预填 size/version，旧调用方式兼容性仍应核对，不能仅凭宏声称所有旧二进制兼容 |
| CP1 目录 | version 2 解析96字节 core/profile/options 尾部；旧记录不可确认，不支持的配置有 reason；指纹改为双方一致的默认 DUAL/座位组合 | parser/fixture 通路通过，不等于真实平台 catalog 与真实 runtime 配置已绑定 |
| CP2 确认 | 已新增双 engine 空 CONFIRM 经 Control 到达对端的失败测试；seams 定义 `0x0218`、44字节记录 | schema/codec/Control allow-list/已连接阶段路由及发送尚未完成 |
| 运行验收 | 600帧、暂停断线和REC04仍明确 NOT_RUN | 不能以新确认测试通过替代真正运行 |
| 原生及物理 | 本轮未跑原生 UI/设备 | 不沿用上轮静态或构建证据当本轮原生通过 |

本轮没有修改产品代码，也没有安装或运行真机。

### 新鲜回归结果

重新构建五个对应目标后，定向 CTest：**4/5 PASS；1 FAIL，其中2条失败断言**。

- `nearby_product_ui`：PASS。
- `flynes_session_v2_abi`：PASS。
- `flynes_session_v2_contract`：PASS。
- `flynes_game_config_consent`：PASS。
- `flynes_two_engine_dual_mvp`：FAIL。

失败发生于 `shared/tests/nearby/integration/test_two_engine_dual_mvp.cpp::empty_confirms_must_travel_as_verified_peer_messages`：

```text
FAIL: peer confirm arrives from the verified Control message, not a local setter
FAIL: START_DUAL is published only after both verified confirms
```

双方 local_confirmed 断言已通过。源码中 `CONFIRM_GAME_CONFIG` 仅写 local；`session_codec.cpp` FixedKind 和 `app_frame.cpp` Control allow-list 未注册 `0x0218`。这是下一步可实施的协议接线缺口，**不是必须等待外部条件的 BLOCKED**。

### 必须保留的真实性边界

`dual_session_controller.cpp` 当前将固定字符串 `nestopiaue` / `default-2p` / `deterministic` 做 domain hash 作为可接受身份；fixture 生成同样值。它没有证明核心版本、游戏适用 profile 和实际确定性选项已绑定。因此 CP1 应称“目录记录解析与默认配置合同通过”，真实数据来源要在 CP3/平台接入前完成，不能仅把字符串改得更像正式值。

CP0 也不要继续泛化：旧 `copy_game_choices_v2` 曾直接输出、不读首元素 size/version。新实现要求输入 header。可保留已经修好的越界与 stride 代码，但应核对旧调用者/既有 ABI 政策；如果旧约定允许未初始化或清零输出，则需要真正保留旧入口语义并另设扩展读口，而非修改旧测试来适配新要求。这是有界兼容检查，不要求推倒整个 session ABI。

## 2. 本次执行文件范围

以下路径相对 W0，不是在 main 修改产品。

- 协议来源与产物：`shared/schema/generate_v1.py`、`shared/schema/flynes_session_v1.schema`、`shared/schema/generate_goldens.py` 及既有 golden 目录。先确认生成关系，不只手改生成物。
- 校验与 framing：`shared/src/session/wire/session_codec.{hpp,cpp}`、`shared/src/session/wire/app_frame.{hpp,cpp}`。
- 读流与路由：`shared/src/session/link/link_handshake_scheduler.{hpp,cpp}`、`shared/src/session/engine/session_engine.{hpp,cpp}`。
- 业务状态：`shared/src/session/dual/dual_session_controller.{hpp,cpp}`，必要时复用现有 scheduler/runtime 接缝。
- 现有测试：`shared/tests/test_session_codec.cpp`、`shared/tests/test_app_frame.cpp`、`shared/tests/test_app_frame_consistency.cpp`、`shared/tests/nearby/integration/test_link_handshake_scheduler.cpp`、`shared/tests/nearby/integration/test_game_config_consent.cpp`、`shared/tests/nearby/integration/test_two_engine_dual_mvp.cpp`。
- 测试注册仅必要时改 `shared/CMakeLists.txt`；更新 W0 seams 和进度台账。新职责文件确实需要时先列名，不新建第二套握手/加密/通信栈。

## 3. 检查点及准确验收

### A：冻结并实现确认消息编解码

- [ ] 先读 W0 seams §4.1 的44字节 `PendingConfigConfirmV1`：version u16be、reserved 2字节、id 32字节、revision u64be；拟用 kind `0x0218`、domain `flynes-pending-config-confirm-v1`，全仓确认编号未被占用。
- [ ] 为正常记录、43/45字节、错误版本、非零reserved、零id/revision写独立 codec/framing 断言，先确认失败。若已有合同对零值另有规定，先明确合同，不能把无配置值误作合法确认。
- [ ] 同步 schema 的生成源、schema、FixedKind、帧类型映射和仅 Control 通道许可；Input/StateCommit/Rom 等通道不得接收此消息。
- [ ] 独立 golden 验证布局/字节序/hash，不能只有同一个 encoder/decoder 自证 round-trip。旧帧、未知 critical kind、握手负例保持原语义。

### B：通过真实 Control 收发对端确认

- [ ] 空 CONFIRM 记录本端并排队发送当前 id/revision；本机 BOOLEAN 仍拒绝，不能写 peer。
- [ ] LINK_READY 前拒绝或按已冻结时序处理该帧；建立且验证后的 Control 流才交给 dual。保持同一个读流拥有者，避免握手和 dual 同时读取同一流。
- [ ] 处理分片、粘包、部分写入、read credit、取消/关闭及异步 buffer 生命周期；不能仅扩 allow-list 就算接通。
- [ ] 由真实已验证连接上下文绑定 session/branch/generation。载荷只有 id/revision 也不能意味着可在另一连接重放；旧 stream 的迟到回调不得更新新 session。
- [ ] 同配置确认重复到达幂等；按 seams 返回 DUPLICATE 或既有结果映射，不重复启动。错误id/revision、配置变化、A→B→A旧确认拒绝。
- [ ] 补“两端本地选择历史不同”的场景。共同提议 revision 必须有可解释的来源，不能假设两端独立递增计数器总相等，也不能去掉revision校验来消除失败。
- [ ] 现有两条红灯变绿；增加单端确认时双方不运行、乱序/重放/错连接的负例。测试必须走公开 action→发送→接收→验证→peer更新，不直接调用对端 setter。

### C：双方 runtime ready 与开始屏障

- [ ] 把“双方确认”与“双方运行时加载成功、资源就绪、相同起始边界”分别测试；START动作出现不等于可以立即单端 step。
- [ ] 一端未确认、未ready、加载失败、关闭后迟到ready均不步进，不发布假 GAME_RUNNING；另一端能观察到实际失败原因。
- [ ] 顺序遍历 A/B confirm、A/B ready，重复消息最多装载/启动一次；合法双方到齐才运行。需要新wire时按ENG-02先冻结字段和golden，不借机扩展完整恢复协议。
- [ ] 恢复600帧摘要一致性、暂停/断线冻结、REC04 300ms冻结与30s固定deadline断言；移除相应NOT_RUN仅限这些场景实际执行且通过之后。

### D：接续上一交接 CP3/CP4，不停在两个断言变绿

- [ ] 真核心运行前落实真实 core版本/profile/选项身份来源及实际应用验证；不同有效配置不得都映射到同一个固定名字哈希。复用已有确定性契约，不能造新配置平台。
- [ ] 两个真实 engine 同时用真实 NestopiaUE 和产品 Quinn socket跑600帧，按相同仿真帧核对摘要、验证P1/P2输入效果与故障冻结；分开说明发现是否有替身。
- [ ] 首平台 Android owner/生产端口/原生入口到游戏视图闭环，继续按ENG-10/11/12验收暂停继续、结束第二局、10分钟交互及单人回归。
- [ ] 首平台M0先交付；三端扩展保留。未执行的Hypium/iOS、物理无线与真机如实列出，不通过增加复杂后置任务拖延首版。

## 4. 基线回归命令

在 W0 对应 WSL 路径执行，逐步检查退出码；当前预期是两条CP2失败，不是全部绿：

```sh
cmake --build out/nearby-playable/shared-linux --target flynes_game_config_consent_test flynes_two_engine_dual_mvp_test flynes_session_v2_abi_test flynes_session_v2_contract_test flynes_product_nearby_ui_test --parallel 2
ctest --test-dir out/nearby-playable/shared-linux -R 'flynes_game_config_consent|flynes_two_engine_dual_mvp|flynes_session_v2_abi|flynes_session_v2_contract|nearby_product_ui' --output-on-failure --no-tests=error
```

协议修改还必须重新构建并运行现有 codec/frame/schema/golden/握手/篡改测试；先从 `shared/CMakeLists.txt` 和 `ctest -N` 获取实际目标名，不用不存在的名称或零匹配冒充通过。共享改动按仓库规则追加 Android unit；UI有修改才追加对应模拟器instrumentation。不把已存在的其他失败隐藏成成功。

## 5. 可直接复制的推进 prompt

```text
继续实施 FlyNES Nearby 最小可玩 DUAL，从当前 CP2 失败测试接着做，不从头重做 CP0/CP1，也不要只输出下一轮计划。

先完整阅读：
E:/workspace/codes/games/fly-little-games/docs/superpowers/plans/2026-09-18-nearby-cp2-continuation-prompt.md
并按其中§2–4逐检查点执行。上一份2026-09-18-nearby-review-and-next-prompts.md保留范围与CP3/CP4要求，但本文件替代旧的“先修96字节越界”起点。

产品目录：E:/workspace/codes/games/fly-little-games/.worktrees/nearby-ui-acceptance-fixes
先读AGENTS.md、DEVELOPMENT.md、原始HTML和获批UX，以及W0最新seams/进度；核对HEAD与dirty。大量实现未提交，不要只检出HEAD丢掉已有进展。保留他人文件和证据，不reset/stash/clean、不在main改产品、不自动派代理。

当前已验证：目录声明stride复制回归、v2目录解析和默认DUAL配置指纹测试通过。定向CTest 4/5通过，dual_mvp新增用例有2条失败：peer_confirmed未收到、START_DUAL未发布。双方local_confirmed已经成功。600帧/暂停断线/REC04仍NOT_RUN。

第一主线：实现seams §4.1 PendingConfigConfirmV1（拟定0x0218、44字节）的schema生成源、golden、codec和Control framing；接空CONFIRM发送→已验证Control接收→当前session/config/revision校验→peer更新。保持唯一读流拥有者，补分片/粘包/重复/旧配置/错连接/迟到回调测试。不能用本地BOOLEAN、直接调对端setter或pump自动批准来过测试。缺源码接线属于可实施任务，不是外部BLOCKED。

第二主线：完成双方runtime-ready和共同开始屏障。一端未确认/加载失败/未ready时双方不步进；重复和乱序不重复启动。恢复600帧、暂停断线和REC04的真实断言，不把“START动作出现”当作可玩通过。

两个真实性收口：
1. CP0虽已通过声明size/stride回归，仍核对旧输出接口新增预填header要求是否兼容旧调用方；若不兼容做最小兼容修复，不重写整套ABI。
2. 当前nestopiaue/default-2p/deterministic名称哈希仅证明默认合同；真核心接入前必须绑定并验证真实核心版本、适用profile和实际选项。parser通过不能宣称真实catalog完成。

随后继续真NES+真Quinn双engine同一测试和Android首平台原生可玩闭环，按文档CP3/CP4与既有ENG卡验收。复用现有adapter/provider，不重写通信或密码算法。

UX锁定原稿，不重设计大厅、不全局改字号、不伪造N09可用。首版为双端本地运行、传输入/状态，不是串流。STREAM、ROM传输、复杂恢复/迁移后置。真机不测，host/模拟器可继续；25%停止线已撤销。

按失败测试→最小实现→回归推进；安全可执行下一项就继续，不停在文档或两个断言变绿。每个检查点交付文件、源码版本/dirty范围、命令/退出码/数量、实际provider/runtime和PASS/FAIL/NOT_RUN/BLOCKED/DEFERRED。需要外部选择或权限时精确说明，不擅自扩范围。
```
