> 历史归档（2026-09-21）：本文的执行顺序、首版范围和进度结论已被[当前最小可玩方案](../../../../../nearby/README.md)取代。保留供追溯，不作为新增任务或发布承诺。

# 多人联机接口、同步与三端 UX 测试设计

日期：2026-09-13。状态：**测试设计待评审；本文没有新增测试代码，也没有执行这里拟议的新测试。**

依据：[接口合同 IF01–IF16](2026-09-13-nearby-interface-design.md)、[后端总设计 B01–B16](2026-09-13-nearby-backend-repair-design.md)、[获批 UX U01–U11/C01–C18](2026-09-13-nearby-ui-parity-design.md)、[原协议规范](2026-09-04-cross-platform-nearby-multiplayer-design.md)。上轮现有共享 CTest 49/49 的结果见[探查记录](../../acceptance/2026-09-13-nearby-backend-audit.md)，不算本文新增能力的通过证据。

## 1. 层次、测试替身和独立判据

| 层 | 被测对象 | 使用真实对象 / 替身 | 可证明 / 不能证明 |
|---|---|---|---|
| L0 ABI / 依赖 | C header、导出符号、production composition root | C/C++消费者、JNI/N-API/ObjC++ bindings | 版本/内存/接线；不证明无线网络 |
| L1 公共合同 | engine/reducer、ports contract、product projection | 虚拟时钟、确定性 executor、故障 ports | 事件顺序、取消、授权、错误/UX；不证明 OS 行为 |
| L2 双引擎系统 | 两个真实公共 engine、wire、crypto、runtime、恢复存储逻辑 | 虚拟网络、崩溃文件系统；真实可分发 ROM fixture | 端到端输入与确定性、持久安全；不证明真机 codec/延迟 |
| L3 原生适配 | 实际系统 ports、公共 bridge、原生页面 | 真 OS/模拟器可用资源，测试场景驱动公共 view | 原生回调/权限/布局、资源释放；模拟器不认证离线承载和物理时延 |
| L4 实机组合 | 发布装配的整条链路 | 两台设备、实际 BLE/Wi-Fi/codec/sinks | 真实码/扫码/输入/媒体/恢复、支持矩阵与时延门禁 |

不是把 expected 设成另一台使用相同 bug 的 engine 输出。独立判据包括：手工冻结并审核的 exact wire golden；按已知目标帧离线执行的单一 runtime oracle；单独校验的 PCM 序列/字节模式；预先定义的 durable manifest 可达性与 writer 不变量；获批 UX 的 screen/text/actions/geometry fixture。hash 与预期结果不能在断言前用被测 decoder/reducer 自己生成。

L1 可模拟系统失败，但认证通过测试必须使用真实 crypto provider 和测试专用 key 完成握手，不能注入 `VerifiedPairEvidence=true` 宣称 PAIR/QUIC 通过。fake Codec 只用于队列/代次测试，真实编码能力和时延只看 L3/L4。

## 2. 公共可复现测试装置

### 2.1 Harness 合同

| 组件 | 可控制量与记录 |
|---|---|
| VirtualClock | 每端独立 boot generation、ns、offset/drift；advance/suspend/reboot；相同真实事件可映射不同本地原始时钟 |
| DeterministicExecutor | session/simulation/IO 独立 FIFO；人工 run-next、hold、cancel；同 deadline 事件按 fixture 的明确顺序投递 |
| ScriptedPorts | 每个 start/cancel 的 token、参数、buffer ownership、completion sequence；可同步 error、延迟/重复/冲突 terminal、永不完成 |
| SimulatedNetwork | STREAM 逐字节分割/粘合/背压/FIN/RESET；DATAGRAM 独立丢失/重复/乱序/延迟；禁止把 stream loss 模拟成应用 bytes 永久缺口 |
| CrashStore | 易失写、flush、immutable publish、atomic replace 各阶段独立；kill 丢失未 durable 数据，再从 durable bytes 创建新 engine，保留/变化 clock generation可选 |
| RuntimeOracle | 可分发 fixture ROM、冻结 core/timing/拓扑；按期望 ports[4]/target 离线无网络运行，生成可独立比较的 state/frame/PCM/SRAM |
| MediaRecorder | source/published/sample ranges、block bytes/hash、decode/present tags、queue depth、DROP/SILENCE/CORRECTION 原因 |
| UxRecorder | 每个已发布 view revision 的 screen、literal text/key+参数、action/reason、config hash/确认位；原生另记录逻辑几何和可访问性树 |

每例至少输出 `case_id, fixture_version, seed, source_revision, provider_ids, configuration, ordered_events, assertions, result`。失败保留最小可重放事件 trace；错误分类/状态不得包含真实设备密钥、私有 ROM 或网络凭据。

### 2.2 边界与组合策略

- deadlines 测 `T−1ns / T / T+1ns`，分别让回调在到期前执行、仅入队而到期后执行、到期后才入队。处理顺序是先检查当前 continuous clock，再消费旧队列。
- stream 每条合法 record 覆盖所有单切分位置；再做全部单字节分块及固定 seed 多切分/合并，FIN/RESET覆盖前缀/body/完整结束处。
- 输入 DATAGRAM 覆盖 0..12 帧延迟、丢失/重复/乱序，可靠 repair 有“硬上限前可达”和“无法及时到达”两类，后者必须冻结，不要求无限修复。
- 取消、完成、超时、页面退出、generation 更新的可达事件排列做有界枚举；长度最多7的 race trace 枚举所有合法排列，长序列用固定 seeds 1..200。seed 命中失败必须缩减成固定回归，不能反复 rerun 直到绿。
- 测试预算采用小值暴露边界（如 queue容量2/4），生产约定的64样本/12帧/媒体容量也运行实值边界；不把缩小配置的结论代替真实生产阈值。

## 3. API 与系统注入测试

每一行都是未来测试用例族，负例必须断言“状态/存储/网络副作用未越权”，不能只检查返回值。

