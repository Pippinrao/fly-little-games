# 多人联机公共接口设计与 UX 映射

状态：**待评审的接口合同；本文件中的签名仅为设计，不创建或修改头文件、实现或 schema。** 日期：2026-09-13。

配套：[后端总设计](2026-09-13-nearby-backend-repair-design.md)、[已确认 UX](2026-09-13-nearby-ui-parity-design.md)、[测试设计](2026-09-13-nearby-test-design.md)。UX 的 U01–U11、N00–N12/G00、C01–C18 是应用行为依据。本文件细化总设计 §3、§10；接口名称/字段在本次评审通过后才成为实现输入。

## 1. 分层与 ABI 选择（IF01）

保留 `shared/include/flynes/flynes_session.h` 作为 session 公共入口，追加明确的 V2 类型和函数，现有 V1 大小、字段、枚举值和拒绝语义不变。V2 是进程 ABI 版本，与拟议 wire major=2、对象编码 V1、仓库 VERSION 是四条不同版本轴。不得把 C struct 内存直接发送上网。

公共层分三块：SessionEngine 负责联机事实与副作用；shared product policy 负责 screen/action/text-key/布局和大厅稳定过滤投影；平台仅装配 ports、渲染 UI、转发事件。本机输入草稿、焦点、详情展开和浏览偏好由 UI 持有，不作为网络配置。平台不再有可写 `connected`、`authority`、`mode` 或 `peerVerified` 的业务字段。

下面签名采用 C ABI 设计记法。所有非 opaque DTO 首部统一为 `struct_size:u32, abi_version:u32=2`，显式 flags/reserved 使用 u32 且未定义位必须为零；不跨 ABI 使用 bool、size_t、C++ enum、STL、异常或平台对象。数组使用明确元素类型、u32 count/capacity；字节长度为 u64，但每个接口按自身上限先检查再转换成本机地址长度。ID/hash 按 bytes 比较，时间/计数溢出失败，不回绕。

## 2. 基础类型与结果（IF02）

| 类型 | 字段 / 值域 / 所有权 |
|---|---|
| `fly_session_v2_t` | opaque 引擎；create 返回一个拥有引用，只有 shutdown 完成后可 destroy |
| `fly_session_inbox_v2_t` | opaque、可 retain 的回调入口；引擎与 ports 分开持有引用，关闭后仍可安全拒绝迟到回调 |
| `fly_session_view_v2_t` | immutable 快照引用；可跨 UI 帧读取，不依赖 engine 继续存活；用 view_release 释放 |
| `Id128 / Hash256` | `bytes[16] / bytes[32]`；未分配使用全零，仅在字段允许 absent 时合法 |
| `Scope` | `kind=ENGINE/LINK/GAME`、link_id[16]、branch_id[16]；ENGINE 两 ID 零，LINK branch 零，GAME 两 ID 非零 |
| `Fence` | engine_instance_id[16]、Scope、connection_generation:u64、config_revision:u64、authority_term:u64、writer_generation:u64、timeline_epoch:u64、seat_revision:u64、mode_generation:u64、media_generation:u64；相关轴必须匹配，不相关轴为零，具体授权规则见下文 |
| `OpToken` | Fence、operation_id:u64、transition_id[16]；operation_id 在引擎实例内单调非零，transition 为非事务操作时零 |
| `ReadBytes / WriteBytes` | data 指针、length/capacity:u64；只借用到当前函数返回，不得异步持有 |
| `BufferHandle / ResourceHandle` | 不透明本进程引用，不能落盘或上线；跨异步持有必须 retain，最终 release 恰好一次 |
| `ContentRef` | 公共目录签发的内容 ID、内容 revision、只读授权 handle；不接受 UI 传原始路径、URL 或平台 FD 作为内容身份 |
| `FrameCursor / EvidenceCursor` | 沿用 GENESIS/FRAME、NONE/PRESENT 语义；NONE 不代表 frame 0，evidence 必须含 epoch/cursor/hash |
| `ErrorInfo` | category、reason_key、stage、retry_class、Scope、operation_id；平台内部错误码仅诊断，不作为用户文案 |
| `ApprovalToken` | opaque 本进程引用；公共引擎签发并登记动作与绑定上下文，不能序列化、落盘或上线；view 持有引用，另行持有用 token_retain/token_release；持有引用不延长授权有效期 |
| `ActionDescriptor` | descriptor_id、action_kind、enabled、reason/text key、typed choice schema、ApprovalToken（禁用时 absent）；只读描述，枚举或读取本身不代表用户批准 |
| `WriterGuard` | scope、执行目标authority_term/writer_generation、expected top-level durable root hash/revision、typed lease_guards数组（本机各受影响branch的expected lease hash/revision、authority_term、writer_generation或明确ABSENT）、transition/decision token；跨局/source handoff必须同时列出source/target，不只检查目标；仅公共引擎从持久状态产生，不能由UI或adapter选择 |

`authority_term` 是 GAME 分支的协议 authority 代次：涉及当前游戏执行权、认证游戏消息或恢复证据时必须精确匹配相应事务认可的 term；ENGINE/LINK 操作没有游戏 authority，值为0。`writer_generation` 是**本机** BranchWriterLease 的执行者代次，不上线、不与对端本机 generation 比较。执行或发布当前 branch 的 runtime/storage mutation 必须带该 lease 的非零 generation；纯读取、网络收发及无 branch writer 的操作为0，但网络 GAME 消息仍检查自己的 authority_term。候选 scratch/WAL 准备不能冒充 active writer，其后提升/发布必须检查对应事务和 WriterGuard；新游戏未获执行权时不会仅因某个 term 已分配就获得 step 许可。

WriterGuard中的source可以是本机OFFLINE lease，此时其authority_term=0但writer_generation仍是非零本机写权代次；ABSENT是CAS前置条件而非“忽略该branch”。LINK操作外层Fence的两轴为0不省略它在跨局事务中改变的source/target lease_guards。这里只对本机top-level root做原子CAS；跨设备仍按既有签名证书/持久ACK事务推进，不能假设远端文件系统可参与一次本机CAS。

同 branch 换 writer 即使 config/operation 不变，也先按原事务持久切换 BranchWriterLease 并递增本机 writer_generation。simulation worker 在每个有副作用的 runtime 调用及 promote 前核对当前 lease/term/generation；撤销和在途旧 step 排空与 writer 切换串行，不能把旧 step 输出发布为新 writer 的结果。存储可先写不可变候选对象，但发布有效状态必须由 ObjectStore 的 guarded root CAS 同时检查 WriterGuard：失配返回 STALE，无新 active root。旧完成仅可结算资源/归档其确切结果，不能授予执行权、推进当前 root 或重新签发许可。operation_id、config_revision 和单纯内存比较均不能替代持久 lease 与 CAS。父 link 的 durable root、child/ledger 引用发布及跨局交接遵守总设计 §4.4–§4.5：LinkRootV2 可作为现有 top-level lease 索引下的不可变子对象，唯一 publish pointer 是该顶层 root；其 CAS 同时切换 link_root_hash 与 source/target writer lease 引用，不另建可独立推进的 child/link pointer，也不先后跨两个数据库 replace。

