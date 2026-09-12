# A1 — nearby product-UI spine: per-platform implementation spec

Date 2026-09-13. Branch `codex/nearby-multiplayer`. Worktree `E:/workspace/codes/games/fly-little-games/.worktrees/nearby-multiplayer`.
Parent plan: `docs/superpowers/plans/2026-09-11-nearby-product-spine-plan.md` (134 lines, HEAD `fe0534f`). Completion snapshot, rules carried forward and the slice order live there — not repeated here.
Approved design: `docs/superpowers/specs/2026-09-04-cross-platform-nearby-multiplayer-design.md` §22 (lines 1148-1186), blob `c88683050f52cb72773917bb6c97573bdddae8af`.
Decisions that must never be re-decided: `docs/handoffs/2026-09-11-nearby-multiplayer-handoff.md` §3; prohibitions §8.3-8.4.

Three implementers, one platform each: A1a Android, A1b Harmony, A1c iOS. No product decisions are left to an implementer except where §10 names an open question.

## 1. Verified anchors

Every citation below was read in this worktree. Line numbers are the numbers the current file content has (one blank line = one line).

| Ref | Verified content |
|---|---|
| `app/src/main/java/com/flynes/emu/HomeActivity.java:54` | `public final class HomeActivity extends AppCompatActivity {` |
| `HomeActivity.java:89` | `setContentView(R.layout.activity_home);` |
| `HomeActivity.java:171-172` | `findViewById(R.id.open_settings).setOnClickListener( view -> startActivity(new Intent(this, SettingsActivity.class)));` |
| `app/src/main/res/layout/activity_home.xml:3` | `game_center_topbar` LinearLayout opens; `:4` `game_center_heading` TextView |
| `activity_home.xml:13` | `open_settings` MaterialButton (last child of the topbar) |
| `activity_home.xml:14` | `</LinearLayout>` closing the topbar |
| `app/src/main/AndroidManifest.xml:3` | `<uses-permission android:name="android.permission.VIBRATE" />` — the only permission |
| `AndroidManifest.xml:21,31,36,41,46,51` | six `<activity>` elements; `:55` closes the last, `:56` is `</application>` |
| `app/src/main/res/values/strings.xml`, `values-zh-rCN/strings.xml` | 220 lines each; these are the only two `strings.xml` in `app/src/main/res` (no `values-night` copy) |
| `app/src/main/res/values/ids.xml:2-28` | id declarations incl. `pause_layer`, `pause_scrim`, `pause_drawer`, `pause_continue`, `pause_game_center`, `pause_settings` |
| `app/src/main/java/com/flynes/emu/MainActivity.java` | 1666 lines; the pause drawer is built in code (no layout XML): drawer buttons added `:722-730`, layer returned `:738` |
| `app/src/main/java/com/flynes/emu/app/FlyNesApp.java` | Android native bridge class (JNI prefix `Java_com_flynes_emu_app_FlyNesApp_` in `app/src/main/cpp/flynes_app_jni.cpp`) |
| `harmony/entry/src/main/ets/pages/GameCenter.ets:9` | `struct GameCenter {`; `build()` `:33`; `navButtons()` `:154-168`, used from `regularToolbar()` `:77` and `compactToolbar()` `:88` |
| `GameCenter.ets:160,166` | `void router.pushUrl({ url: 'pages/Sources' });`, `... 'pages/Settings' });` |
| `harmony/entry/src/main/resources/base/profile/main_pages.json:2-9` | `"src"` array: GameCenter, RunGame, Sources, Settings, ControlLayout, Index; `:9` is `]` |
| `harmony/entry/src/main/resources/` | only `base`, `rawfile`, `zh_CN` — **no `en_US`** |
| `harmony/entry/src/main/resources/base/element/string.json` | 320 lines, English values in `base`; last entry `permission_file_access_persist` at `:315-318`, `]` at `:319` |
| `harmony/entry/src/main/resources/zh_CN/element/string.json` | 288 lines, Chinese values; same last key `:283-286`, `]` at `:287` |
| `harmony/entry/src/main/ets/pages/RunGame.ets:147-202` | `@Builder pauseDrawer()` — eyebrow, title, `pause_checkpoint_failed` `:168-173`, `ForEach(this.pauseIds, …)` `:174-185`, builder closes `:202` |
| `harmony/entry/src/main/cpp/napi_init.cpp` | 1629 lines, Harmony NAPI bridge; typings at `harmony/entry/src/main/cpp/types/libentry/Index.d.ts` |
| `harmony/entry/src/main/module.json5:36-47` | `requestPermissions` array holding only `ohos.permission.FILE_ACCESS_PERSIST` |
| `harmony/hvigorfile.ts` + `harmony/README.md:33-46` | no project-local `hvigorw`; build via DevEco-bundled node + `hvigorw.js` |
| `ios/app/CatalogLibraryView.swift:3-6` | `enum LibraryRoute: Hashable { case detail(CatalogGame); case run(String) }` |
| `CatalogLibraryView.swift:29` | `struct CatalogLibraryView: View {` |
| `CatalogLibraryView.swift:70-81` | `ToolbarItemGroup(placement: .topBarTrailing)` with `library.sources` `:71-75` and `settings.title` `:76-80` |
| `CatalogLibraryView.swift:83-90` | `navigationDestination(for: LibraryRoute.self)` switch over `.detail` / `.run` |
| `ios/app/CMakeLists.txt:41-52` | `set(FLYNES_PRODUCT_SWIFT …)` list, 10 entries; `:53-62` `FLYNES_PRODUCT_OBJCXX` |
| `ios/app/CMakeLists.txt:84` | `bridge/FlyNes-Bridging-Header.h` in the target sources; `:18` `enable_language(Swift)` |
| `ios/app/en.lproj/Localizable.strings`, `zh-Hans.lproj/Localizable.strings` | 61 lines each, dotted keys; last key `library.rom_open_failed` at `:61` |
| `ios/app/bridge/FlyNes-Bridging-Header.h:1-7` | imports `FlyNesAppBridge.h`, `FlyNesRuntimeBridge.h`, … |
| `ios/app/bridge/FlyNesAppBridge.h:10-37` | only create/settings/controlLayout/catalogSnapshot/scanBorrowedFd — **no nearby/session method** |
| `ios/app/run/RunSurfaceViewController.mm` | 406 lines; owns `pauseButton_`, `pauseLayer_`, `drawerOpen_`, `flynes/product/pause_actions.hpp` |
| `ios/app/Info.plist.in:1-50` | no `NSBluetooth*`, no `NSCameraUsageDescription`, no `NSLocalNetworkUsageDescription`, no `NSBonjourServices`; `</dict>` at `:49` |
| No `*.xcodeproj` / `*.xcworkspace` / `*.pbxproj` anywhere in the tree | iOS project is generated; `ios/app/CMakeLists.txt:8-10` requires `-G Xcode` and `:11-13` requires arm64 |
| `ios/CMakeLists.txt` (116 lines) vs `ios/app/CMakeLists.txt` (179 lines) | `-S ios` builds `FlyNESPortabilitySmoke` only (`ios/CMakeLists.txt:43-52`); the product app is a **separate** CMake project rooted at `ios/app` (`add_executable(FlyNES …)` at `ios/app/CMakeLists.txt:80`) |
| `shared/include/flynes/flynes_session.h` | at HEAD `fe0534f`: 194 lines, `fly_session_ui_state` `:54-57` holds only `FLY_SESSION_UI_IDLE`, `fly_session_command_kind` `:49-52` only `NONE`, snapshot `:147-157`. **Under concurrent modification in this worktree** — see the warning below |
| `app/src/main/cpp/flynes_app_jni.cpp`, `harmony/entry/src/main/cpp/napi_init.cpp`, `ios/app/bridge/FlyNesAppBridge.mm` | **zero** matches for `fly_session` / `flynes_session` (grep, all three) |
| `shared/schema/golden/` | 39 object-kind directories; `shared/schema/flynes_session_v1.schema` = 123 079 bytes |
| Truly dead nearby code | `app/src/main/cpp/nearby/nearby_dtos.hpp`, `ios/app/platform/nearby/nearby_dtos.hpp`, `harmony/nearby/nearby_dtos.hpp`; `harmony/nearby/{nearby_adapter,friend_store}.{hpp,cpp}` compiled only by `harmony/tests/CMakeLists.txt:141-144`; `harmony/entry/src/main/cpp/CMakeLists.txt` has no `nearby` reference |

