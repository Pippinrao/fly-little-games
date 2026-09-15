# Nearby 双端非真机验收追踪（骨架） — 2026-09-15

本文是 Nearby 双端联机**非真机验收文档**（计划 Task 16）的需求追踪表骨架：
逐项登记总体方案 B01–B16、接口合同 IF01–IF16 与测试设计 81 个用例族，
**状态列一律留空 `—`**，等待 W0 依据本次可复核的实测证据填写。
本文不作任何通过/失败判断，也不重复已获批设计文档的正文。

条目来源（三份均为已获批文档，本 worktree 内）：

- `docs/superpowers/specs/2026-09-13-nearby-backend-repair-design.md` —— 总体方案 B01–B16（该文档以表格条目形式列出，无缺号）。
- `docs/superpowers/specs/2026-09-13-nearby-interface-design.md` —— 接口合同 IF01–IF16（以章节标题形式列出，无缺号）。
- `docs/superpowers/specs/2026-09-13-nearby-test-design.md` —— 用例族 API/PORT/PAIR/WIRE/GAME/CONTENT/MEDIA/REC/UX/PERF 共 81 条，与其 §10 自述的族数一致。

提取方式：各 ID 均按原文逐字摘出（B 取方案表第二列，IF 取章节标题，用例取表格的场景列；REC 取「事务族 + 决策前期望」）。
若后续设计与本文不一致，以已获批设计文档为准并回改本表。

## 证据分级约定

本套验收文档只使用下列五种状态，语义互不混用：

| 状态 | 含义 |
|---|---|
| `PASS` | 有本次可复核的命令输出证据 |
| `FAIL` | 实测不通过 |
| `BLOCKED` | 有明确环境/依赖阻塞，必须附阻塞原因 |
| `NOT_RUN` | 根本没有执行 |
| `DEFERRED` | 本轮明确延期（STREAM 专属 MEDIA/PERF，以及真机物理项） |

三条硬规则，不得违反：

1. **0 tests matched 视为 FAIL**。筛出的测试目标一个都没匹配上（空跑、目标改名、标签未注册）时记 `FAIL`，不是 `PASS`。
2. **只编译通过不算 PASS**。构建成功、类型检查通过、包能产出，都不能代替该条要求的实测断言结果。
3. **不得把 `BLOCKED`/`NOT_RUN` 写成 `PASS`**，也不得用 mock、另一平台的结果、模拟器或截图替代本条要求声明的证据形式。

补充约定：`证据/备注` 列当前只登记设计交叉引用（层级、C 编号/IF 编号对应），**不得当作状态证据**；实测命令、输出摘要与阻塞原因由 W0 追加。

## 追踪表

### 1. 总体方案 B01–B16