| ID | 准备与刺激 | 必须断言 | 层 / 合同 |
|---|---|---|---|
| API01 | C/C++最小消费者只包含公共头；V1/V2各自size；未知version/非零reserved | 合法 ABI 可消费；坏参数明确拒绝、无副作用；V1不被重新解释 | L0 / IF01–02 |
| API02 | 缺 Clock/Executor/认证 provider；缺 Camera；缺 encoder | 必需缺失 create不可假就绪；Camera只禁扫码；encoder只影响相应authority能力；无假 store fallback | L0/L1 / IF07–14 |
| API03 | action入队后更换config，再消费旧CONFIRM；同request_id重复；另测仅倒计时/view刷新 | 旧hash不确认；相同请求只生效一次、不同payload拒绝；无关view刷新不误拒当前config的确认 | L1 / IF03、IF05 |
| API04 | start返回error后违规回调；ACCEPTED后重复相同terminal和不同terminal | 前者识别违约；相同重复无第二次effect；冲突被检测，不冒充新操作成功 | L1 / IF07 |
| API05 | generation g取消后创建g+1，再送g的Wi-Fi/QR/codec/storage结果 | g资源释放、返回STALE；g+1状态/确认不变化；旧成功不把入口改双人中 | L1/L2 / IF02、IF07 |
| API06 | 2槽action队列和结果槽填满；再投普通动作、terminal、shutdown | 新普通动作BACKPRESSURE且无接管；已接受结果不丢；取消/终结仍能排空 | L1 / IF03、IF07 |
| API07 | bytes提交后调用者立即改写/释放；接受/拒绝/取消每种结果 | 引擎看到原复制内容或有效retained immutable buffer；引用计数最后归零；拒绝不吞引用 | L1+ASan / IF02、IF07 |
| API08 | UI持view r；engine产生r+1并destroy；旧view分页读取再release | r内部字段/分页一致且可读；不引用已释放engine；全分页不混revision | L1+ASan / IF04 |
| API09 | ports在start内直接投递；同时任意线程回调 | 仅入队，无reducer重入；单session writer/单simulation writer，顺序可重放 | L1+TSan / IF07–08 |
| API10 | begin_shutdown后持续送callback；某port永不terminal | 不再接受新业务；不能提前destroy，BUSY可见；closed inbox无UAF；完成后全资源可释放 | L1/L3 / IF03、IF14 |
| API11 | source/媒体sequence从0开始、uint64上界、大小乘法溢出 | 0的合法源不被当静音；溢出在执行/分配前拒绝，无回绕或半帧推进 | L1/L2 / IF02、IF12–13 |
| API12 | release产品链接/启动配置、测试provider参数注入尝试 | 真实公共session与provider已链接；fake不可在release选择；平台无设置CONNECTED/RUNNING的旁路 | L0/L3 / IF01、IF16 |
| API13 | 独立C消费者仅经acquire_view/copy_actions/copy_game_choices获取ActionDescriptor、ApprovalToken及typed choice，依次走匿名接受、PROPOSE_GAME、配置确认、文件许可/导入及恢复选择→submit；无内部头/测试私有入口 | 每个获批页面动作均可用公开ABI构造；enabled/reason、token及choice来自同一view，UI不构造SourceProgressRef或绑定hash；LOADING/未验证来源无可用批准；成功副作用由真实engine执行 | L0/L2 / IF03–06 |
| API14 | 保持engine/connection/game scope相同，仅推进authority_term或本机durable writer_generation；放行旧step/import/promote/save及迟到结果；另在排队后、effect前切换lease | 旧fence不能写active runtime/manifest或呈现；执行副作用前再次比较durable lease guard，失败无半步；新合法writer仍可执行，不能只靠入队时检查 | L1/L2 / IF02、IF11–12 |
| API15 | retain公开ApprovalToken后release原view，再更换request/config/offer/recovery source或向新engine提交；同request_id重发相同/不同token或choice；另仅无关view刷新 | retained handle无UAF但不延长授权；只批准token绑定的exact对象/权限，过期/撤销/替换/跨scope无越权；同请求幂等、不同输入拒绝；无关view刷新不误拒合法token；读取token本身不是批准 | L1/L2+ASan / IF03–05 |
| PORT01 | 两端不同raw clock；暂停/后台10秒；clock generation变化 | deadline包含挂起，不能用UI计时延长；新boot无原session resume授权 | L1/L3 / IF08 |
| PORT02 | GATT MTU23、分片背压、换MTU、central与protocol role反向 | exact logical重组一致，角色不反转；未知候选无稳定身份泄露 | L2/L3 / IF09 |
| PORT03 | 空认证plan交集、错误creator/confirmation role、拒绝系统弹窗 | 不create/join、不发credential；预算内最多一次系统确认，无循环重试 | L1/L4 / IF09 |
| PORT04 | full TLS正确/错误pin、恢复ticket、exporter context/长度错误 | 只有正确full-handshake+后续ChannelBind可用；错误者无业务流副作用 | L2真实QUIC/L4 / IF10 |
| PORT05 | ObjectStore flush/replace失败；SecureStore revision冲突；短读取 | 不推进durable水位/好友成功；旧root可重建；短读不补零；重试不会重复写writer | L1/L2/L3 / IF11 |
| PORT06 | codec请求profile与实际输出不符、flush后迟到帧 | capability失败或明确暂停；旧代次不能present，tag不重写 | L1/L3 / IF13 |
| PORT07 | 同一音频块向双sink提交，host mute/guest慢消费/route变化 | 同块hash不变；各自游标单调；mute不改对端/不清config；路由换代可丢旧回调 | L2/L3 / IF13 |
| PORT08 | 真实provider创建独立Identity/SessionSigning/TLS材料；错purpose句柄互换；材料durable前后/manifest切换前后kill；换child、原link合法恢复、link终结再新建 | 用途与canonical验证材料匹配，不能借Identity签名冒充SessionSigning或把TLS句柄当签名证据；恢复按durable引用读取，缺材料fail closed；换child保留parent会话签名身份，终结后旧私钥不再授权新link，公开验证材料按恢复引用保留；TLS各binding生命周期遵守full-handshake/pin合同 | L1/L2真实crypto/L3 / IF10–11、IF14 |