### 1.1 Corrections to the dispatch brief

1. `ios/app/FlyNesAppBridge.mm` does not exist. Actual path: **`ios/app/bridge/FlyNesAppBridge.mm`** (header `ios/app/bridge/FlyNesAppBridge.h`).
2. The brief implies C2 is the next slice. HEAD `fe0534f` has C2 **IN PROGRESS** and adds slice **C2b** (wire type identification + envelope), which is the actual blocker for the receive path. A1's ABI dependencies must be read against plan lines 81-106, not the older C2 text.
3. `ios/app/CatalogLibraryView.swift` is 127 lines (not 118); `app/src/main/java/com/flynes/emu/HomeActivity.java` is 615 lines. Both cited anchors were still correct.
4. iOS product build root is `ios/app`, not `ios`. `ios/CMakeLists.txt` (`-S ios`) builds only `FlyNESPortabilitySmoke`; `ios/app/CMakeLists.txt` is the `FlyNES` product project. `ios/README.md:3-6` ("not the FlyNES product UI", "no public bridge API") describes only the probe and is stale for `ios/app`.
5. The Android in-game pause drawer has no layout XML — it is constructed in `MainActivity.java` and its ids come from `app/src/main/res/values/ids.xml`.
6. Harmony has no project-local `hvigorw`; any verification command must call DevEco's bundled `hvigorw.js` (`harmony/README.md:33-46`).

## 2. Vocabulary — one table, three platforms

Canonical id → platform key: **iOS** uses the id verbatim as a `Localizable.strings` key; **Android** and **Harmony** use the id with `.` → `_` as the resource name. No platform may rename, reorder or reword a row.

Pipeline order = display order. The string "失败阶段" is a stage name plus its reason; both keys exist.

### 2.1 Pairing failure stages (design §22.2 last bullet)

| id | User-visible meaning | Reason key | Locally knowable today? |
|---|---|---|---|
| `nearby.stage.permission` | 权限 | `nearby.stage.permission.reason` (Android/Harmony), `nearby.stage.permission.reason_ios` | **Android/Harmony: yes** — an undeclared permission query returns DENIED, which is truthfully reportable. **iOS: no** — with no usage-description key there is no query API at all, so iOS renders a static "尚未申请" reason under its own key (D1). |
| `nearby.stage.discovery` | 发现 | `nearby.stage.discovery.reason` | No — no bearer/ABI |
| `nearby.stage.auth` | 认证 | `nearby.stage.auth.reason` | No |
| `nearby.stage.wifi` | Wi-Fi | `nearby.stage.wifi.reason` | No |
| `nearby.stage.quic` | QUIC | `nearby.stage.quic.reason` | No |
| `nearby.stage.version` | 版本 | `nearby.stage.version.reason` | No |
| `nearby.stage.codec` | codec | `nearby.stage.codec.reason` | No |

### 2.2 Lobby fields (design §22.3) and in-game fields (§22.4)