| ID | 要求摘要 | 状态 | 证据/备注 |
|---|---|---|---|
| B01 | production 公共引擎不链接平台 UI/OS 头；所有系统访问经 ports；三端不存在复制的配对/游戏 reducer | — | — |
| B02 | 完成回调重复不重做；旧代次回调不破坏新连接；相同 operation key 冲突结果可检测 | — | — |
| B03 | 无 ROM 完成码/QR 认证并进入同一空大厅；验证未完成绝不显示“双人联机中” | — | — |
| B04 | 错码、过期、刷新、并发两入口、匹配冲突、拒绝、退出、低 GATT MTU、错 pin/反射/重放/PSK 均安全结束当前尝试 | — | — |
| B05 | 对所有允许消息逐字节切分/拼接、FIN 半包、错流/type/channel、长度溢出、未知 critical、未绑定早到包验证无越权效果 | — | — |
| B06 | 所有 platform-pair/authority/seat 组合的端口映射一致；连续 press/release 在丢包/乱序/repair 下不被吞掉 | — | — |
| B07 | 截止已发 target 不平移；已 committed frame 不重写；raw/rollback 达界冻结；重放只发布最终画面 | — | — |
| B08 | runtime 源 PCM 正确区分真实零样本与欠载；两消费者不重复 destructive pull；同 published sequence 的 authority/guest bytes/hash 一致 | — | — |
| B09 | 视频缺片/旧 generation/decoder 晚回调丢弃；IDR/config 前不显示；音频 FEC 单丢失恢复、多丢失记录静音，静音不停止 timeline | — | — |
| B10 | guest 本机 touch→最终成为 canonical committed 的对应输入真实 presentation：DUAL p95≤80 ms、STREAM p95≤150 ms；预测回滚后用修正后的 canonical 首次呈现，不能取较早被撤销画面。每个输入边沿须在 1 秒内匹配，否则整轮失败。每台设备本机 A/V 偏差绝对值 p95≤50 ms，FEC 与全队列延迟计入；PERF04 反例验证统计器 | — | — |
| B11 | storage flush/replace 失败不推进 committed/replayable；active config/head/SRAM 原子一致；所有崩溃点无双 writer | — | — |
| B12 | 30 秒 deadline 不被本机 send/重启延期；recovery 未释放不能恢复 step；暂停中断重连后仍暂停 | — | — |
| B13 | 无 ROM 的 guest 只能保存不透明包；有完整 proof 才可接管；分区后用户新 branch 不覆盖另一未来 | — | — |
| B14 | 换游戏后新 branch/config，旧输入/媒体/确认/WAL 结果不生效；旧 grant 关闭、完整 ledger 交接、CAS/source release、双方不一致 link 摘要按 §4.4–§4.5 收敛；大厅类别/搜索/双人过滤不被连接改变 | — | — |
| B15 | 三端同 fixture 的 snapshot/动作/错误及逻辑尺寸 UI 相同；OS 权限和实际能力差异映射一致 | — | — |
| B16 | 真实离线已认证 bearer 任意支持组合可用，不能拿 Android↔Windows probe、路由器 LAN、模拟器帧率或 HTML 截图代替 | — | — |

B09/B10 在同一设计文档正文另有媒体门禁补充，需一并验收：guest 有效帧率不低于声明源帧率 92%（NTSC 至少 55 fps、PAL 至少 46 fps）；不得出现连续 100 ms 及以上音频欠载；未静音 10 分钟窗口内 gain-before 非 canonical/插入静音总量 ≤200 ms 且 ≤0.1%，单次 correction ≤200 ms。精确采样与统计见测试设计 §8。

### 2. 接口合同 IF01–IF16

| ID | 要求摘要 | 状态 | 证据/备注 |
|---|---|---|---|
| IF01 | 分层与 ABI 选择：保留 `shared/include/flynes/flynes_session.h` 作为 session 公共入口，追加 V2 类型和函数，V1 大小/字段/枚举/拒绝语义不变 | — | — |
| IF02 | 基础类型与结果：类型、字段/值域/所有权 | — | — |
| IF03 | Session 公共 API 签名与交互形状 | — | — |
| IF04 | View 读取 | — | — |
| IF05 | UI action 合同：action_kind 完整首版业务集合；公共层绑定上下文/用户选择，UI 不在 submit 重传 hash/revision | — | — |
| IF06 | 公共 product policy：与 UX 逐页对齐 | — | — |
| IF07 | 所有 port 的公共模式：异步方法 `start(OpToken, Request) → ACCEPTED \| error`，完成经 `deliver(inbox, Event)`，ACCEPTED 才接管引用 | — | — |
| IF08 | 时钟、executor、权限、扫码和平台事实 | — | — |
| IF09 | BLE 与 bearer | — | — |
| IF10 | QUIC | — | — |
| IF11 | key、crypto、secure store、object store 和 content | — | — |
| IF12 | Runtime：默认 RuntimePort 包装公共 `fly_runtime`，不搬模拟算法；除 capability/只读元数据外仅 simulation worker 串行调用，active 与 scratch 用不同 handle | — | — |
| IF13 | Codec 与 sinks | — | — |
| IF14 | 装配和资源释放次序：composition root 创建时钟/executor/系统 provider 并调用 create，引擎持引用 | — | — |
| IF15 | 错误到 UX 的映射 | — | — |
| IF16 | 接口完成与 UX 对齐检查 | — | — |