PORT08用独立审核的P-256/digest/签名正反向量验证prehashed接口，生成结果另由独立参考verifier检查原digest，不能仅让同一个provider自签自验：原digest验证通过，double-hash、非canonical/高S、错purpose和错scope拒绝；PAIR_ECDH只可key_agree，Identity/SessionSigning/TLS不能替代。分别用独立Key/SecureStore和ObjectStore故障实例，在KeyRef/SessionSigningKeyBinding/TlsMaterial已durable但parent root未CAS、CAS成功后未send处kill，不能假设跨库原子提交。重启必须open原key与exact binding/material，TLS SAME_PATH保持原SPKI/certificate，material.create不能暗中新生key；LOCKED/UNAVAILABLE仅保留root并按deadline等待，NOT_FOUND/REVOKED禁止同session自动generate。另检验旧generation的generate结果只形成可回收孤立对象，release不误destroy、公开验证材料不随私钥撤销删除。

PORT08复用ObjectStore.read_root合同到真实provider：启动、进程恢复及CAS冲突后都须原子读取同一top-level key的revision/ref/hash，再验证不可变图。并发root从A换到B时以独立store oracle断言每次返回完整A或完整B；revisionB+ref/hashA等半新半旧组合判provider合同失败。enumerate中放入更新时间更晚的孤立candidate C，不能据此选择C或跳过read_root。NOT_FOUND、损坏和IO失败分开注入；有installation/session历史但root缺失不能按首次安装建空root、重生key或复位水位，只能保留证据并报告恢复不可用。

## 4. 配对、wire、游戏与文件测试

| ID | 准备与刺激 | 必须断言 | 层 |
|---|---|---|---|
| PAIR01 | 创建60秒邀请；查询`012345`；无匹配/多context匹配/限流边界 | 前导零保留；最多8候选/16秒且不越邀请deadline；不自动选冲突身份；查询不认证 | L1/L2 |
| PAIR02 | 同一UI generation的code/QR child并发请求；房主先接受其一 | 只有一条route占用；另一route不可继续；刷新同时撤销旧两child；不复用nonce/contribution | L2 |
| PAIR03 | join提交commit但房主未accept，之后拒绝或页面退出 | 无身份reveal/credentials/bearer；匿名view；退出撤销exact permission | L2 |
| PAIR04 | 真实BLE/SAS双端握手，单端确认、码不同、双方确认 | 单端/不一致不能进入bearer；双方同transcript确认才推进；SAS不等于locator | L2/L4 |
| PAIR05 | 合法QR、签名/commitment替换、反射/重放/到期；接受丢失 | 合法也需host许可，QR不弹SAS；其他终结attempt；不降低到明文/旧邀请 | L2/L4 |
| PAIR06 | 使用总设计唯一V2 GATT注册：type27 REQUEST body32 bytes、type28 MATCH body136 bytes，完整logical分别72/176 bytes；交换outer/body version、方向、type、长度及nonce/context；注入并行草案V1 type28/29、0x214/0x215 | 独立golden校验V2 header/长度前缀/hash domain及body offsets；V1的27/28/29和V2的29拒绝，不探测兼容其他草案；错消息无匹配/认证副作用；V1 type16仅作exact logical ACK，MATCH后PAIR_CONTEXT逐byte绑定同一context | L2+golden / B04–05 |
| WIRE01 | 所有合法record切分/粘合；两stream交织；FIN半包/重复bind流 | 完整消息各消费一次；每流独立cursor；半包不执行；错误关闭受影响连接 | L2 |
| WIRE02 | 类型跨channel、unknown critical、reserved、计数/长度越界、旧scope/generation | 分配受上限约束；拒绝且无reducer/runtime/store副作用；已有golden仍一致 | L2+fuzz |
| WIRE03 | bind FIN之前早到合法Control；TLS仅connected；HELLO版本不兼容 | 有界缓存但不执行；只在完整门禁后处理；失败不显示双人中，不假执行codec检查 | L2 |
| GAME01 | 两端无ROM建立link；随后从原大厅提议支持双人的fixture | N08/G00可达且无game writer；提议才有pending config/内容门禁 | L2/L4 |
| GAME02 | authority=P1/P2、换authority/seat/content/profile；本机mute/查询改变 | 前一类两端确认失效且hash相同；后一类config/hash/peer mute不变；模式不可手动强制 | L2 |
| GAME03 | seq100按A于frame42，seq101释放于43；丢/乱序/repair | canonical ports准确应用42/43，不能折叠；与offline oracle state/PCM一致 | L2 |
| GAME04 | 相同input key同bytes重复、不同mask或target；已committed冲突 | 同bytes幂等ACK；不同字段违规冻结；committed历史不被重写 | L2 |
| GAME05 | raw=47→48、模拟差9→10、ring硬界12；guest超前4→5；追帧量子 | 阈值受控冻结，无覆盖raw/no越界restore；单量子最多3步，不因renderer/audio快而自由运行 | L2 |
| GAME06 | D=2/3/4与PAL/NTSC；120中立帧门禁通过/摘要不一致 | gate后回共同初态且不外放预演；D按公式协商，>4不可开始；失配合法选STREAM | L2/L4 |
| GAME07 | 准备期返回大厅、运行期RETURN_TO_LOBBY、换新游戏后旧输入/媒体/确认 | 准备确认清零、link/browse不变；运行先完成共同事务；新branch不接旧数据 | L2/L4 |
| CONTENT01 | 只批准send；再批准receive；完成hash校验但未批准import | 单许可不传；双许可才传；仅校验完不入目录，批准import后恰好一次导入 | L2/L3 |
| CONTENT02 | 错declared hash、>8MiB、offset越界、10秒无进展、取消 | staging隔离且清本次资源；原文件/目录不变；不可运行未验证bytes | L2 |
| CONTENT03 | 缺ROM选择STREAM且拒绝补齐；以后获准导入 | 拒绝不阻止合法STREAM；导入前replayable=NONE不可接管；导入后仍需scratch proof才可接管 | L2/L4 |

