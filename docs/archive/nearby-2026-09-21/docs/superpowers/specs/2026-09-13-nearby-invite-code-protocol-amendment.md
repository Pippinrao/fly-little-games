> 历史归档（2026-09-21）：本文的执行顺序、首版范围和进度结论已被[当前最小可玩方案](../../../../../nearby/README.md)取代。保留供追溯，不作为新增任务或发布承诺。

# 协议补充：手输配对码的临时邀请查找（2026-09-13）

状态：按 [实现计划](../plans/2026-09-13-nearby-ui-parity-implementation-plan.md) P2 制定。
约束来源：[2026-09-04 附近联机设计](2026-09-04-cross-platform-nearby-multiplayer-design.md) §9.2、§21.4、§22 与
[交接约束](../../handoffs/2026-09-11-nearby-multiplayer-handoff.md) §3。
本补充**不**修改原协议的身份释放、双方授权、文件确认与失败语义，也**不**引入第三套认证协议、互联网房间服务器或明文凭据。

## 1. 能力定位

原协议只有 `BLE_ANONYMOUS` 与 `QR` 两种 route，不存在六位手输邀请查找能力。本补充新增一个
**邀请查找（invite lookup）**子能力：六位手输码只用于**定位房主当前在场的临时匿名邀请**，
定位成功后立即回到原有匿名加入流程（房主明确接受 → 双方六位 SAS 身份校验 → 认证/建网）。

不变式（违反即为实现错误）：

- 手输码**不是** SAS、口令、会话密钥或认证凭据；认证仍由原 BLE/SAS 身份握手提供。
- 匿名阶段不广播、不查询、不显示好友昵称、长期公钥或稳定指纹。
- 单端 SAS 确认、查找命中、房主接受都不构成已连接。
- 没有选择游戏/ROM 不是连接前置条件；完成验证即进入无游戏的连接大厅。

## 2. Wire 消息

在 `flynes_session_v1` registry 中新增两个固定长度 kind（原 GATTLogicalMessageV1 类型
1..26 冻结不变；本补充通过 session 对象 registry 承载，不重载 1..26，也不引入 27..255 的
新语义——这两个对象经既有匿名 GATT 逻辑通道的分段机制传输，类型号在该通道的分配表中登记为
28/29，仅限身份释放前的匿名链路使用）：

### 2.1 `INVITE_CODE_LOOKUP_REQUEST_V1`（kind `0x0214`，32 字节，joiner → host）

| 偏移 | 长度 | 字段 | 约束 |
|---|---|---|---|
| 0 | 2 | `version` u16be | 必须 = 1 |
| 2 | 6 | `reserved` | 必须 = 0 |
| 8 | 6 | `code` | 六个 ASCII 数字（0x30..0x39），保留前导零，无填充/分隔符 |
| 14 | 18 | `reserved1` | 必须 = 0 |

hash domain：`flynes-invite-code-lookup-request-v1`。

### 2.2 `INVITE_CODE_LOOKUP_RESPONSE_V1`（kind `0x0215`，24 字节，host → joiner）

| 偏移 | 长度 | 字段 | 约束 |
|---|---|---|---|
| 0 | 2 | `version` u16be | 必须 = 1 |
| 2 | 6 | `reserved` | 必须 = 0 |
| 8 | 1 | `status` | 枚举：1=MATCH_PENDING_HOST_APPROVAL，2=NO_MATCH，3=EXPIRED，4=RATE_LIMITED |
| 9 | 7 | `reserved` | 必须 = 0 |
| 16 | 8 | `generation` u64be | 仅 status=1 可非零，且必须等于 host 当前活动邀请 generation；其余必须 = 0 |

hash domain：`flynes-invite-code-lookup-response-v1`。

两条消息的 `Truncated/Trailing/NonzeroReserved/UnknownEnum/InvalidField` 拒绝语义与
registry 既有规则一致；golden 集合进入四份 schema 树。

## 3. 生命周期与限额

- **同一被更新的邀请 generation**：手输码与 QR 绑定同一邀请对象。重新生成（N01 重新生成）
  与取消作废都会使旧 generation 立即死亡：对旧 generation 的迟到查找一律应答
  `NO_MATCH`（不复活、不区分描述），旧 QR 按原规范单次消费语义作废。
- **有效期**：沿用原设计 60 秒连续时钟。host 在 `publish` 时启动；到期后查找应答 `EXPIRED`。
  joiner 侧 attempt 同样 60 秒上限；取消、超时之后到达的 match/accept 回调按 stale generation
  丢弃（C16）。
- **并发上界**：host 同一 session 至多一个活动邀请 generation；joiner 至多一个活动查找
  attempt。重生成必须先终结旧 generation。
- **速率限制**：host 对每条已建立匿名连接（每个邀请 generation）最多处理 5 次查找请求；
  超出应答 `RATE_LIMITED` 并可断开该连接。成功命中也计数。
- **冲突处理**：一个 attempt 收到两个不同 `generation` 的 match（或发现层报告多个候选宣称
  同一码）即视为歧义：attempt 进入失败终态，不得静默选择任何身份。相同 generation 的重复
  应答按幂等处理。host 侧永远只对当前活动 generation 应答，旧码不静默映射到新邀请。

## 4. 输入规约（joiner，三端一致）

- 接受恰好 6 个 ASCII 数字，前导零保留（`012345` 六字节全保留）。
- 粘贴内容先去除首尾 ASCII 空白；除空白外出现任何非数字、少于或多于 6 位均不发请求，
  不静默截断。
- 输入不完整时提交动作不可用；字段错误在提交后或输入完成时显示，不逐键报错。
- 提交期间锁定该 attempt，不重复发送。

## 5. 攻击面

- 6 位数字空间 10^6；每邀请 5 次查找限速把穷举压到可忽略，且 regeneration 重置预算并
  更换码值。查找请求不含设备身份；应答不含除活动 generation 外的任何身份信息。
- 码值本身按设计不是秘密也非凭据；它只定位临时邀请，命中后仍需房主明确接受与双方 SAS。
  泄露码值最多导致一次可被房主拒绝的匿名加入请求。
- 拒绝/取消/超时不产生可复用凭据；失败路径不降级明文，不在原请求内自动重试。
- 迟到、重放、跨 generation 的请求/应答必须被 generation fence 丢弃（见 §3）。

## 6. 验收映射

| 用例 | 本补充提供 |
|---|---|
| C05 | §4 输入规约 + `0x0214` 数字约束 |
| C06 | 匿名不变式：请求/应答不携带身份字段 |
| C07 | 单端 SAS 不足、不一致取消 generation（原 SAS 流程，路由只透传结果） |
| C08 | QR 路径不受影响；本补充不强制 QR 走 SAS |
| C16 | §3 迟到/重放/重生成 generation fence |
| C04/C09 | 查找命中 → 原匿名流程 → 无游戏连接大厅 |