### 3. 测试条目

用例族共 81 条，与测试设计 §10 自述数量一致（API15、PORT8、PAIR6、WIRE3、GAME7、CONTENT3、MEDIA8、REC9、UX18、PERF4）。
本表 3.1–3.8 列 API/PORT/PAIR/WIRE/GAME/CONTENT/REC/UX；MEDIA/PERF 见第 4 节（预填 `DEFERRED`）。

#### 3.1 API（API01–API15，15 条）

| ID | 要求摘要 | 状态 | 证据/备注 |
|---|---|---|---|
| API01 | C/C++ 最小消费者只包含公共头；V1/V2 各自 size；未知 version/非零 reserved | — | 层级 L0；对应 IF01–02 |
| API02 | 缺 Clock/Executor/认证 provider；缺 Camera；缺 encoder | — | 层级 L0/L1；对应 IF07–14 |
| API03 | action 入队后更换 config，再消费旧 CONFIRM；同 request_id 重复；另测仅倒计时/view 刷新 | — | 层级 L1；对应 IF03、IF05 |
| API04 | start 返回 error 后违规回调；ACCEPTED 后重复相同 terminal 和不同 terminal | — | 层级 L1；对应 IF07 |
| API05 | generation g 取消后创建 g+1，再送 g 的 Wi-Fi/QR/codec/storage 结果 | — | 层级 L1/L2；对应 IF02、IF07 |
| API06 | 2 槽 action 队列和结果槽填满；再投普通动作、terminal、shutdown | — | 层级 L1；对应 IF03、IF07 |
| API07 | bytes 提交后调用者立即改写/释放；接受/拒绝/取消每种结果 | — | 层级 L1+ASan；对应 IF02、IF07 |
| API08 | UI 持 view r；engine 产生 r+1 并 destroy；旧 view 分页读取再 release | — | 层级 L1+ASan；对应 IF04 |
| API09 | ports 在 start 内直接投递；同时任意线程回调 | — | 层级 L1+TSan；对应 IF07–08 |
| API10 | begin_shutdown 后持续送 callback；某 port 永不 terminal | — | 层级 L1/L3；对应 IF03、IF14 |
| API11 | source/媒体 sequence 从 0 开始、uint64 上界、大小乘法溢出 | — | 层级 L1/L2；对应 IF02、IF12–13 |
| API12 | release 产品链接/启动配置、测试 provider 参数注入尝试 | — | 层级 L0/L3；对应 IF01、IF16 |
| API13 | 独立 C 消费者仅经 acquire_view/copy_actions/copy_game_choices 获取 ActionDescriptor、ApprovalToken 及 typed choice，依次走匿名接受、PROPOSE_GAME、配置确认、文件许可/导入及恢复选择→submit；无内部头/测试私有入口 | — | 层级 L0/L2；对应 IF03–06 |
| API14 | 保持 engine/connection/game scope 相同，仅推进 authority_term 或本机 durable writer_generation；放行旧 step/import/promote/save 及迟到结果；另在排队后、effect 前切换 lease | — | 层级 L1/L2；对应 IF02、IF11–12 |
| API15 | retain 公开 ApprovalToken 后 release 原 view，再更换 request/config/offer/recovery source 或向新 engine 提交；同 request_id 重发相同/不同 token 或 choice；另仅无关 view 刷新 | — | 层级 L1/L2+ASan；对应 IF03–05 |

#### 3.2 PORT（PORT01–PORT08，8 条）