### 4.1 可复现输入 trace（GAME03 的一个 fixture）

前置：共同配置D=2，当前epoch=7、seat_revision=3，guest拥有P2，其他端口中立；seq100/101均在已批准reservation内，target通过合法beacon/base计算为42/43。mask `0x01` 使用fixture冻结的A键语义。

| 顺序 | 注入事件 | 判据 |
|---|---|---|
| 1 | 交付seq101、target43、mask0，recent副本也暂丢 | 缺seq100，不得把101当连续确认前缀；发RAW repair请求 |
| 2 | authority在允许窗口内预测到43 | 可有provisional frame，不得把缺真实输入的42/43推进confirmed/committed |
| 3 | 可靠repair交付seq100、target42、mask1；随后重复相同101 | 回滚最早受影响42；42的P2=1、43的P2=0；重复无第二次消费 |
| 4 | exact输入连续、seal与durable head完成 | ports日志、state-after43、PCM rolling digest与离线先42按下再43松开的oracle一致 |

另例将步骤3推迟到10帧阈值之后：预期先冻结，不要求仍保持RUNNING或满足正常网络时延门禁；修复只能通过合法恢复路径。不要把负载故障导致的冻结当测试随机超时。

## 5. 音视频与端到端同步测试

| ID | 准备与刺激 | 必须断言 | 层 |
|---|---|---|---|
| MEDIA01 | step_exact输出真实零PCM、source first=0；容量不足；旧pull欠载 | 真实零块source_valid=1；容量不足无半帧推进；旧fallback不进PublishedAudioBlock | L2 |
| MEDIA02 | 两帧staging内回滚；发布后深回滚 | 未发布可替换；已发布相同media seq/hash永不重写或重播；correction可见并计数 | L2 |
| MEDIA03 | 四块960-byte固定模式，逐个丢其中1块；再丢2块；parity迟到 | 单丢在deadline前逐byte恢复；双丢/迟到按对应序列记录静音；不等待无界、不重复播放 | L2 |
| MEDIA04 | payload budget恰好够DATA/FEC和少1byte；运行中预算下降 | 分别检查完整DATA和FEC长度；不足不支持/冻结STREAM；不做IP分片，不把包改可靠流 | L2/L3 |
| MEDIA05 | AU乱序/缺片/重复片/冲突片、3→4在途、256KiB边界、新generation | 完整合法才decode；冲突拒绝；过期整帧丢；旧generation不present；config+IDR才启新代次 | L2/L3 |
| MEDIA06 | encoder queue满、慢decoder、慢video sink、audio欠载/静音 | encoder最多2帧，丢旧未编码视频；control/input不被占满；audio timeline持续，UI明确降质/冻结 | L2/L4 |
| MEDIA07 | 两端clock offset差5秒、±100ppm drift、播放设备路由切换 | 不直接比较raw clock；每端media序列单调，slew不倒跳；generation flush正确 | L2/L3 |
| MEDIA08 | DUAL连续两次deep audio correction在30秒内、持续失步 | 触发规定重同步/降级，保留进度；不能加无限buffer掩盖或自动升回DUAL | L2/L4 |

MEDIA03 固定音频内容由 fixture 显式公式 `sample(block,j) = ((block*997+j*31) mod 60001)-30000` 生成，block=0..3、j=0..479，编码为S16LE；parity用独立简单参考逐byte XOR，不能用被测FEC encoder同时生成“预期”。DATA media区间固定为[0,480)、[480,960)、[960,1440)、[1440,1920)，播放游标只能前进，每次恢复后核对原块960 bytes，而非仅核对“恢复成功”标志。

## 6. 持久事务与故障矩阵

### 6.1 所有事务共同不变量

- **R-I1** 同一active multiplayer branch最多一个writer，单机source writer与active game writer不并存。
- **R-I2** committed/replayable的全部state/SRAM/tail/hash引用必须durable且可重建，不能高于其证据。
- **R-I3** pending/旧generation媒体不呈现；FROZEN/UNCERTAIN/RECOVERY_LOCKED下无未经授权的active step或save。
- **R-I4** ID/epoch/sequence/counter烧号不回退；精确重发不重做副作用；已决定方向不回滚。
- **R-I5** release/ACK丢失不清理仍被WAL/tombstone引用的包；GC不能产生不可重建active head。
- **R-I6** 分区后的不同branch不靠term大小自动合并；用户选择绑定具体source proof与精确SRAM。

### 6.2 注入点与 oracle

每种事务从手工审核的合法phase trace派生故障运行；只有能到达的phase参与。对每个phase分别：effect前kill、写入但未flush kill、flush后manifest replace前kill、replace后send前kill、send后丢ACK、重复相同ACK、同key冲突hash、terminal完成迟到，以及断链后新connection重放旧phase。重启只用durable bytes，不能复用进程内reducer。