| id | User-visible meaning | §  | ABI/source today |
|---|---|---|---|
| `nearby.lobby.friend_name` | 好友昵称 | 22.3 | none — no friend identity in the ABI |
| `nearby.lobby.identity_fingerprint` | identity fingerprint 短码 | 22.3 | none |
| `nearby.lobby.authority_capability` | authority 候选能力 | 22.3 | none |
| `nearby.lobby.resource_risk` | 资源风险 | 22.3 | none |
| `nearby.lobby.network_owner` | 当前网络 owner | 22.3 | none |
| `nearby.lobby.seat` | P1/P2 seat | 22.3 | `fly_session_snapshot.authority_role` exists, no `P1/P2` value is defined |
| `nearby.lobby.rom_identity` | ROM 身份 | 22.3 | none |
| `nearby.lobby.rom_local_state` | 两端各自是否已有 | 22.3 | none |
| `nearby.lobby.rom_transfer_confirm` | 传输确认 | 22.3 | none |
| `nearby.lobby.rom_transfer_progress` | 传输进度 | 22.3 | none |
| `nearby.lobby.profile_verified` | MultiplayerProfile 是否已验证 | 22.3 | none |
| `nearby.lobby.mode_expected` | 预计 DUAL 或 STREAM | 22.3 | `fly_session_snapshot.mode` exists, no `FLY_SESSION_MODE_*` values are defined |
| `nearby.lobby.local_audio` | 两端本地声音开关 | 22.3 | none — no media/mute in the ABI |
| `nearby.lobby.confirm` | 双方确认 | 22.3 | `FLY_SESSION_COMMAND_NONE` is the only command kind |
| `nearby.lobby.confirm_invalidated` | authority/seat 变化后确认失效 | 22.3 | none |
| `nearby.ingame.mode` | 当前模式 | 22.4 | as `nearby.lobby.mode_expected` |
| `nearby.ingame.connection_quality` | 连接质量 | 22.4 | none |
| `nearby.ingame.seat` | P1/P2 | 22.4 | share the row above; separate key because the in-game label is short |
| `nearby.ingame.pause_state` | 暂停/重连状态 | 22.4 | `fly_session_snapshot.ui_state` is hardcoded `UI_IDLE` |
| `nearby.ingame.frozen` | 冻结显示 | 22.4 | none — no FROZEN/UNCERTAIN flag |
| `nearby.ingame.reconnect_countdown` | 重连倒计时 | 22.4 | partial — `fly_session_tick` has a 60 s attempt deadline, no exposed remaining time |
| `nearby.ingame.authority_timeout` | authority 超时 | 22.4 | none |
| `nearby.ingame.takeover` | 接管 | 22.4 | none |
| `nearby.ingame.continue_solo` | 继续单人 | 22.4 | none |
| `nearby.ingame.save_and_end` | 保存结束 | 22.4 | none |
| `nearby.ingame.option_unavailable_reason` | 选项不可用原因 | 22.4 | none |
| `nearby.ingame.branch_reunion` | 多 branch 重逢 | 22.4 | none |
| `nearby.ingame.branch_no_auto_merge` | 明确不会自动合并 | 22.4 | none |
| `nearby.ingame.local_mute` | 每端独立静音 | 22.4 | none |

### 2.3 Scaffolding and blocked-state keys (Table K)

All ids below are added to every locale file that platform has. Grouped, not repeated per platform.

| Group | ids |
|---|---|
| Entry + pages | `nearby.title`, `nearby.open`, `nearby.tab.friends`, `nearby.tab.devices`, `nearby.find_devices`, `nearby.scan_host_qr` |
| Empty states | `nearby.friends.empty`, `nearby.devices.empty` |
| Pairing actions | `nearby.join.request_anonymous`, `nearby.join.accept`, `nearby.join.reject`, `nearby.code.label`, `nearby.code.confirm`, `nearby.wifi.path_building`, `nearby.wifi.system_confirm_once` |
| Friends management | `nearby.friends.section`, `nearby.friends.manage`, `nearby.friends.saved_after_auth`, `nearby.friends.rename`, `nearby.friends.delete`, `nearby.friends.block`, `nearby.friends.identity_reset` |
| Blocked reasons | `nearby.blocked.permissions`, `nearby.blocked.discovery`, `nearby.blocked.auth`, `nearby.blocked.wifi`, `nearby.blocked.quic`, `nearby.blocked.version`, `nearby.blocked.codec`, `nearby.blocked.friend_store`, `nearby.blocked.rom_transfer`, `nearby.blocked.profile_verify`, `nearby.blocked.mode_gate`, `nearby.blocked.connection_quality`, `nearby.blocked.authority_recovery`, `nearby.blocked.branch_merge`, `nearby.blocked.local_mute`, `nearby.blocked.session_read` |
| Stage status (accessibility) | `nearby.stage.status.passed`, `nearby.stage.status.current`, `nearby.stage.status.not_reached` |

`nearby.blocked.session_read` is the generic fallback: "会话状态尚未接入本端" — used wherever a page needs a session value the ABI does not yet expose.

**Stage-status keys are mandatory, not decorative.** The three keys above must exist on every platform and must be used as the accessible label or value of each pipeline stage row. UX rule `color-not-only` forbids conveying state by colour alone, and `voiceover-sr` requires a meaningful announced label; an icon plus a colour is not sufficient for a screen-reader user. Whether the status also appears as visible text is each platform's choice, but the accessible string is required.

**Resolved reading of D5 versus §4 (confirmed after A1c-1 raised it).** The pipeline stage list and the per-capability rows are **two different UI elements** and are not contradictory:
- The **pipeline list** renders all seven stages in order; only the first failing stage carries `nearby.stage.<stage>.reason`; earlier stages are marked passed and later stages are marked not-reached with no reason and no blocked key (D5).
- A **capability row** (for example the BLE-discovery capability above the 寻找设备 button) shows `nearby.stage.discovery` plus `.reason` and the capability's own `nearby.blocked.discovery` on its disabled control, as §4's table requires.
The two elements may both be on the 附近设备 tab. Keep them visually distinct so no reader sees one list with two renderings. This is the converged reading all three platforms already implemented independently — keep it.

## 3. ABI boundary — what the UI may read

Rule for all three platforms: UI reads session state **only** through the platform's existing shared-ABI binding (Android JNI `app/src/main/cpp/flynes_app_jni.cpp` + `com.flynes.emu.app.FlyNesApp`; Harmony NAPI `harmony/entry/src/main/cpp/napi_init.cpp` + `types/libentry/Index.d.ts`; iOS ObjC++ `ios/app/bridge/FlyNesAppBridge.{h,mm}`). No platform-local protocol model, no new DTO, and no reuse of the dead `nearby_dtos.hpp` headers (`app/src/main/cpp/nearby/`, `harmony/nearby/`, `ios/app/platform/nearby/`).

**The ABI is moving while A1 is being written.** Plan slice C2 is in progress in this same worktree: `shared/include/flynes/flynes_session.h` was 194 lines at HEAD `fe0534f` and is 223 lines in the working tree, `flynes_session.cpp` and two session test files are dirty, and `shared/tests/test_session_public_path.cpp` is a new untracked test. `FLY_SESSION_UI_UNSPECIFIED = 0` has already been added; `FLY_SESSION_MODE_*` still does not exist (header `:169` declares `uint32_t mode` with no value enum). Re-verify the two columns below at dispatch time instead of trusting them.