`fly_session_result_v2` 为独立 int32 合同，不复用旧枚举的数值含义：

| 值 | 语义 |
|---|---|
| OK=0 | 同步读取/操作完成，不等于网络已认证或事务已提交 |
| ACCEPTED=1 | 请求进入队列/系统操作被接管，最终结果稍后通知 |
| EMPTY=2 / DUPLICATE=3 | 没有可读项 / 完全相同请求或完成已处理，不再次执行 |
| INVALID_ARGUMENT=-1 / ABI_MISMATCH=-2 | 入参、字段长度或 ABI 不合；没有接管内存或产生副作用 |
| STALE=-3 / INVALID_STATE=-4 | 旧 scope/revision/token 或当前不允许；不破坏更新的会话 |
| UNSUPPORTED=-5 / PERMISSION_DENIED=-6 | 能力不可履行 / 实际权限拒绝；必须有共享 reason |
| BACKPRESSURE=-7 / BUFFER_TOO_SMALL=-8 | 未接受请求，可按合同重试 / 输出容量不足，报告 needed 且不部分写入 |
| CLOSED=-9 / CANCELLED=-10 / TIMEOUT=-11 | 接口已关闭 / 本操作取消完成 / 操作超时完成 |
| IO_FAILED=-12 / AUTH_FAILED=-13 / PROTOCOL_VIOLATION=-14 | 系统 I/O 失败 / 验证失败 / 已认证协议冲突 |
| CONTRACT_VIOLATION=-15 / BUSY=-16 | adapter 违约 / 前置资源尚未排空，不可销毁 |
| OUT_OF_MEMORY=-17 | 预算内资源分配仍失败；未接受的调用不产生副作用，已接受操作经terminal明确失败 |

同步 action 返回 ACCEPTED 后，必须有 `ACTION_RESULT(request_id, APPLIED/REJECTED, ErrorInfo)`；队列接受时状态可能尚未检查，实际消费时以当前 revision 再判定。不要把 ACCEPTED 映射成勾选成功。语法错误可同步拒绝且不生成异步结果。系统 operation 则有独立的一次 terminal completion。

## 3. Session 公共 API（IF03）

设计签名只声明交互形状，相关 DTO 的字段在后续表中定义：

```c
fly_session_result_v2 fly_session_create_v2(
    const fly_session_config_v2* config,
    const fly_session_ports_v2* ports,
    fly_session_v2_t** out_engine);
fly_session_result_v2 fly_session_submit_action_v2(
    fly_session_v2_t* engine, const fly_session_action_v2* action);
fly_session_result_v2 fly_session_submit_input_v2(
    fly_session_v2_t* engine, const fly_session_input_v2* input);
fly_session_result_v2 fly_session_acquire_view_v2(
    fly_session_v2_t* engine, fly_session_view_v2_t** out_view);
fly_session_result_v2 fly_session_read_notice_v2(
    fly_session_v2_t* engine, fly_session_notice_v2* out_notice);
fly_session_result_v2 fly_session_begin_shutdown_v2(
    fly_session_v2_t* engine, uint64_t request_id);
fly_session_result_v2 fly_session_destroy_v2(fly_session_v2_t* engine);
fly_session_result_v2 fly_session_deliver_v2(
    fly_session_inbox_v2_t* inbox, const fly_session_port_event_v2* event);
```

| 方法 / DTO | 精确合同 |
|---|---|
| create / `config` | config 包含 app/runtime/protocol capability 引用、认证支持矩阵 hash、内存/队列预算、允许的 catalog/progress namespace 与恢复选择策略、诊断开关；无默认假 provider。同步只验证/复制配置、retain ports，返回 LOADING view；随后异步读取安装身份 guard、KeyRef、好友、父 durable root 及 catalog/progress 元数据。仅加载并验证完成的能力产生可用 descriptor；加载错误/锁定有统一原因。不开 BLE、相机或 Wi-Fi |
| `ports` | 每个 port 是 size/version/context/function table/retain/release 的不可变集合，公共引擎复制 table 并 retain context；必需/可选依赖在 §6 定义，不传函数名字符串定位 OS 能力 |
| `action` | request_id:u64、expected_view_revision:u64（诊断）、ApprovalToken、choice_size:u32、带 discriminator 的 typed user choice；action_kind/scope及绑定 hash/revision 从 token 登记项取得。无额外选择的确认仅提交 token；同 request_id 的完全相同 token/choice 幂等，不同输入拒绝 |
| submit_action | 可由 UI 任意非实时线程调用；复制 choice、retain token 后返回，不重入 reducer。session worker 核验 token 来源、存活状态、确切绑定及 choice schema，再检查当前事实与真实用户意图；不能只比 UI 短码。读取 descriptor、构造 UI 按钮或拿到 token 都不自动提交批准，也不代替密码学/双方许可门禁 |
| `input` | Scope、expected seat_revision、local_device_input_id:u64、完整 buttons:u32、本机 capture clock sample；不允许 UI 填 target frame、canonical sequence 或远端 seat。公共层分配这些值 |
| submit_input | 完整 mask 的边沿顺序必须保留；不能把已接受的 press/release 合并成最后状态。背压时返回明确结果，公共引擎触发受控冻结/清本地输入，UI 不继续假装有效按键 |
| acquire_view | 返回当前完整 immutable view；不会等待网络/磁盘。没有新 revision 可返回同一个快照的另一个引用 |
| read_notice / `notice` | 只由一个 UI dispatcher drain；字段 notice_sequence、request_id、kind、outcome、ErrorInfo、view_revision。notice 表达一次性结果/提示；当前业务状态始终以 view 为准 |
| begin_shutdown | 先禁止新动作/输入、撤销资源许可，异步协调 game end/recovery 或保留未决材料，再排空 ports。正常用户“断开”必须先通过 action，不能用销毁绕过存档/终结事务 |
| destroy | 仅 SHUTDOWN_COMPLETE 且无在途系统任务时 OK；否则 BUSY 且不释放引擎。OS 永不回调的违约不能靠释放仍被引用内存解决，应保留 CLOSED inbox 并报告 stuck resource |
| deliver / `port_event` | 线程安全；event={OpToken、event_sequence、event_kind、typed payload}。只入队，不直接迁移状态。输入 bytes 在调用内复制或 retain handle；CLOSED/拒绝时不接管引用 |

notice 队列为每个已接受 action 预留一个结果槽；未有槽则 action 返回 BACKPRESSURE。状态刷新提示可以合并，但接受后的结果不能静默丢失。UI 不读取 notice 时新的普通动作停止接受；shutdown 和当前操作的完成使用保留控制容量。

`expected_view_revision`用于关联用户当时所见及诊断，不把全局view revision的字面相等当所有动作的门禁：倒计时、metrics、本机mute可以产生新view。动作真正比较的是其相关的scope、invitation/request token、config hash/revision、friend record revision等；无关view刷新不能让合法的“确认入局”一直失败。