| ID | 要求摘要 | 状态 | 证据/备注 |
|---|---|---|---|
| PORT01 | 两端不同 raw clock；暂停/后台 10 秒；clock generation 变化 | — | 层级 L1/L3；对应 IF08 |
| PORT02 | GATT MTU23、分片背压、换 MTU、central 与 protocol role 反向 | — | 层级 L2/L3；对应 IF09 |
| PORT03 | 空认证 plan 交集、错误 creator/confirmation role、拒绝系统弹窗 | — | 层级 L1/L4；对应 IF09 |
| PORT04 | full TLS 正确/错误 pin、恢复 ticket、exporter context/长度错误 | — | 层级 L2 真实 QUIC/L4；对应 IF10 |
| PORT05 | ObjectStore flush/replace 失败；SecureStore revision 冲突；短读取 | — | 层级 L1/L2/L3；对应 IF11 |
| PORT06 | codec 请求 profile 与实际输出不符、flush 后迟到帧 | — | 层级 L1/L3；对应 IF13 |
| PORT07 | 同一音频块向双 sink 提交，host mute/guest 慢消费/route 变化 | — | 层级 L2/L3；对应 IF13 |
| PORT08 | 真实 provider 创建独立 Identity/SessionSigning/TLS 材料；错 purpose 句柄互换；材料 durable 前后/manifest 切换前后 kill；换 child、原 link 合法恢复、link 终结再新建 | — | 层级 L1/L2 真实 crypto/L3；对应 IF10–11、IF14 |

#### 3.3 PAIR（PAIR01–PAIR06，6 条）

| ID | 要求摘要 | 状态 | 证据/备注 |
|---|---|---|---|
| PAIR01 | 创建 60 秒邀请；查询 `012345`；无匹配/多 context 匹配/限流边界 | — | 层级 L1/L2 |
| PAIR02 | 同一 UI generation 的 code/QR child 并发请求；房主先接受其一 | — | 层级 L2 |
| PAIR03 | join 提交 commit 但房主未 accept，之后拒绝或页面退出 | — | 层级 L2 |
| PAIR04 | 真实 BLE/SAS 双端握手，单端确认、码不同、双方确认 | — | 层级 L2/L4 |
| PAIR05 | 合法 QR、签名/commitment 替换、反射/重放/到期；接受丢失 | — | 层级 L2/L4 |
| PAIR06 | 使用总设计唯一 V2 GATT 注册：type27 REQUEST body32 bytes、type28 MATCH body136 bytes，完整 logical 分别 72/176 bytes；交换 outer/body version、方向、type、长度及 nonce/context；注入并行草案 V1 type28/29、0x214/0x215 | — | 层级 L2+golden；对应 B04–05 |

#### 3.4 WIRE（WIRE01–WIRE03，3 条）

| ID | 要求摘要 | 状态 | 证据/备注 |
|---|---|---|---|
| WIRE01 | 所有合法 record 切分/粘合；两 stream 交织；FIN 半包/重复 bind 流 | — | 层级 L2 |
| WIRE02 | 类型跨 channel、unknown critical、reserved、计数/长度越界、旧 scope/generation | — | 层级 L2+fuzz |
| WIRE03 | bind FIN 之前早到合法 Control；TLS 仅 connected；HELLO 版本不兼容 | — | 层级 L2 |

#### 3.5 GAME（GAME01–GAME07，7 条）

| ID | 要求摘要 | 状态 | 证据/备注 |
|---|---|---|---|
| GAME01 | 两端无 ROM 建立 link；随后从原大厅提议支持双人的 fixture | — | 层级 L2/L4 |
| GAME02 | authority=P1/P2、换 authority/seat/content/profile；本机 mute/查询改变 | — | 层级 L2 |
| GAME03 | seq100 按 A 于 frame42，seq101 释放于 43；丢/乱序/repair | — | 层级 L2 |
| GAME04 | 相同 input key 同 bytes 重复、不同 mask 或 target；已 committed 冲突 | — | 层级 L2 |
| GAME05 | raw=47→48、模拟差 9→10、ring 硬界 12；guest 超前 4→5；追帧量子 | — | 层级 L2 |
| GAME06 | D=2/3/4 与 PAL/NTSC；120 中立帧门禁通过/摘要不一致 | — | 层级 L2/L4 |
| GAME07 | 准备期返回大厅、运行期 RETURN_TO_LOBBY、换新游戏后旧输入/媒体/确认 | — | 层级 L2/L4 |