Verified gap: **none of the three bindings references `fly_session` at all.** The public session path itself is still largely stubbed (parent plan §"Session public path"), and `fly_session_event` has no payload while `session_codec.cpp` cannot classify bytes (plan lines 83-104). Therefore:

| UI need | ABI today | A1 treatment |
|---|---|---|
| Session handle + snapshot | `fly_session_create` / `_get_snapshot` exist; at HEAD the snapshot was hardcoded `UI_IDLE` + genesis cursor, which C2 is replacing | blocked; render `nearby.blocked.session_read` |
| Pairing stage | no stage enum, no field | blocked except `nearby.stage.permission` |
| Friend identity / fingerprint | none | blocked; friends list renders empty state only |
| Seat / authority / mode | fields exist, enum values do not | blocked; label row shows `nearby.blocked.mode_gate` |
| ROM identity + transfer progress | none | blocked |
| MultiplayerProfile verification | none | blocked |
| Per-end local mute | none | blocked, except the existing local app audio setting |
| Pause / reconnect countdown / authority timeout options | none | blocked |
| Multi-branch reunion | none | blocked |

Consequence for planning: A1 tasks that only lay out pages and render blocked/empty states are dispatchable now. Any task that renders a live session value is gated on the ABI additions (plan C2 / C2b, §12.1 versioned `struct_size`/`abi_version` additions) — and that gate is **not** owned by A1.

## 4. Truthful placeholder contract

Global, all three platforms:

- A page never shows a friend, device, peer, session or count that the local platform cannot prove exists. Empty lists show the `empty` key, never sample rows.
- A control that cannot act is **absent**; if its absence would hide a required stage, it is shown disabled with a visible reason (`nearby.blocked.*` / `nearby.stage.*.reason`).
- No navigate-only button: every button either performs a real local action or is disabled with a reason. Buttons that only push a page which itself shows only blocked states are permitted **only** as the entry into 好友/附近设备 and 配对, because those pages must exist to display the blocking stage (plan line 143; handoff §8.3 line 90).
- The pairing page always renders the stage list from §2.1 in order. The current (first failing) stage is marked; every later stage is shown in a **neutral "not yet reached" treatment with no blocked key and no reason**, and every earlier stage is shown as passed. Stage *selection* is read from the session ABI once A0 lands; until then the page marks `nearby.stage.permission`. Do **not** annotate unmarked rows with `nearby.blocked.session_read` — that gives one list two contradictory renderings and leaves the i18n task no single rule.
- **Scope reading, written down rather than left implicit:** handoff §8.3 forbids passing a half-built capability off as implemented. A *session-scoped* surface with no session, or a *body-less* concept with no wire object, is not a capability being faked — it is a surface that must not be shown at all. That is why D7 shows nothing in single-player and D10 defers branch reunion. This reading does **not** license hiding a capability a user can reach: anything reachable and unimplemented still shows its exact stage and reason.

Per page and capability:

| Page | Capability | Shown while unimplemented |
|---|---|---|
| Game-center entry | 附近联机 entry | Always enabled (real local navigation) |
| 好友/附近设备 | Saved friend list | `nearby.friends.empty` + `nearby.blocked.friend_store` |
| 好友/附近设备 | BLE discovery | `nearby.stage.discovery` + `.reason`; 寻找设备 disabled with `nearby.blocked.discovery` |
| 好友/附近设备 | Host-QR scan | `nearby.stage.discovery` + `.reason`; 扫描房主二维码 disabled with `nearby.blocked.discovery` |
| 配对 | Anonymous join request | **⚠ over-delivery — see the note under this table; needs §30 review before it is built.** Render `nearby.join.request_anonymous` heading + `nearby.blocked.session_read`; accept/reject disabled with reason |
| 配对 | 6-digit code | `nearby.code.label` + `nearby.blocked.auth`; confirm disabled |
| 配对 | Wi-Fi path | all five of `permission…codec` stages rendered; current stage marked |
| 大厅 | every §22.3 field | row label + `nearby.blocked.session_read` (or the field-specific blocked key from §3) |
| 大厅 | 双方确认 | `nearby.lobby.confirm` disabled with `nearby.blocked.session_read` |
| 游戏中状态 | every §22.4 field | row label + `nearby.blocked.session_read`; the status block lives in the existing pause drawer. **Separately**, the states that demand user action — 冻结, 重连倒计时, and the authority-timeout options (接管 / 继续单人 / 保存结束) — render as a **non-dismissible banner row pinned to the top of the run surface**, because a frozen end must not have to hunt through the drawer to recover. The banner is not a modal and is not dismissible while the state persists. |
| 好友管理 | rename / delete / block | entries present, disabled with `nearby.blocked.friend_store` |
| 好友管理 | 本机身份重置 | entry present, disabled with `nearby.blocked.friend_store` |

**Over-delivery flagged by independent review.** This table's 配对 rows are the one place §2 goes beyond the approved design. Design §22.2 (spec:1159-1161) names an anonymous join request **only for the QR path** and a 6-digit code **only for the BLE path**; it does not require a generic `nearby.join.request_anonymous` / `accept` / `reject` control set. Building it would ship UI the approved design does not require. Resolution: keep the keys in the vocabulary (they are harmless) but mark the pairing page's anonymous-join block as **pending §30 centralised review** and do not build it until that review answers. Everything else in the table is derived from design §22 and is buildable.

## 5. A1a — Android

### 5.1 Files to create

| Path | Purpose |
|---|---|
| `app/src/main/java/com/flynes/emu/NearbyFriendsActivity.java` | 好友/附近设备 |
| `app/src/main/java/com/flynes/emu/NearbyPairingActivity.java` | 配对 |
| `app/src/main/java/com/flynes/emu/NearbyLobbyActivity.java` | 大厅 |
| `app/src/main/java/com/flynes/emu/NearbyFriendsManageActivity.java` | 好友管理 |
| `app/src/main/res/layout/activity_nearby_friends.xml` | layout, `activity_*` naming convention |
| `app/src/main/res/layout/activity_nearby_pairing.xml` | |
| `app/src/main/res/layout/activity_nearby_lobby.xml` | |
| `app/src/main/res/layout/activity_nearby_friends_manage.xml` | |

Follow `SettingsActivity.java` conventions (`extends AppCompatActivity`, `final class`, `setContentView`, landscape `sensorLandscape`).