shutdown本身不等于用户批准丢弃/保存/接管。缺少必要业务授权时只能冻结并持久保存未决恢复材料、撤销执行权和回收进程资源，不得伪造对端ACK或End/Fork decision。

### 3.1 View 读取（IF04）

```c
fly_session_result_v2 fly_session_view_read_v2(
    const fly_session_view_v2_t* view, fly_session_snapshot_v2* out);
fly_session_result_v2 fly_session_view_copy_candidates_v2(
    const fly_session_view_v2_t* view, uint32_t offset,
    fly_session_candidate_v2* out, uint32_t capacity, uint32_t* written);
fly_session_result_v2 fly_session_view_copy_friends_v2(
    const fly_session_view_v2_t* view, uint32_t offset,
    fly_session_friend_v2* out, uint32_t capacity, uint32_t* written);
fly_session_result_v2 fly_session_view_copy_actions_v2(
    const fly_session_view_v2_t* view, uint32_t offset,
    fly_session_action_descriptor_v2* out, uint32_t capacity, uint32_t* written);
fly_session_result_v2 fly_session_view_copy_game_choices_v2(
    const fly_session_view_v2_t* view, uint32_t offset,
    fly_session_game_choice_v2* out, uint32_t capacity, uint32_t* written);
void fly_session_approval_token_retain_v2(fly_session_approval_token_v2_t* token);
void fly_session_approval_token_release_v2(fly_session_approval_token_v2_t* token);
void fly_session_view_release_v2(fly_session_view_v2_t* view);
```

同一 view 的分页不跨 revision；到末尾 written=0，offset 超过 total 为 INVALID_ARGUMENT。每项输出都含 size/version；字符串使用显式 byte_count 的定长 UTF-8 数组（显示名称最多 128 bytes、reason/text key 最多 64 bytes），超长输入拒绝或按已定义产品名称规则处理，不截断半个 Unicode 字符；协议 bytes 不作为用户文本。

`candidate_v2` 字段为临时candidate handle、discovery_generation、最近观察的本机时间、RSSI分组、available actions/reason；不包含contact_id、长期key或昵称。`friend_v2` 字段为本机contact_id[16]、record_revision、display_name、identity_policy_state（ACTIVE/NEEDS_REVERIFY/BLOCKED）、可用管理动作；只有已有认证证据才可显示连接状态，不用匿名广播填“在线”。snapshot同时提供candidate_count/friend_count，读分页时固定于该view。

`copy_actions` 与 `copy_game_choices` 使用相同分页/所有权规则，snapshot 提供 action_count/game_choice_count。candidate/friend/game choice 的动作以 descriptor_id 引用同一 view 的 descriptor，位集仅供快速显示能力摘要。`game_choice` 提供公共目录签发的 ContentRef、显示所需内容/profile 状态，以及 opaque SourceChoiceRef、可选进度的显示摘要/不可选原因；元数据来自 Content 的 catalog/progress 读取，再由公共层核验对象及来源。UI 只返回用户选中的 choice，不构造 SourceProgressRef；PROPOSE_GAME 消费时公共层核对最新 catalog/progress revision，按原规则冻结/规范化完整 SourceProgressRef，再形成 pending config。读取列表不移交 writer、不创建游戏、不默认替用户选择另一份进度。

共享BufferHandle提供`buffer_size_v2(handle,out_size)`、`buffer_read_v2(handle,offset,WriteBytes,out_written)`、`buffer_retain_v2(handle)`、`buffer_release_v2(handle)`；read最多复制请求容量/剩余长度的较小值，offset超界拒绝。UI只可读获准显示的QR payload等public buffer，不得取得secret handle。view拥有其引用的buffer；若需要超过view寿命另行持有，调用者先retain。inbox对应`inbox_retain_v2/inbox_release_v2`，release最后引用前无在途deliver；provider不能保存一个未retain的裸engine指针。

| snapshot 分组 | 字段及显示门禁 |
|---|---|
| 顶层 | view_revision、engine_state、link_state、game_state、Scope、action_count/game_choice_count、allowed action 位集摘要、当前 primary reason；完整可执行动作读取 ActionDescriptor，没有一个可写 connected 布尔值 |
| invite | generation、route、request_id、remaining_ms、code[6]、QR payload 的只读 handle；仅房主当前有效 N01 可见；到期先作废再发布 0 |
| pairing | anonymous request token、route、双方 SAS confirmed、sas[6]、当前阶段；SAS 只在该 route 要求且 transcript 合法时可见 |
| peer | verification state、已验证身份短码、保存好友状态与本机昵称；anonymous 候选只暴露临时 handle/RSSI 分组，无长期指纹 |
| game | canonical content ID/身份 hash、profile status、authority 候选/不可选原因、seat 映射、mode 只读结果、config_revision/hash/短码、双方 confirmation |
| content | offer_id、两端拥有状态、send/receive/import 三项许可、declared/received/verified bytes、transfer/check/import state |
| local | foreground/surface/audio/permissions/path readiness、本机 mute；本机 mute 不进入 config hash，不清双方确认 |
| recovery | main state、FROZEN/UNCERTAIN 等 flags、committed/verified/replayable/opaque evidence、可接管/继续单人/保存的原因 |
| stages | 权限/发现/认证/Wi-Fi/QUIC/版本/codec 各自 NOT_STARTED/IN_PROGRESS/PASSED/FAILED、首个 failure 与 retry_class；不会把后续未开始阶段标红 |

所有动作的精确绑定来自 descriptor 的 token 登记项，而非要求 UI 从显示字段重建：N05 绑定 request/context/commitment/generation；N06 绑定 transcript/route/generation；N11 绑定 offer、内容身份/长度、send/receive/import 各自 consent revision及导入时 verified artifact hash；N12 绑定 contact/record revision或本机 identity revision。配置确认绑定 full config hash/revision；恢复选择绑定当前 source evidence、decision/lease及 failure context；取消绑定确切 invitation/request/scan/operation/transfer；无目标时的 create/discovery 绑定当前引擎能力与流程 generation。内部 hash 可保留在 opaque token 后，匿名页面不会因此得到长期 key、稳定指纹或 secret。

token 在公共层发布该动作时创建；新 request/generation、相关 config/content/身份/lease/证据变化、到期、取消、终结以及前台交互授权丢失时撤销受影响 token。应用进后台撤销尚未提交的交互批准 token，恢复时先读取真实 session，重新投影；它不凭空取消已 durable 的业务批准或复活已终结请求。已入队 action 消费前再次判定；成功消费的批准不可换 request_id 再消费，相同 request_id 重试只返回原结果。读取旧 view 或 retain 不保活 token。倒计时、metrics、本机 mute、布局或无关 view_revision 更新不撤销绑定未变的 token。首次签发 token 不是新业务授权步骤，不增加已批准 UX 的确认次数。

## 4. UI action 合同（IF05）