| ID / 事务族 | 决策前期望 | 决策后期望 / 特殊断言 |
|---|---|---|
| REC01 初始开始/Source handoff | source writer与候选准备门禁一致；未允许时无game step | active source/新branch state+SRAM精确配套；不可两个writer，FINALIZED前不假运行 |
| REC02 pause/resume/seat/input-delay/resync | 冻结并按各自允许的abort/恢复路径；不越过input close/prime | target config/epoch/hash按WAL唯一收敛；暂停中断重连后仍暂停；resync重建committed F而非丢到旧C |
| REC03 DUAL→STREAM | pre_safe可读、旧branch冻结，失败不恢复已判无效DUAL | COMMIT后仅收敛STREAM；缺包RECOVERY_LOCKED；state_afterK与exact SRAM一致，旧媒体不复活 |
| REC04 reconnect/release尾链 | 原deadline不可滑动；30s边界和5s限定grace只授权合法phase | READY/BOUND不等于可step；FINALIZED/RELEASE/ACK/BEACON每个丢失点保留足够tombstone并按原链释放 |
| REC05 save/end/takeover/fork | 无合法proof不生成可用user save/接管；保持未决包 | terminal writer不可复活；新branch仅用已选source；同键重试不导出两份不同的“同一save” |
| REC06 object GC/存储损坏 | 先保持旧root，不能把缺失新candidate当成功 | active proof/pending/repair tombstone引用全部保留；恢复缺材料只按hash修复，不能混合不同cursor的state与SRAM |
| REC07 空大厅link恢复与换局 | NONE game用link专用基线，不造GENESIS游戏 | 有active或pending game不能走简化link恢复；原session级ledger/signing绑定换局不归零 |
| REC08 parent ledger跨child交接 | old child仍active+FROZEN时先修复/关闭reservation；逐phase故障覆盖ChildInputCloseV2、LedgerHandoffV2、PrestartAbortTombstoneV2及read_root/CAS | active close完成才可END；未开局取消须形成合法ABORTED_PRESTART前驱；后继完整继承ledger链及+1下界；缺close不改ENDED/新开局；有prime保留烧号，source release后不复活旧writer | L2 / 总设计§4.4 |
| REC09 reconnect混合child状态 | 先交换phase=ROUTE_ONLY的LinkResumeSummaryV2；旧TAIL_STATUS prelude完成后read_root重读并交换RECONCILE；覆盖§4.5九行并交换方向、故障注入 | 旧release/live槽未修复前不推进child/BOTH_PREPARED/普通ChannelResumeSummary；READY只绑定最终RECONCILE双摘要/结果hash；NONE不遮蔽未决child，冲突冻结/缺证据repair-blocked，不按term或local_root_revision选赢家 | L2 / 总设计§4.5 |

REC04 deadline fixture必须覆盖：last-auth-activity+29.999999999s/30s/30.000000001s；本机连续send不延时；进程kill后durable基线较旧不以启动时间重算；同clock generation可沿合法剩余预算恢复，新generation拒绝；BOTH_PREPARED后TLS重建不能挪用旧5秒grace。

REC03具体checkpoint判据：fixture冻结pre_safe=C0=FRAME(40)、新E1、K=41；candidate只在scratch执行I[41]。分别在READY前、COMMIT_DECIDED后、ACTIVATED_ACK后kill。重启后读取durable root，断言旧态仍冻结或新STREAM head严格由RecoveryPoint(E1,40)+I[41]重建，不允许用state_after40搭配SRAM41。不得通过仅比较两端mode字符串代替此断言。

REC08固定parent ledger fixture从手工审核链派生：旧child两座位各有非零reserved_through/ledger_generation/last_reservation_hash及未用尾段，NORMAL/PRIME/TERMINAL/AUTHORITY_CLEAR四种reservation均有合法子例，并分别放入PENDING/ACKED状态后补齐为OPEN。关闭body的before-ledger/OpenReservationSet hash、terminal committed cursor/root、每段已用/烧号并集以及固定Pair role双签都用独立expected核对，不能从被测新child重新读出水位当expected。对CLOSE_PREPARE/ACK/FINAL/FINAL_ACK、END与handoff的PREPARED/COMMIT_DECIDED/PEER_COMMITTED/FINALIZED/RELEASED逐一在flush、顶层CAS、send/ACK前后kill，断言只有完整release proof可参与新SessionStartAuthorizationV2；提前到达的新局消息只能有界等待补proof。

例如关闭后某座位冻结为reserved_through=100、ledger_generation=7、last_reservation_hash=R，新authority首次grant必须从101及generation8开始并继承R；另一座位的owner/seat revision与ledger也在同一次本机顶层CAS切换。失配guard时旧link_root_hash、所有source/target lease和pending_handoff_slot保持原组，只有不可达候选对象可留下；不允许独立child pointer先成功。将ledger初始化为generation0/空链只允许全新父session；后继必须继承真实值，已有非零账本被重置为零的负例拒绝。另设首handoff后从未分配grant就合法abort的正例，继承仍为零的ledger可开始后继，但必须引用ABORTED_PRESTART而非伪GENESIS；SessionStartAuthorizationV2按discriminator检查所需proof，GENESIS_LINK或无grant abort不伪造不存在的close。新prime已durable预留后abort再新开，必须按关闭/取消证明把新水位写回父root并继续烧号；清slot前缺proof保持repair-blocked。另移除close证据但保留完整END证据，断言旧ENDED对象bytes/hash不变且只能找回既有proof、不能签新close或启动新child。S=H与S≠H都在OFFLINE_RELEASE_COMMITTED之后取消新局，旧source writer始终不复活；source handoff证据不能由child输入关闭替代。

REC08补三条未开局取消trace，并在ABORT_PREPARE/ABORT_ACK/ABORT_FINAL/FINAL_ACK、双方顶层CAS前后kill或丢ACK：①handoff已RELEASED但无新grant、无source release、start未决定，双方签发PrestartAbortTombstoneV2，完整证明后各自CAS到NONE；随后从获批大厅重新选游戏，后继handoff接受ABORTED_PRESTART及最终ledger，不能要求从未开始的child提供END。②已有prime/grant但未激活，先用scope=PRESTART_ABORT的ChildInputCloseV2绑定pending manifest、SessionStartAuthorization、初始恢复点和完整授权链，以tagged prestart evidence代替不存在的运行终结cursor；PENDING/ACKED修到OPEN、关闭所有尾段后abort，最终ledger保留新增sequence/generation/last hash并被下一局继承。错用ACTIVE_END、零branch/伪committed root或省close拒绝。③source已release或start已有不可逆decision，必须先走原决定方向/release尾链，有安全未开局终结证据才可abort；证据不足保持pending/repair-blocked。已获得active许可时只能ACTIVE_END close+原END，不能拿prestart abort清slot；全部路径禁止复活源writer。

REC08另在read_root与guarded CAS之间让独立writer发布新root，并把较新的孤立candidate列入enumerate。CAS冲突后必须重新read_root获得完整当前revision/ref/hash、核验ledger/lease再重算，不能继续旧proposal或拼接半新半旧根。durable store oracle仅允许旧整组或新整组发布；启动收到NOT_FOUND但保留历史时，不能建GENESIS_LINK、空ledger或新source writer。