### 5.2 Files to modify

| Path | Exact insertion |
|---|---|
| `app/src/main/res/layout/activity_home.xml` | new `MaterialButton android:id="@+id/open_nearby"` modelled on `:13` (`open_settings`), inserted as a new line between `:13` and the topbar's `</LinearLayout>` at `:14` |
| `app/src/main/java/com/flynes/emu/HomeActivity.java` | after `:172`, add `findViewById(R.id.open_nearby).setOnClickListener(view -> startActivity(new Intent(this, NearbyFriendsActivity.class)));` |
| `app/src/main/AndroidManifest.xml` | insert four `<activity>` elements after `:55` (before `</application>` at `:56`), copying the shape of `:41-45` (`GameLibraryActivity`): `android:exported="false"`, `android:screenOrientation="sensorLandscape"`, `android:configChanges="orientation|screenSize|keyboardHidden"` |
| `AndroidManifest.xml` | permission declarations after `:3` (task A1a-P, gated by §10 D1) |
| `app/src/main/res/values/strings.xml` | insert all §2 keys before `</resources>` at `:220` |
| `app/src/main/res/values-zh-rCN/strings.xml` | insert the same keys before `</resources>` at `:220` |
| `app/src/main/java/com/flynes/emu/SettingsActivity.java` | 好友管理 entry row (gated by §10 D2) |
| `app/src/main/java/com/flynes/emu/MainActivity.java` | in-game status block: `drawer.addView(…)` after `:730`, before the drawer layout params at `:732` |
| `app/src/main/res/values/ids.xml` | add the ids the new in-game block needs, before `</resources>` at `:28` |

Navigation registration on Android is the manifest `<activity>` list — there is no route registry, so nothing else registers the pages.

### 5.3 i18n

Two locale files only: `values/strings.xml` (English) and `values-zh-rCN/strings.xml` (Chinese). Add **every** id from §2.1, §2.2 and §2.3 in both, using the `.`→`_` name transform.

### 5.4 Verification

```powershell
cd E:\workspace\codes\games\fly-little-games\.worktrees\nearby-multiplayer
.\gradlew assembleDebug
```

Evidence: exit code 0; the tail of the Gradle output; `app/build/outputs/apk/debug/` artifact name + SHA-256. Then prove registration and i18n without a device:

```powershell
Select-String -Path app\src\main\AndroidManifest.xml -Pattern 'Nearby'
(Select-String -Path app\src\main\res\values\strings.xml -Pattern 'name="nearby_').Count
(Select-String -Path app\src\main\res\values-zh-rCN\strings.xml -Pattern 'name="nearby_').Count
```

Both counts must be equal and must equal the number of ids in §2.1+§2.2+§2.3. Real UI evidence needs the emulator instrumentation suite (`app/src/androidTest`); `adb devices` is currently empty (plan line 20), so no instrumentation evidence can be captured now — say so, do not substitute a compile for it.

## 6. A1b — Harmony

### 6.1 Files to create

| Path | Purpose |
|---|---|
| `harmony/entry/src/main/ets/pages/NearbyFriends.ets` | 好友/附近设备, `@Entry @Component struct NearbyFriends` |
| `harmony/entry/src/main/ets/pages/NearbyPairing.ets` | 配对 |
| `harmony/entry/src/main/ets/pages/NearbyLobby.ets` | 大厅 |
| `harmony/entry/src/main/ets/pages/NearbyFriendsManage.ets` | 好友管理 |

Every user-visible string must go through `$r('app.string.…')`; no literal text in `.ets`.

### 6.2 Files to modify