下表是 action_kind 的完整首版业务集合。第二列列出**公共层绑定上下文 / 用户实际选择**，不要求 UI 在 submit 中重传这些 hash、revision 或证明；全部 submit 采用 §3 的 token + typed choice。token 必须来自同一引擎公开 view，拷贝/改写 descriptor 字段不能生成有效 token 或改变登记的动作；公共层不把 UI 自报“已确认”当协议事实。仅无副作用的本机导航/输入验证留在 product policy；返回或切换入口若要取消/撤销，须提交其当前取消 descriptor，保留已认证 link 的导航不因此断连。

| action_kind | token 绑定 / 用户选择及前置条件 | 成功后的事实或结果 |
|---|---|---|
| SET_NEARBY_VISIBLE | visible:u32、user_gesture_id；来自进入/离开附近流程 | 更新发现/权限生命周期；不改变已存在连接，不自动创建邀请 |
| START_DISCOVERY / STOP_DISCOVERY | 当前 LINK scope/用户动作 | 有界扫描开始/结束；保留匿名规则 |
| CREATE_INVITE / REGENERATE_INVITE | current generation（首次为 0） | 新 generation 的码/QR；刷新先使旧 generation 和许可失效 |
| CANCEL_INVITE / CANCEL_JOIN | exact invitation/request token | 作废本请求，释放其资源；不等于断开另一个已认证 link |
| JOIN_CODE | normalized code[6] | 开始有限查询，后续 N04/N05；匹配不等于认证 |
| BEGIN_SCAN / END_SCAN | camera user_gesture_id / scan token | 请求/关闭扫码资源；不默许发送邀请或身份 |
| SUBMIT_SCANNED_INVITE | scan token、原始 decoded text bytes | 公共格式/有效性/绑定验证，再请求加入；平台解码成功不等于签名合法 |
| JOIN_CANDIDATE / JOIN_FRIEND | 当前 candidate handle / 本机 contact_id | 进入规定匿名/保存好友握手；好友选择不把广播关联为已验证身份 |
| ACCEPT_REQUEST / REJECT_REQUEST | request token、pair_context hash、generation | 只授权 exact request；接受后继续 route 身份验证 |
| CONFIRM_SAS / REJECT_SAS | exact transcript hash、generation | 本端确认或终结；单端确认不建网，QR 不显示该动作 |
| CANCEL_CONNECT | current operation/scope | 取消当前尚未建立连接的尝试；迟到成功无效 |
| DISCONNECT_LINK | link ID、expected revision | 无游戏可关闭 link；有游戏转共同结束/恢复裁决，不能静默丢存档 |
| PROPOSE_GAME | token 绑定 catalog/profile/progress revision；选择公开 game choice 的 ContentRef/SourceChoiceRef，公共层冻结 SourceProgressRef | 创建新的 pending game config；要求 link 已建立、双人资格 SUPPORTED |
| CHANGE_AUTHORITY / CHANGE_SEATS | pending config hash、候选 key ID / 完整 seat 映射 | 合法变更形成新 config hash，两端旧确认清零；listener/owner 不跟随改写 |
| CONFIRM_GAME_CONFIG | full config hash、config_revision | 记录本端确认；两端一致且门禁就绪才 SYNCING，不使用短码判断相等 |
| RETURN_TO_LOBBY | current game scope | 准备期取消本局确认/提议；运行期走共同暂停/结束，不直接卸载 ROM；父连接保留 |
| SET_LOCAL_MUTE | muted:u32 | 仅本机增益改变；不发对端静音事件、不换 config |
| OFFER_CONTENT / APPROVE_SEND / APPROVE_RECEIVE | OFFER 绑定当前内容/接收方向，消费后公共层分配 offer ID；后两者绑定 offer ID、exact 内容身份/声明长度、对应 consent revision，点击无额外 payload | 只有双向传输许可均匹配才打开 ROM 数据流 |
| CANCEL_CONTENT / APPROVE_IMPORT | offer ID / verified artifact hash 与 import consent revision | 取消仅作用本 transfer；校验完成且接收端确认导入后才进入目录，不自动补造许可 |
| PAUSE_GAME / RESUME_GAME | current config hash、revision | 提交共享屏障/恢复意图；不直接把本端 RUNNING 改 PAUSED |
| TAKE_OVER / CONTINUE_SINGLE | recovery choice token、精确 source evidence hash | 原事务允许且用户明确选择后形成新 branch/writer，不自动续跑 |
| SAVE_AND_END | recovery choice 或当前游戏 token、save policy | 按可用证据落 user save 或未决 recovery package，不能假装未决进度已解决 |
| RETRY_FAILURE | failure token、当前 allowed retry action ID | 公共 retry_class 判定同 attempt 重试或创建新邀请；AUTH_FAILED 不复活旧请求 |
| RENAME_FRIEND / DELETE_FRIEND / BLOCK_FRIEND | 本机 contact_id、expected record revision、规范昵称（仅 rename） | SecureStore 完成后更新 UI；不改对端名字，不继承新设备身份 |
| RESET_LOCAL_IDENTITY | 当前本机身份 revision、显式确认 token | 终结关联授权并按策略换本机身份；原记录/活动事务不可冒用新 key |

不提供 SET_MODE、SET_CONNECTED、SET_CANONICAL_INPUT、FORCE_START、直接导入未校验 ROM 或直接修改恢复水位的 public action。

## 5. 公共 product policy：与 UX 逐页对齐（IF06）

产品投影继续归 shared product 模块，沿现有大厅 policy 扩展，不让 SessionEngine 持有分类/搜索/收藏偏好。设计方法：

| 方法 | 输入 → 输出 |
|---|---|
| `validate_join_code(draft, submitted)` | UTF-8 草稿/是否提交 → normalized[6]、can_submit、field_error_key；未完成输入不逐键显示错误，不调用网络 |
| `project_nearby(view, local_navigation)` | immutable view/本机页面意图 → screen_id、text keys/参数、公开 descriptor_id/typed choice 控件、disabled reasons、stage rows；前进由真实状态约束，只筛选/排列已有 descriptor，不签发或改写 token |
| `project_lobby(view, browse_state, catalog_view)` | 连接视图/现有分类查询偏好/目录 → top-right label/target、原 stable results/selection、独立 multiplayerOnly、启动动作/原因 |
| `layout_nearby(screen_model, layout_input)` | 公共页面模型/逻辑宽高、已消费一次的安全区、字体倍率、keyboard occlusion → 内容/底栏/命中区域、滚动与重排策略 |

layout_input 不含“Android 风格”开关。联机页可用宽度 >580 时左 224、间距18；≤580 纵排；大厅保持 30%/70% 与两行横向网格；底部主按钮最小160且不越过可用宽度。命中区域至少48，字形栅格差异允许≤2逻辑单位，布局不得复制一套平台自定义数值。

### 5.1 Screen → action → 状态 → 测试