#### 3.6 CONTENT（CONTENT01–CONTENT03，3 条）

| ID | 要求摘要 | 状态 | 证据/备注 |
|---|---|---|---|
| CONTENT01 | 只批准 send；再批准 receive；完成 hash 校验但未批准 import | — | 层级 L2/L3 |
| CONTENT02 | 错 declared hash、>8MiB、offset 越界、10 秒无进展、取消 | — | 层级 L2 |
| CONTENT03 | 缺 ROM 选择 STREAM 且拒绝补齐；以后获准导入 | — | 层级 L2/L4 |

#### 3.7 REC（REC01–REC09，9 条）

| ID | 要求摘要 | 状态 | 证据/备注 |
|---|---|---|---|
| REC01 | 初始开始/Source handoff：source writer 与候选准备门禁一致；未允许时无 game step | — | — |
| REC02 | pause/resume/seat/input-delay/resync：冻结并按各自允许的 abort/恢复路径；不越过 input close/prime | — | — |
| REC03 | DUAL→STREAM：pre_safe 可读、旧 branch 冻结，失败不恢复已判无效 DUAL | — | — |
| REC04 | reconnect/release 尾链：原 deadline 不可滑动；30s 边界和 5s 限定 grace 只授权合法 phase | — | — |
| REC05 | save/end/takeover/fork：无合法 proof 不生成可用 user save/接管；保持未决包 | — | — |
| REC06 | object GC/存储损坏：先保持旧 root，不能把缺失新 candidate 当成功 | — | — |
| REC07 | 空大厅 link 恢复与换局：NONE game 用 link 专用基线，不造 GENESIS 游戏 | — | — |
| REC08 | parent ledger 跨 child 交接：old child 仍 active+FROZEN 时先修复/关闭 reservation；逐 phase 故障覆盖 ChildInputCloseV2、LedgerHandoffV2、PrestartAbortTombstoneV2 及 read_root/CAS | — | 层级 L2；总设计 §4.4 |
| REC09 | reconnect 混合 child 状态：先交换 phase=ROUTE_ONLY 的 LinkResumeSummaryV2；旧 TAIL_STATUS prelude 完成后 read_root 重读并交换 RECONCILE；覆盖 §4.5 九行并交换方向、故障注入 | — | 层级 L2；总设计 §4.5 |

#### 3.8 UX（UX01–UX18，18 条）

