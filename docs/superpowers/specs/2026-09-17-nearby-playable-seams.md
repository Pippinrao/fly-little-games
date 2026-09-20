# Nearby 可玩首版最小接缝（ENG-02）

日期：2026-09-18。状态：**ABI 冻结；未宣称生产接入或可玩**。

配套：公开头 `shared/include/flynes/flynes_session.h`、UX 投影 `shared/include/flynes/product/nearby_ui_state.hpp`、合同 `shared/schema/nearby_ui_v1.json`。原稿 HTML 与获批 UX 正文不因本文件改写。

本文件只冻结平台读哪些字段、发哪些已有 `fly_session_action_kind_v2`。不为 STREAM/media、完整恢复或新 action 种类开洞。平台不得另造 `connected` / `confirmed` 布尔。

## 1. 所有权与兼容

| 对象 | 规则 |
|---|---|
| `fly_session_snapshot_v2` | 调用方先填 `struct_size` + `abi_version=2`。`fly_session_view_read_v2` 只拷 `min(declared, SIZE)`。`declared < R0` 或版本不符 → `ABI_MISMATCH`。 |
| `fly_session_game_choice_v2` | 尾追加 `core_id`/`profile_id`/`options_id`（R0=272，SIZE=368）。`copy_game_choices_v2` 以 `out_choices[0].struct_size` 为元素 stride，每项只写 `min(declared, SIZE)` 字节并写回调用方 `struct_size`/`abi_version`。`declared < R0`、`declared > SIZE` 或版本不符 → `ABI_MISMATCH`，`written=0`，无写入。禁止按新 `sizeof` 做数组指针步进。 |
| 前缀 | `R0` = `offsetof(dual_mode)`；`R1` = `offsetof(dual_state_digest)` = R0 + DUAL 运行块；`R2` = `offsetof(pending_config_id)` = R1 + 96；`SIZE` = R2 + 48。 |
| `ApprovalToken` | 进程内不透明引用。禁止序列化、落盘、上网或把内部 hash 当授权。禁用动作的 token 为 absent。 |
| Buffer / port 指针 | 只借用到当前调用返回。跨异步必须 retain，最终恰好 release 一次。 |

未知 action kind、未定义 flags、非零 reserved：同步 `INVALID_ARGUMENT`，无副作用。endian 为本机 ABI，不上线。

## 2. UX 语义 → 已有 action / 读口

`flynes::product::nearby::session_action_kind` 给出 UX `ActionId` 的数值身份。返回 0 表示纯本地导航，不提交 session。

| UX 语义 | 读 | 写（已有 kind） | 禁止 |
|---|---|---|---|
| 待房主接受 | `link_state`=`JOINING`/`INVITING`；N04/N05 由 `apply_nearby_session` 投影 | 房主 `ACCEPT_REQUEST=15` / `REJECT_REQUEST=16`；加入方 `CANCEL_JOIN=8` | 平台用“已发送”本地 bool 当已连接 |
| SAS | `fly_session_pairing_v2.local_confirmed` / `peer_confirmed` / `sas[6]` / `stage` | `CONFIRM_SAS=17` / `REJECT_SAS=18` | 单边 confirm 当 Connected；第三套 confirmed 字段 |
| 已连接好友 | `link_state`=`CONNECTED_LOBBY` **且** 入口四事实（established+verified+bound+compat）；`friend_count` + `fly_session_friend_v2.identity_state` | `DISCONNECT_LINK=20` | 从页面路径或持久化 bool 恢复 Connected |
| 选择内容 | catalog `game_choice`；G00 `SelectGame` | `SELECT_CONTENT=42` | 映射到 `START_DUAL=43`；UI 短码当内容身份 |
| pending config | snapshot 尾：`pending_config_id[32]`、`pending_config_revision` | 由 ENG-05 从 catalog 写入；空 id = 未绑定 | 平台自造 fingerprint；把 UI hash 当 id |
| 双确认 | `pending_config_local_confirmed` / `pending_config_peer_confirmed`（0/1） | `CONFIRM_GAME_CONFIG=24` | 单边 1 当可开局；`dual_seats_confirmed` 冒充本局确认 |
| 暂停 / 结束 | `dual_state` / `game_state` / `dual_freeze_reason` | `PAUSE_GAME=32`、`RESUME_GAME=33`、`SAVE_AND_END=36`、`RETURN_TO_LOBBY=25` | 新 pause/end kind；STREAM 降级 |

其余 UX → kind（完整表见 `session_action_kind`）：

| ActionId | kind |
|---|---|
| CreateInvite | `CREATE_INVITE=5` |
| FindDevices | `START_DISCOVERY=3` |
| RegenerateInvite | `REGENERATE_INVITE=6` |
| CancelInvite | `CANCEL_INVITE=7` |
| SubmitJoinCode | `JOIN_CODE=9` |
| ScanQr | `BEGIN_SCAN=10` |
| CancelConnecting | `CANCEL_CONNECT=19` |
| BackToLobby | `RETURN_TO_LOBBY=25` |
| GrantSend / GrantReceive / CancelTransfer / ConfirmImport | 28 / 29 / 30 / 31 |
| Retry / RenameFriend / DeleteFriend / BlockFriend / ResetIdentity | 37 / 38 / 39 / 40 / 41 |
| EnterInviteCode、OpenFriends、GoToGameCenter、ViewDetails、Cancel（无在途请求）、SwitchToScan/JoinCode、NewInvite、BackToEntry、DisableMultiplayerFilter | 0（本地路由） |