| Screen | 首屏与点击合同 | 驱动状态 / 后端输出 | 测试 |
|---|---|---|---|
| N00 nearby | 左侧创建/输入码/扫码依次可见；右侧附近/好友 | CREATE_INVITE；打开本机 N02；BEGIN_SCAN；无好友默认附近，有好友默认好友；不持久化 tab | UX04、UX15 |
| N01 invite | 码、真实 QR、剩余时间、刷新/取消 | invite generation；REGENERATE_INVITE/CANCEL_INVITE 同时撤销旧码和 QR | UX05、UX16 |
| N02 join-code | 草稿留 UI；完整合法时可请求；改用扫码 | validate_join_code；JOIN_CODE；BEGIN_SCAN；waiting 时不重复提交 | UX05、PAIR01 |
| N03 scan | 相机、改用输入码、取消 | BEGIN_SCAN/END_SCAN/SUBMIT_SCANNED_INVITE；QR 格式校验归公共层 | UX08、UX10 |
| N04 wait-host | 已请求但身份匿名；取消可达 | pending request；CANCEL_JOIN；未批准不暴露 nickname/key | UX06 |
| N05 host-request | 匿名请求与 route；接受/拒绝 | ACCEPT_REQUEST/REJECT_REQUEST 精确绑定 context/commitment | UX06、PAIR03 |
| N06 sas | 双方身份校验码与确认位；一致/取消 | CONFIRM_SAS/REJECT_SAS；QR route 不进入本页 | UX07、UX08 |
| N07 connecting | 当前阶段、路径、详情和取消 | stages、selected plan、CANCEL_CONNECT；系统完成不直接设 connected | UX09、UX10 |
| N08 connected | verified peer/身份短码；去大厅/断开 | CONNECTED_LOBBY；去大厅是导航、不重新配对；DISCONNECT_LINK | UX09、UX13 |
| G00 game-center | 保留四分类/搜索/详情/网格；独立双人开关 | project_lobby；未连单人启动原路径，已连 PROPOSE_GAME，非 SUPPORTED 显示原因 | UX01–UX03、UX13 |
| N09 config | 文件、主机、P1/P2、本机声音、短码、双方确认固定可见；详情可展开 | CHANGE_AUTHORITY/CHANGE_SEATS/SET_LOCAL_MUTE/CONFIRM_GAME_CONFIG/RETURN_TO_LOBBY；mode 只读 | UX11、UX13、GAME02 |
| N10 failure | 首失败阶段/原因/真实可执行动作 | ErrorInfo/retry_class；RETRY_FAILURE 或新邀请/返回；无自动重新授权 | UX10、UX14 |
| N11 content | 拥有状态、发送/接收许可、进度、校验及导入许可 | OFFER_CONTENT/APPROVE_SEND/APPROVE_RECEIVE/CANCEL_CONTENT/APPROVE_IMPORT | UX12、CONTENT01 |
| N12 friends | 保留设置内入口及联机快捷入口 | rename/delete/block/reset；policy 必须在 future auth 查询 exact key | UX15 |

游戏中正常状态仍放现有暂停抽屉；冻结/断线才用不可忽略顶部条，接管/继续单人/保存结束来自 recovery action projection。不新增永久 HUD。连接阶段的“版本”检查是 link/wire 兼容性；游戏内容、确定性和 codec 门禁属于 N09 准备，未选游戏时 codec=NOT_STARTED，不把它当空大厅认证失败。

### 5.2 文案与确认的边界

- `附近联机`：无认证连接或请求/验证/建网中，点击回当前步骤；`双人联机中`：双方 link 门禁成功；`联机中断`：曾建立的 link 中断。扫码、accept、单端 SAS 和 socket open 均不提升入口状态。
- `配对码` 只用于 N01/N02 locator；`身份校验码` 只用于 N06 SAS；`配置短码` 来自完整 config fingerprint 的显示映射，三个概念不互换。
- `multiplayerOnly` 本机持久化且默认 false；连接/断开/分类变化均不重置。SUPPORTED/UNSUPPORTED/UNKNOWN 分开；UNKNOWN 文案是“支持情况待确认”，开启筛选时不入结果。
- 连接中仍能关闭过滤浏览单人游戏，启动处显示不可用原因，不断开、不直接单人开跑。空结果留原分类，关闭筛选只改开关。
- 配置确认只绑定共享配置影响项；本机声音、详情展开、布局、分类和查询不清双方确认。authority/seat/content/profile/capability plan 改变要清两端确认。
- “发送方许可”“接收方许可”“接收端确认导入”三个状态独立；本接口修正总设计中可能被误读成“校验完就自动导入”的表述。已确认导入后由公共流程自动完成目录更新，不再重复弹另一轮确认。

## 6. 注入 ports 的方法与输出（IF07–IF14）

### 6.1 所有 port 的公共模式（IF07）

异步方法记为 `start(OpToken, Request) → ACCEPTED | error`；完成通过 `deliver(inbox, Event)`，Event 带同一 token。ACCEPTED 才允许接管请求中的 buffer/resource 引用，并承诺一个 terminal `DONE/FAILED/CANCELLED`；同步 error 不得稍后再发完成。流/扫描等长订阅的非终结事件用 event_sequence:u64 递增；END 是该订阅唯一 terminal。

`cancel(OpToken)` 是幂等取消请求，完成和取消竞态由唯一 terminal 结果裁决；cancel 不承诺“OS 从未产生效果”，结果必须带已创建资源 handle 供清理。port 即使在 start 内投递事件也只能入队，不能同步执行 reducer 或假设它已处理。相同 terminal bytes 重复返回 DUPLICATE，冲突结果为 CONTRACT_VIOLATION；旧 generation 为 STALE，不使新连接失败。

可靠 stream/存储/terminal completion 在接受操作时预约队列 credit；无 credit 不开始下一个读取/操作。DATAGRAM/视频在接收预算满时可丢弃并报告 counters，不可伪造完成。EngineInbox 对完成保留容量，避免普通事件填满导致无法取消/释放。音频播放回调通过预分配 sink ring 和原子播放游标交互，不逐样本投递 session event。

各 port 的 provider_id、contract_version、能力集合在装配时固定；capability 是“系统可以执行何种原语”，最终 certified pair plan、游戏模式和启动资格由公共引擎决定。测试 fake 只能在测试目标装配，release 构建禁止 runtime 参数切到 fake。

### 6.2 时钟、executor、权限、扫码和平台事实（IF08）

| 方法 | Request → Result / 约束 |
|---|---|
| Clock.read_continuous | 同步 → `{ns:u64, boot_generation:Hash256, suspend_inclusive:u32}`；不能用普通 uptime 代替，generation 改变先使原恢复资格失效 |
| Executor.post | `{worker=SESSION/SIMULATION/IO, task_handle}` → 执行一次公共 thunk；同 worker FIFO，不并发重入；handle 活到执行/取消 terminal |
| Executor.arm_timer / cancel_timer | `{deadline_ns, boot_generation, timer_id}` → TIMER_FIRED/CANCELLED；不负责计算 deadline，重复 fire 由 token 幂等 |
| PlatformState.watch / stop | → foreground/surface/audio route/network/resource readiness 的状态 revision；只报告事实，不决定 PAUSED/STREAM |
| Permission.query / request | `{permission, user_gesture_id, budget_token}` → GRANTED/DENIED/RESTRICTED；后台不自发弹窗，Wi-Fi 一次预算由 shared 分配 |
| Camera.start_scan / stop_scan | `{scan_token, user_gesture_id, preview_surface_handle}` → decoded UTF-8 text/ERROR/END；仅处理相机识别，不验邀请签名，不发网络 |