REC09的独立裁决fixture逐行冻结双方摘要、共同history anchor、proof图和唯一允许结果；local_root_revision故意不同，不能用同一个被测reducer生成expected：

| 双方状态/证据 | 必须出现的归并结果 |
|---|---|
| NONE/NONE且完整ledger、合法前驱terminal+close或prestart-abort/history语义相同、无pending | 旧prelude处理完后的简化link恢复可达，即使本机CAS revision不同 |
| NONE/PENDING，journal证明仅未决定提议且无source release/grant | coordinator持久ABORT/ABORT_ACK后清提议和旧确认，回空大厅；不自动重建批准 |
| NONE/PENDING，分别已有decision、source release、prime/grant | 每种均进入完整pending恢复，不签空大厅READY |
| ACTIVE/ACTIVE同child | 走原AuthorityRecoveryHead/WAL/release恢复；只有link已绑定仍FROZEN |
| ENDED或TERMINAL_REPAIR / ACTIVE或ENDING同child | 原END尾链收敛终结；缺close不开放新局，已ENDED writer不复活 |
| 上一局NONE/ENDED / 后继PENDING或ACTIVE | 验证handoff引用的旧terminal+close或ABORTED_PRESTART及slot前驱并补exact phase后，进入后继完整恢复；伪前驱或缺proof不切branch |
| 两个不同branch的未决定候选 | 只认可固定coordinator及唯一前驱，合法持久取消候选后重新选择；任何新增grant/release/decision使该简化取消不合法 |
| 同slot冲突或不同branch互斥已决定证明 | PROTOCOL_VIOLATION/FROZEN并保留双方证据；交换消息先后、较大term/root revision仍不能选赢家 |
| NONE/NONE但ledger/history不一致，及任意组合缺proof | 仅补齐共同锚点后的可验证单链；仅max(counter)不授权；无法补齐保持repair-blocked且无可开始动作 |

以上每行都交换两端、注入摘要/组件重放和CAS/ACK丢失；首轮ROUTE_ONLY仅路由证据读取，归并后的LINK_READY/ACK必须绑定phase=RECONCILE的两份已验证摘要与结果hash，替换phase/branch/attempt/channel/结果hash均拒绝。重启及prelude后均通过ObjectStore.read_root读取durable顶层root；内存child指针为空不能伪造NONE。归并中root变更使旧结果失效，必须重读和交换新摘要。本机summary连续重试和旧回调不得延长原30秒/限定grace/recovery deadline，也不得取代游戏RUN/RELEASE许可。

REC09固定旧尾链反例：peer已持久旧RELEASE/RELEASE_ACK及MARKER_PENDING tombstone并清live槽，ACK丢失，authority仍保留RELEASE_PENDING/live槽；ROUTE_ONLY同时报告由可达部分提交/恢复产生的不对称child摘要。必须先走原TAIL_STATUS/phase/marker修复，补齐旧release链并释放相应槽，再read_root和RECONCILE裁决实际child。prelude前注入看似合法的NONE/PENDING归并、handoff/start、普通ChannelResumeSummary、BOTH_PREPARED或LINK_READY，均不得产生推进副作用。仅迟到旧RELEASE_ACK或首轮摘要不等于prelude完成；在TAIL修复CAS前后kill并交换两端，重启仍重走正确阶段，不能把旧slot/hold与新摘要混用。ROUTE_ONLY→prelude→read_root→RECONCILE→归并/READY的顺序由独立事件oracle断言，deadline全程不重置。

## 7. 与获批 UX C01–C18 一一对应

这里 UXnn 与原文 Cnn 同号，不重写既有界面。不以合成“成功”按钮作为真实认证证据；L1/L3投影测试可用冻结fixture，L4必须实际操作系统与协议。

| ID / 原用例 | 操作与准备 | 精确 UI / 业务断言 | 层 |
|---|---|---|---|
| UX01 / C01 | 无link进入G00 | 右上“附近联机”；最近/收藏/全部/内置、搜索工具、左30%详情/右70%两行网格保持 | L1/L3 |
| UX02 / C02 | 4分类×2开关×空/命中/不命中查询×未连/连接中/已连/中断 | 连接事件不改category/query/multiplayerOnly/每类选择；稳定交集不重新排序 | L1/L3 |
| UX03 / C03 | BUILTIN只有单人；开双人筛选再关 | 空结果留BUILTIN，动作仅清开关；UNKNOWN文案待确认且不计结果 | L1/L3 |
| UX04 / C04 | 无选中游戏打开N00，分别点三动作 | 创建/输入配对码/扫码加入按顺序首屏可见；不先要求ROM；右tab按有无保存好友决定初值 | L1/L3/L4 |
| UX05 / C05 | `012345`、首尾空白、空/5/7位/字母/全角数字/过期 | 仅trim后6位ASCII可发送，前导0保留，未完整不逐键报错；提交中仅一请求，过期不成功 | L1/L3 |
| UX06 / C06 | N04/N05等待host，拒绝/接受 | 匿名“附近设备请求加入”及route；不出现昵称/长期指纹、不发受保护凭据；accept仅当前token | L2/L3/L4 |
| UX07 / C07 | 一端SAS确认、两端不同、两端同transcript确认 | 一端不能切双人中；不一致终结；N06文字“身份校验码”，不写“配对码” | L2/L3/L4 |
| UX08 / C08 | 合法QR/篡改/重放/过期 | 合法仍等host；QR不额外显示N06；失败原attempt不能自动重试，单人仍可用 | L2/L3/L4 |
| UX09 / C09 | socket成功/ChannelBind未完/单端ready/全部link门禁 | 前几步仍“附近联机”，完整成功才“双人联机中”、保存好友；点击进入N08；无ROM亦可达 | L2/L3/L4 |
| UX10 / C10 | camera拒绝、Wi-Fi拒绝、版本失败、codec未开始 | 相机拒绝可改输码；同budget不反复弹Wi-Fi；N10仅首失败红，后续NOT_STARTED；空大厅不伪造codec通过 | L1/L3/L4 |
| UX11 / C11 | N09某端确认后变authority/seat/content/plan；另测mute/展开详情 | 共享变更两端清确认、新相同短码；本机变化不清确认；确认绑定full hash、mode只读 | L2/L3 |
| UX12 / C12 | send-only、send+receive、verified-only、approve-import | 不传/可传/尚未导入/导入后目录可见；N11三种许可独立，状态不倒序 | L2/L3/L4 |
| UX13 / C13 | 从N09回G00再换游戏；运行中退出；单人游戏可浏览 | 保留link及category/query/filter；确认失效；运行期先共同事务；不静默断开或单人启动 | L2/L3/L4 |
| UX14 / C14 | 已连断线、后台恢复、暂停时断线 | “联机中断”与顶部条，正常游戏信息仍在暂停抽屉；不能自动单人/迁移authority/强制模式 | L2/L3/L4 |
| UX15 / C15 | rename/delete/block/reset；匿名发现再次出现 | 本地好友名字不写对端；block旧key不能新建好友绕过；UI不凭广播认人；tab不保存上次值 | L2/L3 |
| UX16 / C16 | cancel/refresh/timeout后晚到QR、SAS、Wi-Fi、codec、确认 | 不复活旧请求/link/config；旧资源释放但不取消新generation | L1/L2/L3 |
| UX17 / C17 | 所有screen/弹窗×宽320/375/550/580/640/736/900/1024 | 除原游戏网格外横向溢出0；580纵排、>580双栏；底栏不被100%按钮挤出 | L1几何/L3 |
| UX18 / C18 | 640×360/736×414/844×390×1.0/1.3/2.0字倍×安全区×键盘 | 输入/错误/主动作可达，正文可滚；无裁切/覆盖；命中≥48；三端同逻辑框差≤2 | L3原生 |