| Path | Exact insertion |
|---|---|
| `harmony/entry/src/main/resources/base/profile/main_pages.json` | add `"pages/NearbyFriends", "pages/NearbyPairing", "pages/NearbyLobby", "pages/NearbyFriendsManage"` as new entries after `:8` (`"pages/Index"`), before `]` at `:9`. **A page absent from this file cannot be pushed.** |
| `harmony/entry/src/main/ets/pages/GameCenter.ets` | third `Button($r('app.string.nearby_title'))` inside `navButtons()` (`:154-168`), inserted after `:167` (after the Settings button's `onClick`), calling `void router.pushUrl({ url: 'pages/NearbyFriends' });` — same shape as `:162-167` |
| `harmony/entry/src/main/resources/base/element/string.json` | new entries after `:318` (after `permission_file_access_persist`), before `]` at `:319` |
| `harmony/entry/src/main/resources/zh_CN/element/string.json` | new entries after `:286`, before `]` at `:287` |
| `harmony/entry/src/main/ets/pages/Settings.ets` | 好友管理 entry (gated by §10 D2) |
| `harmony/entry/src/main/ets/pages/RunGame.ets` | in-game status block inside `pauseDrawer()`, inserted after the `ForEach(this.pauseIds, …)` block ends at `:185` |
| `harmony/entry/src/main/module.json5` | permission entries inside `requestPermissions` (`:36-47`) — task A1b-P, gated by §10 D1 |

### 6.3 i18n

Two locale files only: `base/element/string.json` (English values, the fallback) and `zh_CN/element/string.json` (Chinese). There is no `en_US`. Add every §2 id to both with the `.`→`_` transform and the existing `{"name": …, "value": …}` object shape. Do not add a new locale directory: that is a product decision (§10 D3).

### 6.4 Verification

```powershell
cd E:\workspace\codes\games\fly-little-games\.worktrees\nearby-multiplayer
$env:DEVECO_STUDIO_HOME = "D:\soft\DevEco Studio"
$env:DEVECO_SDK_HOME = "$env:DEVECO_STUDIO_HOME\sdk"
$Node = "$env:DEVECO_STUDIO_HOME\tools\node\node.exe"
Push-Location harmony
try {
  & $Node "$env:DEVECO_STUDIO_HOME\tools\ohpm\bin\pm-cli.js" install --all
  & $Node "$env:DEVECO_STUDIO_HOME\tools\hvigor\bin\hvigorw.js" assembleHap `
    -p product=default -p module=entry@default -p buildMode=debug
} finally { Pop-Location }
```

Evidence: exit code 0 and the produced `.hap` path/hash. An **unsigned** HAP proves compile/link/package only (`harmony/README.md:48-51`); signing is currently blocked in this worktree (plan line 21), so no install or on-device evidence. Also record:

```powershell
Get-Content harmony\entry\src\main\resources\base\profile\main_pages.json
(Select-String -Path harmony\entry\src\main\resources\base\element\string.json -Pattern '"name": "nearby_').Count
(Select-String -Path harmony\entry\src\main\resources\zh_CN\element\string.json -Pattern '"name": "nearby_').Count
```

The two counts must be equal and must cover every §2 id.

## 7. A1c — iOS

### 7.1 Files to create

| Path | Purpose |
|---|---|
| `ios/app/NearbyFriendsView.swift` | 好友/附近设备 |
| `ios/app/NearbyPairingView.swift` | 配对 |
| `ios/app/NearbyLobbyView.swift` | 大厅 |
| `ios/app/NearbyFriendsManageView.swift` | 好友管理 |

### 7.2 Files to modify

| Path | Exact insertion |
|---|---|
| `ios/app/CMakeLists.txt` | add the four filenames to `FLYNES_PRODUCT_SWIFT` (`:41-52`) — e.g. after `:50` (`RunGameView.swift`). **A Swift file missing from this list is not compiled**, because there is no Xcode project in the tree. |
| `ios/app/CatalogLibraryView.swift` | (a) `enum LibraryRoute` (`:3-6`): add `case nearby`; (b) `ToolbarItemGroup` (`:70-81`): add a `NavigationLink { NearbyFriendsView() } label: { Text("nearby.title") }` after `:80`; (c) `navigationDestination` (`:83-90`): add `case .nearby: NearbyFriendsView()` to the switch |
| `ios/app/en.lproj/Localizable.strings` | append key/value lines after `:61` |
| `ios/app/zh-Hans.lproj/Localizable.strings` | append the same keys after `:61` |
| `ios/app/SettingsView.swift` | 好友管理 entry row (gated by §10 D2) |
| `ios/app/run/RunSurfaceViewController.mm` | in-game status rows in the existing pause drawer, after `pause_command_title` usage / the drawer's button stack (locate the `drawerOpen_` population; 406-line file) |
| `ios/app/Info.plist.in` | permission/usage keys before `</dict>` at `:49` — task A1c-P, gated by §10 D1 |
| `ios/app/bridge/FlyNesAppBridge.h` + `.mm` | session reads, only if the ABI surface is assigned to A1c (§10 D4). `FlyNesAppBridge.h:10-37` currently has no nearby/session method, so every live value is blocked |

### 7.3 i18n

Two locale files only: `en.lproj/Localizable.strings` and `zh-Hans.lproj/Localizable.strings`. Keys are the §2 ids verbatim (dotted). Nothing registers these files beyond `ios/app/CMakeLists.txt:86-98`, which already lists both.

### 7.4 Verification

**Not possible on this machine.** The build requires macOS + Xcode 26.6 and the `Xcode` generator (`ios/app/CMakeLists.txt:5-13`), and no `*.xcodeproj`/`*.pbxproj` exists in the tree. The command an implementer on macOS runs is:

```sh
cmake -S ios/app -B build/ios-app -G Xcode \
  -DCMAKE_SYSTEM_NAME=iOS -DCMAKE_OSX_SYSROOT="$(xcrun --sdk iphoneos --show-sdk-path)" \
  -DCMAKE_OSX_ARCHITECTURES=arm64 -DCMAKE_OSX_DEPLOYMENT_TARGET=17.0