Clock、Executor、PlatformState 是引擎必需；Permission 相关功能按真实系统需要注入；Camera 可缺失，缺失仅禁扫码并保留输入码。UI 不用临时 boolean 冒充这些事实。

### 6.3 BLE 与 bearer（IF09）

| 方法 | Request → Result / 约束 |
|---|---|
| Discovery.scan / advertise / stop | 固定 service UUID、前台租约/截止 → 候选临时 handle/RSSI、实际开启/结束；广播不放昵称/locator/key |
| Discovery.connect / disconnect | candidate handle、GATT generation → connection handle、physical role、MTU；协议 initiator 不由 physical role 推断 |
| Discovery.write / indicate | connection/characteristic、公共已分片 buffer → OS accepted/confirmed、失败；MTU变化独立事件，platform 不改 logical bytes |
| Discovery.subscribe | characteristic → 收到的原始单次 bytes、订阅结束；公共层重组/ACK和角色校验 |
| Bearer.probe | 本机 OS/path/权限事实 → 可履行的 adapter plan capabilities 与原因；不能自行颁发产品认证 |
| Bearer.create / join | exact selected plan、系统确认预算、secret credential handle → bearer/path resource；plan hash 不可变，非指定 role 不调用 |
| Bearer.resolve_endpoint | 已创建/声明路径、listener token → 实际接口 endpoint 或 Bonjour service 路由事实；遵守原两种 endpoint 时序 |
| Bearer.release | owned resource handle → 已释放；不能关闭非本次创建的系统网络或用户连接 |

BLE 只做发现/引导；平台端不能直接由“GATT connected”启动 Wi-Fi。credential handle 只在认证/双向批准/plan locked 后交给指定 adapter。

### 6.4 QUIC（IF10）

| 方法 | Request → Result / 约束 |
|---|---|
| Quic.listen / connect | path handle、endpoint、ALPN、TLS material handle、exact pin、禁 PSK/ticket/0RTT 策略 → connection handle 或失败 |
| Quic.inspect_handshake | current connection → TLS版本/协商 ALPN、实际验证的 DER-SPKI hash、full-handshake/pin-verifier 调用证据、DATAGRAM 支持；不是 peer 业务身份凭证 |
| Quic.exporter | current connection、exact label/context、output_length=32 → secret buffer handle；无日志，不能跨 connection 缓存 |
| Quic.open_uni / open_bidi | opener、stream用途 hint → stream handle；公共层写 stream-kind/FNR1/FNB1 和 app bytes |
| Quic.write / finish / reset | stream handle、BufferHandle / error code → 写入接管/FIN提交/reset完成；对端 FIN/RESET 独立事件 |
| Quic.grant_read_credit | stream/connection、byte/slot额度 → STREAM_OPEN/DATA/FIN/RESET；DATA 允许任意分割，不要求一包一消息 |
| Quic.send_datagram | connection、immutable buffer、deadline → accepted/dropped/backpressure；过期不发送，不许悄悄变可靠 stream |
| Quic.payload_budget / stats | current connection → **应用可发送最大 bytes**、排队 bytes、loss/RTT；不能拿路径 MTU 直接填此值 |
| Quic.close | connection、应用关闭原因 → terminal；未释放 buffers 先结算引用 |

系统 pin/TLS 配置检查由可信 provider 执行，但公共层仍完成 exporter/ChannelBind、身份方向、应用序列、scope/fencing。不能仅凭 inspect_handshake 的布尔位创建 VerifiedPairEvidence。

Quic.listen/connect 的 TLS material handle 由 §6.5 的 TlsMaterial.create/restore 唯一产生；包含获准的本机 TLS key 引用、exact certificate/DER-SPKI 及签名回调，不从 UI 接受任意证书或私钥。公共层在配对 contribution 中绑定该 SPKI hash 后锁定 material，Quic 使用同一 handle 并 retain 到关闭完成；不能由 Quic 自生成另一把 key/默认自签证书覆盖已认证 pin。SAME_PATH 必须恢复并使用原 exact TLS key/SPKI pin，重新执行 full TLS（仍禁 PSK/ticket/0RTT）；需要新 SPKI 时按原认证的 NEW_BEARER/reconnect contribution/proof 流程批准，无法完成则从新邀请重新认证，不能在 SAME_PATH 中静默换 pin。

### 6.5 key、crypto、secure store、object store 和 content（IF11）

| 方法 | Request → Result / 约束 |
|---|---|
| Key.generate | purpose=DEVICE_IDENTITY/SESSION_SIGNING/PAIR_ECDH/TLS、P-256、installation/link/attempt 绑定、存储与用途策略 → 非导出 key handle；需跨进程存活者另返回 durable KeyRef、public hash与持久结果，不隐式覆盖同 scope 原 key |
| Key.open / restore | exact durable KeyRef、expected purpose/scope/public hash → 原 key handle 或明确的 LOCKED/UNAVAILABLE/NOT_FOUND/REVOKED；绝不 open 失败后自动 generate |
| Key.public_key | handle、指定 X9.63 或 DER-SPKI 编码 → public bytes；shared 严格检查 P-256 点、canonical 编码及 hash，不能将平台宽松导入结果当验证 |
| Key.prehashed_sign | DEVICE_IDENTITY 或 SESSION_SIGNING handle、purpose/domain、**已由 shared 计算的32-byte SHA-256 digest** → canonical low-S 64-byte r\|s；provider 不再 hash 一次。若 OS 返回 DER，provider 严格转换，shared 再校验；TLS私钥签名仅经锁定 material 的 TLS签名回调 |
| Key.key_agree | PAIR_ECDH handle、shared 已检查的 peer P-256 point、exact attempt → secret handle；禁止拿 identity/session-signing/TLS key做ECDH，输出不进UI/日志 |
| Key.release / destroy | release 只释放进程引用；destroy 需公共授权的撤销/GC token与expected KeyRef revision，持久删除私钥并返回terminal；在途调用先撤销/排空，公开 verifier/binding 对象仍按引用图保留 |
| Key.restore_wrapped / import_public | 前者只给明确允许的非 identity purpose，接收 provider 自己签发、绑定本安装/purpose/scope/public hash 的 sealed恢复引用 → 原key handle；后者只生成验证用public handle，不能产生私钥能力。无任意裸私钥导入/导出API |
| TlsMaterial.create / restore / release | TLS key handle、public certificate policy / durable material ref+expected SPKI hash → 可供Quic使用的material handle、exact certificate bytes/DER-SPKI/hash、durable material ref；create不另生key，restore不补造新证书/key；release只释放引用，持久销毁由Key.destroy及引用GC协调 |
| Crypto.random / hkdf / aead_seal / aead_open / verify_prehashed | 明确算法、purpose/secret handle、exact preimage/AAD/nonce或已哈希digest → bytes/secret handle/verified result；算法与拼接归 shared，verify_prehashed也不重复hash；private ECDH只走Key.key_agree |
| SecureStore.read / compare_replace / remove | namespace、record key、expected revision、secret buffer → revision 或冲突；durable 成功才更新好友/拒绝策略可见状态 |
| ObjectStore.put_immutable | object kind、expected content hash、read-only buffer → durable object ref；相同 hash不同 bytes拒绝；成功必须含 flush/原子发布结果 |
| ObjectStore.read_root | 由公共层指定的top-level root key → 原子一致的{revision、immutable root ref/hash}；启动、进程恢复和CAS冲突后均由此取得当前publish pointer，再验证不可变对象图。NOT_FOUND仅表示确无记录，损坏/IO失败单独报告；不可从enumerate、缓存或最大文件时间猜当前root，不允许把已知有历史的缺失root当首次安装 |
| ObjectStore.read_range / stat | 已批准 object ref、offset、length → exact bytes/实际长度；小于请求的非 EOF 读取为错误，不能填零补齐 |
| ObjectStore.replace_manifest | top-level root key、expected root revision/hash、WriterGuard（无writer的link操作相应轴为0）、new immutable root → 新 durable revision；在同一原子CAS核验原root及相关lease的term/generation，切换link_root_hash与source/target lease/child/ledger整组引用；失配STALE，不允许先更新cursor或另写child发布指针 |
| ObjectStore.enumerate / remove_unreferenced | namespace/revision cursor / shared 计算的删除集合与 manifest guard → bounded page/删除结果；adapter 不自行判断 WAL 已过期 |
| Content.open_read / read_range / close | ContentRef/offset/length → 授权只读 bytes；hash由 shared 核对，不因路径相同视为同 ROM |
| Content.begin_staging / write_range / discard | offer ID、声明上限/offset/bytes → transfer staging；只清理本 offer 拥有的 staging |
| Content.publish_import | 已校验 artifact hash、接收端 import consent token → 新 catalog ContentRef 或错误；写目录失败不显示导入成功 |
| Content.watch_catalog / read_progress_choices / stop | 已批准 namespace、revision cursor / ContentRef → bounded catalog或进度元数据、read-only SourceChoiceRef/对象引用、revision/变化事件；不执行source writer handoff，不接受UI自报已验证SourceProgressRef；shared检查对象闭包后发布可选项 |