UX02使用小目录固定fixture：A/B为SUPPORTED、C为UNSUPPORTED、D为UNKNOWN；收藏顺序固定B,C,A，最近固定C,A，内置只有C；查询只命中B,D。断言例如“收藏+双人+空查询=B,A”，“ALL+双人+查询=B”，“内置+双人=空且仍内置”。ALL原热度排序单独给expected order，不按测试运行中新计算结果重排。

UX17/18以相同逻辑safe width/height进行三端比较；安全区外层只消费一次。原生测试读取accessibility树和布局rect，真实键盘遮挡由设备/模拟器产生；检查可滚至动作、action与错误同时可达。像素截图只辅助检查颜色/结构，不拿系统字体像素差当业务不一致，也不能用纯HTML136组合替代这些原生用例。

## 8. 实机矩阵与测量方法

### 8.1 组合与角色

| authority平台 | guest平台 |
|---|---|
| Android | Android、HarmonyOS、iOS |
| HarmonyOS | Android、HarmonyOS、iOS |
| iOS | Android、HarmonyOS、iOS |

九个方向分别测authority=P1/P2，共至少18个seat配置。同平台不同机型还交换物理authority；inviter默认authority之外，每个平台对至少测一次接受连接后改选另一端authority；bearer creator/listener按认证plan实际角色，不虚构iOS系统能力。

每配置涵盖：配对码与扫码、发现/SAS、同ROM DUAL、缺ROM或门禁失败STREAM、双方声音与各自静音、内容许可拒绝/接受、断线恢复与保存。所有支持项必须有至少一条已认证的无路由器离线路径；普通LAN和Android↔Windows probe只作为开发基线。

### 8.2 性能用例与统计

| ID | 场景与采样 | 通过标准 |
|---|---|---|
| PERF01 | 已认证正常近场条件，每设备配置/模式3次独立会话；每次稳定运行至少10分钟、至少1000个可辨认touch动作 | 每次分别统计guest同一本机时钟touch→对应canonical画面实际present的nearest-rank p95；DUAL≤80ms、STREAM≤150ms；每edge限1秒，任一无法关联或超时整轮失败；有效呈现≥runtime声明源帧率92%（NTSC≥55fps、PAL≥46fps），不跨设备混样 |
| PERF02 | 同一运行记录source/media/video因果及本机audio playout位置；统计gain前sample status，静音仍计游标 | 每端A/V误差绝对值p95≤50ms；全队列时间计入；连续欠载不得达到100ms；每次10分钟基准非canonical/插入静音总量≤200ms且≤全部sample的0.1%，单次correction≤200ms；DUAL每个committed PCM rolling digest相同，STREAM同PublishedAudioBlock同bytes/hash且不重复乱序；超限整轮失败 |
| PERF03 | 至少30分钟持续运行、背景/恢复、视频拥塞、2s链路断开、温控/资源事件 | 无crash/ANR/OOM/持续内存增长；协议队列不越界，输入/control不会无限排队；最后认证活动后350ms内冻结/归零；恢复按事务完成；严重热状态降视频或暂停，不能以降帧/插静音绕过PERF01/02 |
| PERF04 | L2固定测量反例：同一edge在40ms呈现预测帧，迟到远端输入使其回滚，100ms才呈现纠正后的canonical帧；另有一edge在1秒内无canonical呈现 | 首例终点必须为100ms并记录correction，不能取40ms使DUAL门禁通过；后一例整轮失败，不能删除样本；本例验证统计器/oracle，不认证物理时延 |

动作采样在guest入口打本机连续时钟及input sequence，输出关联`applied_input_sequence[seat]`并核对画面结果。DUAL的终点是首个包含该edge且后续成为canonical committed结果的画面真正presentation；已呈现预测帧被回滚时，终点改为纠正后canonical帧的presentation并记录correction，不能仅凭tag或“曾显示过”通过。fixture动作足够持久/可辨认，所有edge均进入分母；每edge从采样起最长等待1秒，任一无法关联、永久丢失或超时均使整轮失败，不能从延迟样本删除或仅另记失败后仍判基准通过。trace记录输入采样、网络接收、canonical应用/提交、回滚、编码、解码、present和audio位置，所有阶段队列时间都保留。PERF04冻结独立预期100ms；把该反例重复为一轮中超过5%的edge时，nearest-rank p95必须反映100ms并拒绝DUAL≤80ms的通过结论。