| ID | 要求摘要 | 状态 | 证据/备注 |
|---|---|---|---|
| UX01 | 无 link 进入 G00 | — | 对应 C01；层级 L1/L3 |
| UX02 | 4 分类×2 开关×空/命中/不命中查询×未连/连接中/已连/中断 | — | 对应 C02；层级 L1/L3 |
| UX03 | BUILTIN 只有单人；开双人筛选再关 | — | 对应 C03；层级 L1/L3 |
| UX04 | 无选中游戏打开 N00，分别点三动作 | — | 对应 C04；层级 L1/L3/L4 |
| UX05 | `012345`、首尾空白、空/5/7 位/字母/全角数字/过期 | — | 对应 C05；层级 L1/L3 |
| UX06 | N04/N05 等待 host，拒绝/接受 | — | 对应 C06；层级 L2/L3/L4 |
| UX07 | 一端 SAS 确认、两端不同、两端同 transcript 确认 | — | 对应 C07；层级 L2/L3/L4 |
| UX08 | 合法 QR/篡改/重放/过期 | — | 对应 C08；层级 L2/L3/L4 |
| UX09 | socket 成功/ChannelBind 未完/单端 ready/全部 link 门禁 | — | 对应 C09；层级 L2/L3/L4 |
| UX10 | camera 拒绝、Wi-Fi 拒绝、版本失败、codec 未开始 | — | 对应 C10；层级 L1/L3/L4 |
| UX11 | N09 某端确认后变 authority/seat/content/plan；另测 mute/展开详情 | — | 对应 C11；层级 L2/L3 |
| UX12 | send-only、send+receive、verified-only、approve-import | — | 对应 C12；层级 L2/L3/L4 |
| UX13 | 从 N09 回 G00 再换游戏；运行中退出；单人游戏可浏览 | — | 对应 C13；层级 L2/L3/L4 |
| UX14 | 已连断线、后台恢复、暂停时断线 | — | 对应 C14；层级 L2/L3/L4 |
| UX15 | rename/delete/block/reset；匿名发现再次出现 | — | 对应 C15；层级 L2/L3 |
| UX16 | cancel/refresh/timeout 后晚到 QR、SAS、Wi-Fi、codec、确认 | — | 对应 C16；层级 L1/L2/L3 |
| UX17 | 所有 screen/弹窗×宽 320/375/550/580/640/736/900/1024 | — | 对应 C17；层级 L1 几何/L3 |
| UX18 | 640×360/736×414/844×390×1.0/1.3/2.0 字倍×安全区×键盘 | — | 对应 C18；层级 L3 原生 |

### 4. MEDIA / PERF（本轮预填 DEFERRED）

| ID | 要求摘要 | 状态 | 证据/备注 |
|---|---|---|---|
| MEDIA01 | step_exact 输出真实零 PCM、source first=0；容量不足；旧 pull 欠载 | DEFERRED | STREAM 延期 |
| MEDIA02 | 两帧 staging 内回滚；发布后深回滚 | DEFERRED | STREAM 延期 |
| MEDIA03 | 四块 960-byte 固定模式，逐个丢其中 1 块；再丢 2 块；parity 迟到 | DEFERRED | STREAM 延期 |
| MEDIA04 | payload budget 恰好够 DATA/FEC 和少 1 byte；运行中预算下降 | DEFERRED | STREAM 延期 |
| MEDIA05 | AU 乱序/缺片/重复片/冲突片、3→4 在途、256KiB 边界、新 generation | DEFERRED | STREAM 延期 |
| MEDIA06 | encoder queue 满、慢 decoder、慢 video sink、audio 欠载/静音 | DEFERRED | STREAM 延期 |
| MEDIA07 | 两端 clock offset 差 5 秒、±100ppm drift、播放设备路由切换 | DEFERRED | STREAM 延期 |
| MEDIA08 | DUAL 连续两次 deep audio correction 在 30 秒内、持续失步 | DEFERRED | STREAM 延期 |
| PERF01 | 已认证正常近场条件，每设备配置/模式 3 次独立会话；每次稳定运行至少 10 分钟、至少 1000 个可辨认 touch 动作 | DEFERRED | STREAM 延期 |
| PERF02 | 同一运行记录 source/media/video 因果及本机 audio playout 位置；统计 gain 前 sample status，静音仍计游标 | DEFERRED | STREAM 延期 |
| PERF03 | 至少 30 分钟持续运行、背景/恢复、视频拥塞、2s 链路断开、温控/资源事件 | DEFERRED | STREAM 延期 |
| PERF04 | L2 固定测量反例：同一 edge 在 40ms 呈现预测帧，迟到远端输入使其回滚，100ms 才呈现纠正后的 canonical 帧；另有一 edge 在 1 秒内无 canonical 呈现 | DEFERRED | STREAM 延期 |

## 实测环境与命令

待填。

## 资源泄漏与负向审计

待填。