`START_DUAL=43` **没有** UX `ActionId`。N09「确认入局」只发 24（空 choice = 本端确认）。对端确认必须来自经过验证的通信消息，经 `DualSessionController::apply_peer_pending_confirm(id, revision)` 接入；id 或 revision 不符 → `STALE`。生产引擎拒绝 `CONFIRM_GAME_CONFIG` + `CHOICE_BOOLEAN`。双方确认齐之前不得发布/接受 43。确认 wire 未落地前不得把 ENG-06 记为完成，也不得用本机布尔把 dual_mvp 打成 GAME_RUNNING。

`PROPOSE_GAME=21`、`CHANGE_AUTHORITY=22`、`CHANGE_SEATS=23` 仍有效，本卡不新增；G00 选游戏走 42，主机/座位变更属 ENG-07。

## 3. Snapshot 待绑定配置（本卡唯一尾追加）

在 `dual_pcm_digest[32]` 之后：

```
uint8_t  pending_config_id[32];          /* 全配置指纹；全零 = 未绑定 */
uint32_t pending_config_local_confirmed; /* 0 或 1 */
uint32_t pending_config_peer_confirmed;  /* 0 或 1；单边 1 ≠ start */
uint64_t pending_config_revision;
```

空 `pending_config_id`：平台必须用 snapshot `primary_reason_key`（当前为 session-read 类原因）禁用确认，不得本地改成可点。填充与校验属 ENG-05；`SELECT_CONTENT` 把 `dual_seats_confirmed = selected_` 属 ENG-05 缺陷，本卡不改。

## 4. 既有 wire 是否覆盖 config / ready / pause / end

| 通道 | 结论 |
|---|---|
| 内容选择 | 已有 `SELECT_CONTENT` + `fly_session_game_choice_v2`（`content_id[32]`、typed `source_choice_ref`）。目录记录 version 2 在名字后追加 `core_id`/`profile_id`/`options_id`。 |
| 配置确认 | 已有 `CONFIRM_GAME_CONFIG=24`；空 choice = 本端确认。对端确认是 Control 已验证流上的 `PendingConfigConfirmV1`（kind `0x0218`），不是 BOOLEAN、不是 `apply_peer_pending_confirm` 测试入口。 |
| runtime ready | `dual_state` READY/RUNNING；装载失败走 `primary_reason_key`。两端就绪屏障属 ENG-06。 |
| 暂停 / 结束 | 已有 32/33/36/25 与 freeze reason `PAUSED`。 |
| STREAM / media | **不做**。`dual_mode` 仅 NONE/DUAL。 |
| 完整恢复 | **不做**。generation/token 失配已有 `STALE`；不在本卡发明 reconnect wire。 |

重放：同一 `operation_id` → `DUPLICATE`。跨 `connection_generation` / `config_revision` 的 token → `STALE`。未知 choice kind → `INVALID_ARGUMENT`。

### 4.1 PendingConfigConfirmV1（ENG-06 / CP2，已接线）

在 **已完成 LINK_READY 的 Control 可靠流** 上发送，不新开 StateCommit，不手写签名算法：认证来自既有 QUIC + session-signing 绑定。握手调度在 READY/ACK 之后必须把 `0x0218` 转给 dual，不得 `PROTOCOL_VIOLATION`。

```
off  size  field
  0     2  version u16be (= 1)
  2     2  reserved_zero
  4    32  pending_config_id
 36     8  pending_config_revision u64be
```

总计 44 字节。domain `flynes-pending-config-confirm-v1`。kind `0x0218`。id 或 revision 与本端当前绑定不符 → `STALE`，不置 peer。重复同一 (id, revision) → `DUPLICATE`，保持已确认。乱序/迟到旧 revision → `STALE`。本端 CONFIRM 不得写 `pending_config_peer_confirmed`。

## 5. 平台读确认清单

1. Connected 只来自 snapshot 四事实 + `link_state`，不来自 N05 接受或单边 SAS。
2. N09 确认键只看 `pending_config_id` 非空 **且** 本端尚未 confirm；开局只看双方 confirm **且** 同一 id/revision（ENG-06）。
3. 只提交 `session_action_kind` 给出的 kind 与引擎签发的 `ApprovalToken`。
4. 不把 `pending_config_id` 或 digest 当可以重放的授权。
5. 普通返回/关详情不 `terminate_attempt`、不重建邀请（见 UX-00.S）。

验证：C ABI `flynes_session_v2_abi`；前缀兼容 `test_dual_prefix_appends_keep_older_callers_legal`；UX 映射 `nearby_product_ui` 中 `test_eng02_ux_actions_reuse_frozen_session_kinds`。