身份/安全存储和公共对象存储是不同依赖，不能假设跨两个数据库有原子事务。先准备且持久保存被 manifest 引用的 key/material，再原子切换 manifest；失败保留旧 root，孤立未引用材料由共享 GC 回收。生产恢复材料必须走公共允许的 namespace，不能让 UI 指定任意路径。

这里的 KeyPort 细化总设计 IdentityKeyPort/CryptoPort 的私钥接口；平台可按 purpose 拆子 provider，但需同一合同及严格隔离：

| purpose | 创建、恢复与销毁边界 |
|---|---|
| DEVICE_IDENTITY | 首次安装通过原安全存储/installation guard门禁后才generate；正常启动仅open原KeyRef。不可导出/裸导入，永久丢失按原换钥规则成为新身份，明确重置先终结相关授权；临时锁定/设备后台不生成新身份 |
| SESSION_SIGNING | 当前PairTranscript后为父session_id独立generate；发送LINK_HELLO前持久保存KeyRef、exact SessionSigningKeyBinding bytes/hash及其引用。恢复只open同一key并重发同bytes；child游戏结束不销毁。父link终结才撤销签名并按原规则销毁私钥；缺原key/binding不能重签同session或回退长期identity签高频消息 |
| PAIR_ECDH | 每attempt独立generate，仅key_agree；不与identity/session/TLS共用。未完成配对如果没有原规范允许且完整的sealed恢复材料，进程重启终结该attempt而不复用nonce/contribution；完成/取消后按仍有效的恢复引用释放或destroy |
| TLS | 每个待认证TLS绑定独立generate并创建material，跨进程需保留原规范允许的ThisDeviceOnly/nonbackup KeyRef/material。SAME_PATH open原key/material并验证exact SPKI；新key/pin仅在原重新认证授权后切换，旧引用无使用者且不再需恢复时才destroy |

`KeyRef` 是 provider_id、purpose、installation_generation、scope、opaque record ID、record revision和expected public hash组成的不可迁移持久引用，不是私钥bytes；只存在安全恢复记录，UI不能读取/指定。硬件key用设备原生引用；如该purpose允许软件provider，其sealed材料必须由本机不可导出保护钥封装并遵守相同非备份/用途限制；不允许为硬件identity悄悄采用软件fallback。TlsMaterial由公共策略指定证书字段/算法，provider以指定TLS key产生并持久保存exact certificate（及可恢复引用），先交shared验证SPKI等于Key.public_key，再用于配对与Quic；不向公共UI或网络导出私钥，证书公钥只在原协议批准阶段释放。

Key/secure provider暂时锁定、后台不可签或系统不可用时，未建立连接显示明确原因，活动事务保持FROZEN/未决状态并等待合法恢复或结束；不宣布身份丢失、不重生key、不清恢复root、不延长协议deadline。NOT_FOUND/永久失效另走原明确失效规则；被引用的session key丢失不能修造同session签名，旧公开验证材料仍保留。所有key操作结果经OpToken检查后才可发布引用；stale generate/material完成仅登记孤立对象待回收，不能覆盖当前key。API13–API15/PORT08验证action闭环、writer guard及key purpose/恢复边界，具体编号以配套测试设计为准。

### 6.6 Runtime（IF12）

默认 RuntimePort 包装公共 `fly_runtime`，不把模拟算法搬到平台。除 capability/read-only metadata 外，以下方法仅 simulation worker 串行调用；active 和 scratch 用不同 handle。所有active mutation显式接收WriterGuard并在执行前核验§2的持久lease/term/generation；scratch仅接受公共事务限定的candidate token，不能自行promote或发布。read-only接口writer_generation=0。promote先验证事务与guard，撤销旧执行许可后排空旧调用，只有对应root CAS持久成功才激活新writer，失败保持冻结并沿原事务恢复。

| 方法 | Request → Result / 约束 |
|---|---|
| Runtime.create / load_content / destroy | timing、受限内存预算 / verified ContentRef → runtime handle、真实 timing/拓扑/format/profile |
| Runtime.step_exact | active handle、完整 `fly_frame_input_v1`、预分配 video/PCM output spans → `FrameOutput`；单次一步，容量先检查，失败不得留下未报告的帧推进 |
| Runtime.capture_rollback / restore_rollback | slot 0..11 / exact epoch/frame → snapshot token / 精确恢复；原发布媒体不会因此倒退 |
| Runtime.export_checkpoint / import_candidate | 查询/写受限 bytes / scratch handle+verified state/SRAM → exact checkpoint 或 transactional import result；导入失败不破坏 active |
| Runtime.read_digest / read_sram | exact cursor → canonical state/frame/PCM rolling digest、exact SRAM/hash；诊断 timestamp 不参与确定性 hash |
| Runtime.promote_candidate | expected active handle/revision、已验证 scratch、decision token → 原子替换 writer；公共引擎撤销旧 handle 的 step 许可 |
| Runtime.clear_local_input | 指定 owned ports / reason → 清理未提交本地输入latch；不得改写committed snapshot或已封口输入，canonical PORT_CLEAR在下一授权frame按公共赋帧规则记录 |