cmake --build build/ios-app --config Debug
```

`ios/README.md:21-26` shows the same configure shape for the probe at `ios/`. On Windows all that can be captured is static evidence:

```powershell
Select-String -Path ios\app\CMakeLists.txt -Pattern 'Nearby'
(Select-String -Path ios\app\en.lproj\Localizable.strings -Pattern '"nearby\.').Count
(Select-String -Path ios\app\zh-Hans.lproj\Localizable.strings -Pattern '"nearby\.').Count
```

Plus a Swift-syntax-only review, which is **not** a build. Any claim that A1c compiles is unsupported until a macOS run exists.

## 8. Task decomposition

One focused work item per task. `⇢` = gated on the named open question or slice; such a task may be written but not finished until the gate clears.

| Task | Platform | Deliverable | Depends on |
|---|---|---|---|
| A1a-1 | Android | `open_nearby` button in `activity_home.xml` + wiring in `HomeActivity` + manifest entry + empty `NearbyFriendsActivity`/layout showing the §2.1 stage list and `nearby.friends.empty` | — |
| A1a-2 | Android | `NearbyPairingActivity`/layout: anonymous-join heading, 6-digit-code block, Wi-Fi-path block, all blocked states per §4 | A1a-1 |
| A1a-3 | Android | `NearbyLobbyActivity`/layout: all §2.2 rows rendered as blocked | A1a-1 |
| A1a-4 | Android | in-game status block in `MainActivity` pause drawer + new ids in `ids.xml` | A1a-1 |
| A1a-5 | Android | `NearbyFriendsManageActivity`/layout + page reachable from the friends page | A1a-1 |
| A1a-6 | Android | Settings-页 好友管理 entry | ⇢ D2 |
| A1a-7 | Android | permission declarations | ⇢ D1 |
| A1b-1 | Harmony | `main_pages.json` registration + `navButtons()` entry + `NearbyFriends.ets` (stage list + empty state) | — |
| A1b-2 | Harmony | `NearbyPairing.ets` | A1b-1 |
| A1b-3 | Harmony | `NearbyLobby.ets` | A1b-1 |
| A1b-4 | Harmony | in-game block in `RunGame.ets pauseDrawer()` | A1b-1 |
| A1b-5 | Harmony | `NearbyFriendsManage.ets` + page reachable from the friends page | A1b-1 |
| A1b-6 | Harmony | Settings-页 好友管理 entry | ⇢ D2 |
| A1b-7 | Harmony | `module.json5` permission entries | ⇢ D1 |
| A1c-1 | iOS | `CMakeLists.txt` list + `LibraryRoute.nearby` + toolbar link + `navigationDestination` case + `NearbyFriendsView.swift` | — |
| A1c-2 | iOS | `NearbyPairingView.swift` | A1c-1 |
| A1c-3 | iOS | `NearbyLobbyView.swift` | A1c-1 |
| A1c-4 | iOS | in-game rows in `RunSurfaceViewController.mm` pause drawer | A1c-1 |
| A1c-5 | iOS | `NearbyFriendsManageView.swift` + page reachable from the friends page | A1c-1 |
| A1c-6 | iOS | Settings-页 好友管理 entry | ⇢ D2 |
| A1c-7 | iOS | `Info.plist.in` usage keys | ⇢ D1 |
| A1-8 | all three | i18n: add every §2 id to all six locale files, with the `.`→`_` transform on Android/Harmony | may run in parallel with A1*-1..5 |

Dependency order: `A1*-1` first on every platform; `A1-8` may start immediately; `A1*-2..5` follow `A1*-1`; `A1*-6..7` wait for D1/D2. A1a, A1b and A1c share no file and no state, so all three chains run concurrently.

**Correction after independent review:** under D1 (pages-first) the three `A1*-7` permission tasks are **not part of A1 at all** — each permission is declared in the same change that first *uses* it, so those rows move to the capability slices. Their `⇢ D1` gate is closed by that decision rather than still open, and an implementer must not create empty permission declarations to satisfy them. `A1*-6` (the Settings 好友管理 entry) is in scope per D2.

Live-value rendering (any task that shows a real seat, mode, stage, friend, or progress value) is **not** in A1. It is gated on plan C2 + C2b landing the ABI and on whoever owns adding the session surface to the three bindings (§10 D4).

## 9. Verification commands and evidence

| Platform | Command | Evidence to capture | Blocker |
|---|---|---|---|
| Android | `.\gradlew assembleDebug` | exit code, output tail, APK path + SHA-256, manifest grep for the four activities, equal `nearby_` counts in both `strings.xml` | No device: `adb devices` empty (plan line 20) → no instrumentation UI evidence |
| Harmony | DevEco-bundled `hvigorw.js assembleHap -p product=default -p module=entry@default -p buildMode=debug` (§6.4) | exit code, `.hap` path, `main_pages.json` dump, equal `nearby_` counts in `base` and `zh_CN` | Unsigned HAP only; signing still fails in the isolated probe project (plan line 21) |
| iOS | macOS only: `cmake -S ios/app -B build/ios-app -G Xcode …` (§7.4) | configure + build exit codes, `FlyNES.app` path, localization key counts | **Cannot be built or verified on this Windows machine.** No Xcode, no Apple SDK, no generated project |

After any `cmake --build`, remove the case-variant proxy duplicates first (plan lines 13-19) or MSBuild fails with `MSB6001`.

## 10. Decisions — resolved 2026-09-11

Owner authorisation: the owner delegated these to a human–computer-interaction optimum judgement on 2026-09-11 ("选择人机交互最优解，可以通过独立审核专家判断"). Each decision cites the UX rule or repo convention it follows. §10.1 keeps the original question text for traceability.

| id | Decision | Rationale |
|---|---|---|
| D1 | **Pages-first, and this is the design's own staging rather than an interpretation:** design §22.2's preamble (spec:1141-1143) requests permissions per stage only after the user enters the nearby lobby, and permission refusal keeps single-player working. A1 ships no declarations; each permission is declared in the change that first *uses* it. Rendering follows the corrected §2.1 row: Android and Harmony can truthfully report DENIED from an undeclared query, iOS cannot query at all without the usage key and uses its own static reason key. | Design §22.2 preamble, plus `progressive-disclosure`. Declaring unused BLE/camera permissions adds store-review risk and buys no working flow. **Corrected after review:** the earlier version wrongly implied the permission stage was knowable on all three platforms. |
| D2 | 好友管理 is a **row inside the existing Settings page** that opens a dedicated 好友管理 page. Do **not** add a sixth settings root. | UX rule: `nav-hierarchy` separates primary from secondary navigation; the repo deliberately has five settings roots on all three platforms (`Settings.ets:488-490`, `ios/app/SettingsView.swift:42-51`). Rule: `persistent-nav`. |
| D3 | Harmony gains **no** `en_US`. New strings go into `base` (English) + `zh_CN`, matching the existing convention. | UX rule: `consistency`. A duplicate English locale creates a second source of truth. |
| D4 | (decided earlier, plan slice A0) The shared/session line owns adding `fly_session*` to the three bindings. | ABI must be versioned once and reviewed with the C2/C2b ABI decisions. |
| D5 | Stage precedence is the design's listed pipeline order 权限→发现→认证→Wi-Fi→QUIC→版本→codec. The page marks the **first failing** stage, shows earlier stages as passed, and shows later stages in a **neutral not-yet-reached treatment with no blocked key and no reason**. Once A0 lands the current stage is session-reported. | UX rules: `error-clarity`, `focus-management`. **Corrected after review:** the earlier version rendered those same rows two contradictory ways (here unannotated, in §4 as `nearby.blocked.session_read`), which left the i18n task no single rule. |
| D6 | **No persistent in-game HUD.** The status block lives in the existing pause drawer. The states that demand user action (冻结, 重连倒计时, authority 超时选项) render as a **non-dismissible banner pinned to the top of the run surface**, not a modal. | UX rule `persistent-nav`, plus design §22.4's requirement that network problems show a freeze and countdown rather than silently changing mode or owner. **Corrected after review — the earlier modal was WRONG**: it contradicted §4, was owned by no task in §8, and would have made a frozen end hunt through the drawer for 接管/继续单人/保存结束, the one state that must never require hunting. |
| D7 | In local single-player there is **no nearby status block at all** — not a blocked row. It appears only when a session exists. | No session means nothing to report; permanent in-game chrome for a non-existent session is noise. UX rule: `content-priority`, under the scope reading now written into §4. |
| D8 | 双端确认 is **one primary 确认入局 button per side**, plus a **short pending-config fingerprint rendered identically on both ends and bound into the confirm action**, so "the same pending configuration" is visible rather than assumed. Fields in the fingerprint: seat assignment, authority role, negotiated mode (DUAL/HOST_STREAM), ROM identity, and the selected capability-plan identity. An authority or seat change invalidates confirmation, shows the 确认已失效 banner and re-enables the button. No per-field confirmation. | UX rules: `primary-action` (one CTA per screen), `confirmation-dialogs`, `progressive-disclosure`; design §22.3 (`spec:1167`) requires both ends to see the same pending configuration, which the earlier version proved with nothing. **Strengthened after review.** |
| D9 | Authority-timeout options (接管 / 继续单人 / 保存结束) render as a three-action list. Unavailable options are **shown disabled with a specific reason**. **The reason is a `u8` enum kind carried in the wire/session schema with one translation key per value; the UI renders table text and never wire text.** | UX rules: `disabled-states`, `error-clarity` (cause plus recovery path). **Corrected after review:** "session-reported string with static fallbacks" had no defined ABI shape — the schema has no reason-enum kind, and a free-form wire string would be the first such text to reach the UI, which the A0 ABI does not cover. |
| D10 | The multi-branch **visualisation** (two complete branches) is deferred out of A1 with the recovery/branch-identity slice, because no branch identity exists in the ABI. **But `明确不会自动合并` is an anti-surprise invariant, not a blocked row**, so one always-known line stating that branches are never merged automatically stays in the in-game status surface, at zero ABI cost. | UX rule: `content-priority` for the visualisation; design §22.4 (`spec:1185`) makes the no-auto-merge guarantee a user-facing promise. **Corrected after review:** the earlier version dropped the invariant along with the visualisation. |
| D11 | Stage **names** are static strings; stage **reasons** are session/platform-reported detail with a static per-stage fallback. Each stage therefore keeps both keys. | UX rule: `error-clarity` requires the actual cause and fix, which a static string cannot state. Consistent with D9's enum shape. |
| D12 | iOS acceptance for A1 is **static verification only** — CMakeLists registration, route and `navigationDestination` wiring, localization key-count parity across `en`/`zh-Hans`, and a no-syntax review — explicitly labelled unverified. A macOS/Xcode build is required before any iOS claim. Known constraint to re-verify before scheduling iOS device work: `ios/app` requires the iOS 17.0 SDK, while the only reachable Mac runs Xcode 14.3.1 / iOS 16.4 and therefore cannot build it. | Cannot be built on this machine; the only alternative is an unverifiable claim, which this repo's rules forbid. |
| D13 | 好友/附近设备 is **one page with two tabs** (好友 / 附近设备). | Design §22.1 says 页面分为好友和附近设备. UX rules: `avoid-mixed-patterns`, `bottom-nav-top-level`. **Citation corrected after review:** cite this as platform convention (segmented tabs), not as repo precedent — the existing `GameCenter.ets` Toggle segments are content *filters*, not page tabs, so they are only a loose analogue. |

**Visual style: the generated design-system proposal is not adopted.** `ui-ux-pro-max --design-system` proposed a light rose palette, Orbitron + JetBrains Mono, and a "3D & Hyperrealism" style while listing "3D effects" and "complex shadows" in its own anti-pattern list. A1 uses each platform's existing theme tokens (Android `colors.xml` + `values-night`, Harmony `color.json` and string resources, iOS programmatic SwiftUI). Higher-priority rules `platform-adaptive` and `consistency` outrank introducing a new brand style, and the designed surface is a dark, immersive emulator shell rather than a marketing page.

### 10.1 Original open questions (kept for traceability)

Each of these was a genuine choice the design and the repository leave open. No answer was invented at the time; the tasks in §8 mark where each gate lands.

- **D1 — Which permission declarations ship in A1, and with which identifiers?** Design §22.2 requires BLE discovery, QR scanning and a Wi-Fi path, but A1 can also ship pages-first (every stage then truthfully renders `nearby.stage.permission`). For Android and iOS the standard identifiers follow from those capabilities; for HarmonyOS NEXT the exact permission names and their `reason` strings must be read from the installed SDK. Decide: (a) declare now or pages-first, and (b) the exact identifier set per platform.
- **D2 — Where does 好友管理 live in Settings?** Design §22.1 says the settings page provides rename / delete / block / identity reset, but not whether that is a new settings section, a row inside an existing section, or only a link to the friends-management page. Also: the existing settings roots are deliberately five on all three platforms — does adding a sixth root violate that convention?
- **D3 — May Harmony gain an `en_US` locale?** `base` currently holds English and `zh_CN` holds Chinese, and no other locale exists. Is adding `en_US` in scope, or forbidden?
- **D4 — Who adds the session surface to the three bindings?** `fly_session` is referenced by zero shipping platform files. A1 needs it; plan C2/C2b owns the shared side. Does A1's implementer add the JNI/NAPI/ObjC++ surface and the `Index.d.ts` declarations, or does that stay with the C2 owner? Until this is answered, A1c has no way to read session state at all.
- **D5 — Stage precedence when several stages are simultaneously blocked.** The design lists the seven stages but not what to show when, for example, permissions are granted and the bearer is absent at the same time. Is the listed order the precedence, or is "first failing stage" something the session layer must report?
- **D6 — Persistent in-game HUD or pause-drawer-only?** Design §22.4 says 轻量显示 current mode / quality / seats / pause state. This spec puts them in the existing pause drawer on all three platforms (smallest change, no new overlay host). Is a persistent HUD required instead?
- **D7 — Does the in-game status overlay appear during local single-player play?** §22.4 presupposes a session; there is no session in single-player. Shown as blocked, hidden entirely, or hidden until a session exists?
- **D8 — What is the 双端确认 control?** §22.3 requires both ends to see the same pending configuration and to re-confirm after an authority/seat change, but not what the control is (single confirm button, per-field confirm, auto-confirm with a countdown) or how "the same configuration" is proven to the user.
- **D9 — Which authority-timeout options are shown, and what are the "不可用原因"?** §22.4 requires 接管 / 继续单人 / 保存结束 plus reasons for unavailable ones; the reason taxonomy is unspecified.
- **D10 — Is the multi-branch reunion display (§22.4) in A1?** No branch identity exists in the public ABI. Ship a permanent blocked row, or defer the row entirely?
- **D11 — Are the 7 stage *reason* texts static strings or reported by the session?** This spec defines one static reason key per stage; if the reasons must reflect actual platform error detail, the key set changes.
- **D12 — iOS evidence substitution.** A1c cannot be compiled or verified on this machine. Is a macOS build an acceptance requirement for A1, and if not, what evidence is accepted in its place?
- **D13 — Does A1 include a second 好友/附近设备 page split?** §22.1 says 页面分为好友和附近设备 — one page with two sections, or two navigable pages? This spec assumes one page with two tabs (keys `nearby.tab.friends` / `nearby.tab.devices`).