PERF01/02按原规范§25.3的正常基准环境执行：两台正式候选包设备相距1–3米，无路由器/互联网，电量≥30%、初始温控正常，建链后预热2分钟。音频错误预算包括丢包、FEC未恢复静音及rollback/mode correction，在最终本机gain之前统计；重叠状态按sample并集计时，不能重复相加或以用户静音清零。静音前后底层sample水位/digest/错误预算不变；“已记录SILENCE/CORRECTION”不豁免门槛，超限须按策略冻结/降级且该轮基准失败。帧率分母采用完整正常基准窗口及runtime真实PAL/NTSC timing，不能只挑有呈现的片段计算。

presentation callback若只证明提交而非实际显示，必须标记测量能力，不能直接声称touch-to-screen达标。L4增加高帧率外部拍摄/光学校准，用同一可见触发和屏幕响应估计系统显示偏差并报告测量误差；不以两台设备raw clock相减估计延迟。A/V按同一设备可对应的video/audio时间线计算，不要求两个扬声器相位一致。

人为注入12帧延迟、断网、不可用codec等负例验收的是冻结/恢复正确性，不要求在故障窗口继续达到正常可用条件下80/150ms；恢复后正常窗口重新采样。不得把故障冻结时间删掉后将全程宣称无中断。

## 9. 测试资产、运行入口与门禁

下列名称是拟新增测试目标/资产的设计，不是当前可执行目标；本轮不创建文件或改CMake/CI。

| 资产 / 拟议位置 | 用途 |
|---|---|
| `shared/tests/nearby/contract/` | API/port contract，独立C消费者、所有权和race测试 |
| `shared/tests/nearby/harness/` | 虚拟时钟/executor/network/crash-store和受控provider |
| `shared/tests/nearby/scenarios/` | PAIR/WIRE/GAME/MEDIA/REC/CONTENT双引擎用例 |
| `shared/tests/nearby/fixtures/` | 审核过的事件trace、oracle输入、公开测试key、screen/actions/text/geometry期望 |
| `shared/schema/golden/` | 原有及新增协议exact正反golden；V2新增部分经schema评审后生成，禁止仅扩白名单 |
| Android/Harmony/iOS各原生测试目录 | 相同case IDs调用真实binding/ports，记录原生控件与OS结果 |
| `out/evidence/nearby-backend/<revision>/<run-id>/` | ignored证据：manifest、junit/trace、native结果、屏幕/延迟、设备能力说明 |

拟议CTest标签：`nearby_abi`、`nearby_ports`、`nearby_protocol`、`nearby_sync`、`nearby_recovery`、`nearby_ux`。实现后 `ctest --test-dir <本次shared-build> -C Debug --output-on-failure -L nearby_` 必须匹配这些真实注册目标；0 tests匹配是门禁失败，不是通过。sanitizer在受支持host工具链独立运行，Windows普通运行不冒充ASan/TSan证据。

现有入口保留：Android `:app:testDebugUnitTest` / `:app:connectedDebugAndroidTest`；Harmony `tools/quality/run_harmony_completion_gate.ps1` 的Host/HarmonyEmulator/HarmonyDevice；iOS `FlyNESRuntimeTests` / `FlyNESUITests`。未来接线后需验证这些target包含新的场景，不能只重跑原有空壳测试。真实设备缺失记NOT_RUN并阻断其认证组合，不由mock填PASS。

| 门禁 | 必须运行 | 可以延期什么 |
|---|---|---|
| 每次公共合同/协议改动 | L0/L1受影响用例、L2协议/sync/recovery回归、host CTest；shared/native还跑Android unit | 不宣称设备认证；产品UI变动仍按仓库跑instrumentation |
| 系统adapter/bridge改动 | 上述contract复用到真实provider、对应原生测试；Harmony按仓库Host/Hypium/可用设备签名安装 | 不以另一平台结果替代本端 |
| UX接线改动 | UX01–UX18共享projection+三端原生页面；至少关键权限/键盘/横屏组合 | 同逻辑条件的页面缺测不能声称三端完全一致 |
| 发布支持矩阵变更 | L4全声明组合/角色、PERF01–03及L2统计器PERF04、恢复/内容/安全路径 | 未有证据的设备或bearer不加入支持清单 |

## 10. 需求到测试追踪

| 总设计 | 接口合同 | 用例 |
|---|---|---|
| B01/B02 | IF01–03、IF07、IF14、IF16 | API01–API10、API12–15 |
| B03/B04 | IF05–06、IF08–11、IF15 | PAIR01–06、PORT01–04/08、UX04–10/16 |
| B05 | IF07、IF09–10 | WIRE01–03、PAIR06、PORT02/04 |
| B06/B07 | IF03、IF05、IF12 | GAME02–06、API11 |
| B08/B09 | IF12–13 | MEDIA01–08、PORT06–07 |
| B10 | IF08、IF13 | PERF01–04、MEDIA07 |
| B11/B12/B13 | IF02、IF05、IF11–14 | REC01–09、CONTENT01–03、PORT05/08、API14 |
| B14/B15 | IF04–06、IF15–16 | UX01–18、GAME07、API13/15、REC08–09 |
| B16 | IF08–14 | L4九方向/18seat矩阵、PERF01–03 |

本文列出81个用例族：API15、PORT8、PAIR6、WIRE3、GAME7、CONTENT3、MEDIA8、REC9、UX18、PERF4。所有IF01–IF16、B01–B16和UX原C01–C18都有映射；族数量不等于具体场景数量或完整覆盖证明。原规范§25.4–25.6的认证/事务/好友子矩阵继续保留，未来fixture清单须逐项列明继承场景及用例族，不能以本文摘要替换原要求。未定义新测试已通过；本轮验证只检查文档引用、编号完整性及合同语义对齐。