`FrameOutput` 明确含：epoch/frame/frame_sequence、state-after cursor、applied_input_sequence[4]、predicted_port_mask、RGB565 video buffer及256×240/stride、**真实** PCM sample_count/first_source_sequence/end_exclusive_sequence、timing numerator/denominator、source_valid。所有 PCM 边界在 V2 使用半开区间 `[first,end)`；从旧 V1 last 字段转换由 wrapper 显式完成，不能猜其含义。合法全零 PCM/source sequence=0 的 source_valid 仍为1；无源数据为0且count=0。旧 pull_pcm 的静音兜底不进入此接口。

### 6.7 Codec 与 sinks（IF13）

| 方法 | Request → Result / 约束 |
|---|---|
| Codec.probe | profile/dimensions/timing/color、低延迟要求 → 实际 encoder/decoder 能力及最大 buffers；公共层做模式资格判断 |
| Codec.create_encoder / create_decoder | exact media config、generation、surface/resource handle → codec handle、实际 format；配置不匹配即失败，不静默换 profile |
| Codec.encode / decode | frame/AU BufferHandle、FrameTag → ENCODED_AU/DECODED_FRAME/DROPPED/FAILED；回调保留同一 tag 和 generation，不生成 frame index |
| Codec.request_idr / flush / close | handle、generation token → 对应完成；flush 后迟到旧帧必须被公共层拒绝 |
| AudioSink.open / submit / gain / flush / close | 48k mono配置 / PublishedAudioBlock handle / 本机增益 / generation → sink状态和消费游标；静音仍消费块 |
| AudioSink.position | 同步只读 `{media_generation, next_sample_sequence, device_time, validity}`；未知实际播放位置不得伪造已播时间 |
| VideoSink.present / flush / close | decoded frame handle、FrameTag、公共选定 deadline → PRESENTED/DROPPED，含实际 completion timestamp 与能力等级 |

`FrameTag={Scope, timeline_epoch, frame_index, frame_sequence, mode_generation, media_generation, applied_input_sequence[4], pts90k}`。`PublishedAudioBlock={Scope, media_generation, media_first_sequence, sample_count, source_first_sequence, source_count, status, bytes/hash}`。二者的 bytes 在发布后 immutable；sink/codec 不能改 sequence 或 reassign generation。硬件 completion 未必是实际屏幕发光时刻，观测结果须注明 callback 能力，物理校准由测试设计规定。

### 6.8 装配和资源释放次序（IF14）

1. composition root 创建时钟/executor/系统 provider，调用 create；引擎持有引用，公共 policy 显示当前真实 readiness。
2. 原生生命周期先通过 PlatformState port 报告；引擎安排共用动作，UI 只按 view 更新。
3. 运行中不热替换 provider。切换音频路由、surface 或网络用新资源 generation；shared 重新评估，不能直接重设 engine 为 RUNNING。
4. 关闭顺序：撤销新 input/presentation → 共同终结/冻结并保留证据 → 取消 scan/bearer/QUIC/codec subscriptions → 结算 terminal/buffer 引用 → close sinks/runtime → SHUTDOWN_COMPLETE → destroy/release provider。
5. 硬 kill 不保证执行上述异步过程，因此 durable WAL/manifest 才是重启依据；不能把 shutdown 回调当唯一恢复记录。

## 7. 错误到 UX 的映射（IF15）

| reason / stage | 页面与可执行动作 | 不允许发生 |
|---|---|---|
| CODE_FORMAT / 字段 | N02 字段错误，继续输入或扫码 | 每次敲键网络查询、截断7位当6位 |
| INVITE_EXPIRED / ALREADY_CONSUMED / 发现 | N10/N02 明确到期/失效，可新邀请或改扫码 | 迟到匹配复活、将到期当认证成功 |
| INVITE_NOT_FOUND / 发现 | 未找到有效邀请，重试/检查附近；只在有证据时细分不在附近/错码 | 以远端沉默推断某已验证身份或编造精确失效原因 |
| CAMERA_DENIED / 权限 | N03 返回输入码，单人和合法 BLE 路径可用 | 把所有联机功能禁用、自动再次弹授权 |
| USER_REJECTED / SAS_MISMATCH / AUTH_FAILED / 认证 | N10 新邀请/返回，旧 attempt 终结 | 降级明文或自动重新授权 |
| WIFI_DENIED / NO_CERTIFIED_PLAN / Wi-Fi | N10 原因与预算允许的动作，返回可用 | 同一 budget 再弹系统确认、猜默认 bearer |
| TLS_PIN_MISMATCH / QUIC | 终结本连接，N10 | 用系统 CA/accept-all/旧 ticket 绕过 |
| VERSION_INCOMPATIBLE / 版本 | N10 版本原因/返回 | 缺 GAME scope 下继续解释 V2 消息 |
| CODEC_UNAVAILABLE / codec | N09/N10 不可开始及资源原因，可合法改候选主机 | 自动强制模式、空大厅被伪装成认证失败 |
| CONFIG_CHANGED / 本局 | N09 两端确认归零、新配置短码、重新确认 | 单端旧确认开局、本地静音触发此错误 |
| CONNECTION_LOST / 恢复 | 顶部中断条/重连，G00“联机中断”，证据允许的恢复动作 | 自动转单人、默认接管、误称仍可双人开始 |
| STORAGE_FAILED / 恢复/内容 | 保留已验证旧状态、报告具体可用恢复/导入重试 | 先推进 committed/保存好友/导入成功再等待写盘 |

所有 reason 对应共享文字 key 与参数 schema；三端翻译使用同一资源内容。恢复动作来自当前 failure token/allowed actions，不由平台针对 error number 自行拼按钮。

## 8. 接口完成与 UX 对齐检查（IF16）

| UX 决定 | 对应接口 / 本文件约束 |
|---|---|
| U01/U03/U04 | project_lobby/layout_nearby；browse_state 与 session 分离，稳定交集、不换四分类 |
| U02/U05 | PeerLink snapshot 与 N07/N08/G00 映射；无游戏 link READY 才切入口状态 |
| U06/U07 | N00 顺序、三动作首屏可见；UI 打开 N02 不发送任何协议请求 |
| U08/U09 | JOIN_CODE/CONFIRM_SAS/SUBMIT_SCANNED_INVITE、许可 token 与路由区别 |
| U10 | RETURN_TO_LOBBY、PROPOSE_GAME、config hash/双确认；local mute 不失效配置 |
| U11 | 相同 ABI/ports、shared product 投影、文字与几何输入，测试 UX01–UX18 |

测试文档按 IF01–IF16 验证所有公共边界，并逐项对应 C01–C18。本文不引入新的应用页面、额外强制模式确认或文件授权步骤；导入确认沿用 N11 原要求。
