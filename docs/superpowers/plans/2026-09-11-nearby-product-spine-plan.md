# Nearby multiplayer — product spine continuation plan

Date: 2026-09-11. Branch `codex/nearby-multiplayer`. Worktree `E:/workspace/codes/games/fly-little-games/.worktrees/nearby-multiplayer`.

Supersedes the "what next" advice in `docs/handoffs/2026-09-11-nearby-multiplayer-handoff.md` §8 only in ordering and granularity;
that document's product decisions, evidence boundary and prohibitions remain authoritative.
Approved design (unchanged): `docs/superpowers/specs/2026-09-04-cross-platform-nearby-multiplayer-design.md`, blob `c88683050f52cb72773917bb6c97573bdddae8af`.

Baseline re-verified on 2026-09-11 (this plan's first round):

- Host build `cmake --build .artifacts/nearby-host --config Release` exit 0; **41/41 CTests passed, 14.07s**. A second, independent clean configure into `.artifacts/build/shared-host` also gave **41/41 passed, 13.71s**.
- `cmake`/`ctest` are NOT on PATH. Use `C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\{cmake,ctest}.exe`.
- **Environment quirk:** MSBuild fails with `MSB6001 … 字典中的关键字:"NO_PROXY"所添加的关键字:"no_proxy"` because this shell exports case-variant duplicates (`HTTP_PROXY`/`http_proxy`/`https_proxy`, `NO_PROXY`/`no_proxy`). Remove the lower-case duplicates before any `cmake --build`:

```powershell
$keys = [Environment]::GetEnvironmentVariables('Process').Keys
$keys | Group-Object { $_.ToUpperInvariant() } | Where-Object { $_.Count -gt 1 } |
  ForEach-Object { $_.Group | Select-Object -Skip 1 | ForEach-Object { Remove-Item "Env:$_" -ErrorAction SilentlyContinue } }
```
- No device attached: `adb devices` empty, `D:\soft\DevEco Studio\sdk\default\openharmony\toolchains\hdc.exe list targets` = `[Empty]`.
- Harmony probe signing still absent: `.artifacts/nearby-quic-harmony-app/entry/build/default/outputs/default/` holds only `entry-default-unsigned.hap`.

## Verified completion snapshot

| Milestone (design §26) | State | Evidence |
|---|---|---|
| M0a disposable vertical slice | Partial. Only Android↔Windows QUIC positive + wrong-SPKI negative. No phone↔phone, no H.264, no secure identity storage. | `docs/acceptance/2026-09-10-nearby-quic-spike.md` |
| M0b runtime | `flynes_runtime.h` frozen; four-port bundle, checkpoint, 12-slot rollback ring exist. | `shared/tests/test_runtime.cpp` |
| M1 session | schema/codec/goldens real; **public session path is stubs**; no product QUIC contract; no fuzz. | see "Session public path" below |
| M2 platform adapters | Host-only fakes + platform-local DTOs. No radio, no Keystore/Keychain/HUKS. | `docs/superpowers/specs/m2-m5-status.md` |
| M3 HOST_STREAM | Not implemented (encoder command returns `UNSUPPORTED_VERSION`). | `docs/superpowers/specs/m2-m5-status.md:45-50` |
| M4 DUAL | Runtime rollback ring only; no input exchange/digest/resync/downgrade transaction. | `docs/superpowers/specs/m2-m5-status.md:52-58` |
| M5 device certification | Not started. | `docs/superpowers/specs/m2-m5-status.md:60-64` |
| Product UI spine | **Zero on all three platforms.** | below |

## Verified product-UI insertion points

| Platform | Game-center host | Insertion point | New pages must be registered |
|---|---|---|---|
| Android | `app/src/main/java/com/flynes/emu/HomeActivity.java:54,89` | `app/src/main/res/layout/activity_home.xml:3-4`; nav wiring `HomeActivity.java:172` | `app/src/main/AndroidManifest.xml:21-55` |
| Harmony | `harmony/entry/src/main/ets/pages/GameCenter.ets:9`, `build()` `:33` | `@Builder navButtons()` `:154-168` | `harmony/entry/src/main/resources/base/profile/main_pages.json:2-9` |
| iOS | `ios/app/CatalogLibraryView.swift:29` | `ToolbarItemGroup(.topBarTrailing)` `:70-81`; `enum LibraryRoute` `:3-6`; `navigationDestination` `:83-90` | add new `.swift` to `ios/app/CMakeLists.txt:41-52` (no Xcode project in tree; CMake requires `-G Xcode`) |

Current permissions are insufficient for any nearby path: Android manifest declares **VIBRATE only**; Harmony `module.json5:36-47` declares **FILE_ACCESS_PERSIST only**; iOS `ios/app/Info.plist.in` has no Bluetooth/Camera/LocalNetwork keys and no `NSBonjourServices`.

All existing nearby code is unreachable dead code: three DTO headers (`app/src/main/cpp/nearby/nearby_dtos.hpp`, `ios/app/platform/nearby/nearby_dtos.hpp`, `harmony/nearby/nearby_dtos.hpp`) plus `harmony/nearby/{nearby_adapter,friend_store}` compiled **only** into `harmony/tests/CMakeLists.txt:141-144` and absent from `harmony/entry/src/main/cpp/CMakeLists.txt`. `flynes_session.h` is referenced by no shipping platform code.

## Session public path — exact current state

`shared/src/session/flynes_session.cpp` — `fly_session_t` (`:9-16`) owns only `last_tick_ns`, `has_tick`, `pair_generation`, `original_context_start_ns` and `initial_plan` (by value). No event/command queue, no codec state, no seat/mode/authority/media state.

| Function | Line | Today |
|---|---|---|
| `fly_session_create` / `destroy` | 151 / 180 | Implemented |
| `fly_session_submit_event` | 185 | Stub — envelope validated, then `return FLY_RESULT_INVALID_STATE;` (`:208`) |
| `fly_session_receive_stream` | 211 | Stub — `return FLY_RESULT_INVALID_STATE;` (`:224`); bytes never reach `wire::check` |
| `fly_session_receive_datagram` | 227 | Delegates to the stream stub (`:232`) |
| `fly_session_poll_command` | 235 | Shape-only — always `kind = FLY_SESSION_COMMAND_NONE` (`:255`), never calls `initial_plan.poll()` |
| `fly_session_complete_command` | 259 | Stub — `return FLY_RESULT_INVALID_STATE;` (`:275`) |
| `fly_session_tick` | 278 | Implemented (monotonic + 60 s attempt deadline) |
| `fly_session_get_snapshot` | 301 | Hardcoded `UI_IDLE` + genesis cursor (`:321-323`) |

The private seam `shared/src/session/session_initial_plan.hpp:40-51` (10 free functions) is **fully implemented** in `flynes_session.cpp:39-102`, but its only callers are tests. The reducer is complete and unreachable from the public path.

`shared/src/session/wire/` is real: `session_codec.cpp` validates + domain-hashes ~30 kinds by string name (`:279-384`); `pair_capability.cpp` is real. Neither decodes into DTOs, and nothing routes `fly_session_receive_*` into the codec.

Tests currently **lock in the stubs**: `shared/tests/test_session.cpp:46` (UI_IDLE), `:55` (COMMAND_NONE), `:62,66-70,78` (INVALID_STATE). Wiring the public path requires deliberately updating these.

Header gap vs design §12 (the product UI cannot be honestly built without these): only `FLY_SESSION_COMMAND_NONE` and `FLY_SESSION_UI_IDLE` exist; no lifecycle states or FROZEN/UNCERTAIN/DOWNGRADE_REQUIRED/RECOVERY_LOCKED flags (§2 table, spec:200-238); `fly_session_event` has no payload (spec:712); no real command kinds or cancel token (spec:714, 221); no `FLY_SESSION_MODE_*`, seat/confirmation/quality fields (spec:717); no media/mute; no friend identity; no side-effect-free size query (spec:525). `transition_id` is `uint64_t` (`flynes_session.h:126,139`) while the wire transition ID is 16 bytes (spec:470, 1021, 1050, 1058).

Shared session tests are **not in CI**: `scripts/ci-check.ps1:228-250` and `.github/workflows/stage0.yml:28-30` configure `core` only. Hypium/ohosTest is absent repo-wide.

## Slice order

Ordering rule: only slices that produce verifiable progress without a device come first; everything device-gated is named as such and not started.

### C1 — probe script preference leak — DONE

Commit `59d633f`. `build-mobile.ps1` no longer leaves the caller's `$ErrorActionPreference` at `Stop`; regression assertion added to `tools/nearby-quic-spike/tests/test_harmony_runner.py:55-59`. RED observed (exit 8), GREEN observed (6/6 Python tests).

### C2 — session public path (shared, offline) — IN PROGRESS

**Blocker discovered 2026-09-11 (must be read before touching the receive path): there is no envelope decoder, so `fly_session_receive_stream` / `_datagram` must stay fail-closed.**

Design §11.3 (spec:443-452) defines the common envelope and lists `family` and `type` among its semantic fields (spec:447); §11.4 (spec:454-464) then defines which content travels on each QUIC channel — the Control channel alone carries HELLO, pair proof, capability, authority/seat proposal+ACK, mode, pause/resume, transition, heartbeat and goodbye/error (spec:458). So a received object's kind is identified by the envelope's **explicit `family`/`type` tag**, not by trying candidate types until one validates.

That decoder does not exist: `shared/src/session/wire/session_codec.cpp` dispatches by an out-of-band type **name the caller supplies** and reads no tag from the bytes. Unknown bytes therefore cannot be honestly classified, and any "try every declared type" helper would bypass channel/family semantics — the exact hazard the fail-closed receive path exists to prevent. Until the envelope decoder is implemented as its own slice, `receive_stream`/`receive_datagram` keep returning `FLY_RESULT_INVALID_STATE`.

Scope actually implemented in this slice, in this order:

1. **Not** wiring the receive path (see blocker above). Negative tests assert fail-closed behaviour for well-formed-but-unauthenticated bytes, wrong channel, truncated/trailing/non-zero-reserved bytes and null input.
2. Bridge `fly_session_poll_command` → `poll_initial_plan_command` and `fly_session_complete_command` → `complete_initial_plan_command`, mapping the local monotonically increasing command id; keep the 64-bit public `transition_id` explicitly unused/zero rather than truncating a wire id.
3. Project the real snapshot in `fly_session_get_snapshot` from `initial_plan_snapshot()` instead of the hardcoded `UI_IDLE`.
4. Implement `fly_session_submit_event` only for what the existing seam genuinely accepts; invent no decoder. Note the open ABI question: `fly_session_event` (header:110-116) has **no payload**, while §12.6 (spec:712) requires user/lifecycle/capability/runtime/media/storage/transport payloads. That is a versioned ABI addition and needs an explicit decision.
5. Preserve every public struct size. Any needed ABI addition is versioned through `struct_size` / `abi_version` per §12.1 (spec:523-528) — never by changing an existing field's width or meaning.
6. Update the stub-locking assertions in `shared/tests/test_session.cpp` deliberately; add new behavioural tests. TDD: show the failing assertion before the fix.

Acceptance: host Release build + full CTest green (≥41 tests, no test deleted); RED→GREEN recorded; Android arm64 and Harmony arm64 cross-compile of `flynes_session` still succeed; public struct sizes unchanged or version-gated.

**Open interpretation to revisit when a real executor/adapter exists (recorded, not resolved):** design §12.6 (spec:715) says `fly_session_complete_command` 重复结果幂等 — duplicate results are idempotent. The public layer as wired returns `FLY_RESULT_INVALID_STATE` for a duplicate completion of an already-completed id, which is idempotent in the sense that no second effect occurs, but is not idempotent in the sense of repeating the same successful result. That behaviour is inherited from the pre-existing reducer (`InitialPlanLock::complete` at `initial_plan_lock.cpp:151-154` rejects a non-pending id via `reject()`, which calls `invalidate()` and fails the attempt closed) and this slice does not change it. It is now documented in the public header. It must be re-decided when the platform adapters get real retry logic, because a retry that treats `INVALID_STATE` as a hard failure would mis-handle a benign duplicate. Do not silently change it before then.

### C2b — wire type identification — **BLOCKED ON A SPEC DECISION (owner input required)**

Verified 2026-09-11 by direct inspection of the schema, the generators, the golden corpus and the codec, plus a hexdump of the golden binaries. **The approved design and the wire schema do not define how a receiver identifies the type of a wire object on any of the 7 application channels.** This is a gap in the approved specification, not an implementation shortfall, so it must not be filled in by an implementer.

Evidence:

| Question | Finding |
|---|---|
| Is there a generic envelope (§11.3 magic/wire_major/wire_minor/family/type/payload_length) in the encodings? | **No.** `flynes_session_v1.schema` top-level keys are `hash_domain_strings, illegal_kinds, kinds, messages, quic_channels, schema_id, unassigned_illegal, unknown_critical_tlv, unknown_optional_tlv, wire_endian, wire_integer_endian` — no envelope/magic/family/payload_length key. Zero grep hits for `magic\|wire_major\|wire_minor\|stream_kind\|prefix` in the schema or the two generators. Spec:445-452 is prose ("按需要包含") with no widths, offsets or order. |
| Do the golden objects start with a type tag? | **No.** All objects begin with their own `version=1 u16be` (`00 01`). `channel_resume_summary_v1/legal.bin` (176 B) reconciles byte-for-byte with spec:470's `version u16be \|\| reserved_zero[6] \|\| session_id[16] \|\| branch_id[16] \|\| …`. The 8-byte opening `00 01 00 00 00 00 00 00` is byte-identical across ChannelBindV1, ChannelResumeSummaryV1 and several kind-tagged objects, so it carries **zero** identifying power. `manifest.json` declares no expected prefix or in-band tag. |
| Does `check()` expect a prefix? | **No.** It validates body-at-byte-0, and the same bytes/size window is validated and hashed (`session_codec.cpp:45-53,175-201,294-299`). Dispatch is a pure string compare on the caller-supplied `type_name` (`:279-284`). |
| What stream-kind prefix values exist? | Only **two**, both for pre-app bind streams written once at stream open: `BindStreamPreambleV1 = magic[4]"FNB1" \|\| version=1 u16be \|\| stream_kind=CHANNEL_BIND(1) u16be` (spec:441, 8 B) and `ReconnectPrebindPreambleV1 = "FNR1" \|\| version \|\| stream_kind=RECONNECT_PREBIND(2)` (spec:439, 8 B). Spec:468's "可靠stream-kind前缀" for Control/StateCommit gives **no** magic, size or value set, and for Input/Bulk/ROM/Video/Audio it does not exist. |
| Is there a per-channel allowed-type mapping? | **No.** Schema `quic_channels` is only `{id, name, form}`. No kind carries a channel field. The six `messages` are explicitly "Not an ObjectKind" and have no kind hex. |

Decisions that only the owner/design revision may make (do **not** invent any of these):

1. Does the §11.3 envelope get a concrete byte layout, and is it in-band for all objects, some objects, or none?
2. What are the stream-kind prefix magic, size and value set for each of Control, StateCommit, Input, Bulk, ROM, Video and Audio?
3. Is the prefix written once per stream or once per object?
4. Are the prefix bytes inside or outside the length and hash window?
5. Which concrete wire types are legal on each channel (specifically: does CanonicalInputBundleV1 belong to Input or State Commit — spec:459 vs spec:460 conflict)?
6. Do datagram channels carry any prefix or in-band discriminator, and if so where?
7. How are the six non-ObjectKind `messages` identified?
8. Is `u32be(record_length) || record` (spec:441) the framing model for **all** reliable streams, or only for the bind streams?

Consequence for the objective: the "raw 接收" half of objective item (2) cannot be implemented honestly until decision 1-4 are made, because a receiver that cannot identify an object cannot enforce authentication or channel/family constraints. The rest of item (2) (poll/complete/snapshot/submit_event fail-closed) is unaffected and is being delivered by C2. Per handoff §3 and design §30, this goes back for centralized review rather than being frozen by this task.

Reusable when the decision lands: body validators/lengths/hashes (`session_codec.cpp:173-201,237-258,329-354`), `Status`/`QuicChannel` (`session_codec.hpp:9-31`), the golden corpus plus `manifest.json`, and the byte recipes in `generate_goldens.py:282-292`. This is also where the 16-byte wire `transition_id` (spec:450, 1021, 1050, 1058) would enter the process, which is the point at which the versioned 128-bit ABI question must be answered.

#### C2b follow-up verification (2026-09-11, second pass)

Four further findings, one of which **corrects an overstatement this plan previously made**:

1. **Correction to my own earlier claim.** I had written that spec:719 forbids the platform adapter from consuming a framing prefix. That is too strong and wrong. spec:719 forbids the adapter from parsing **seat/mode/checkpoint/ROM protocols**; it says nothing about byte-level framing, and §12.6 assigns the prefix to nobody. The accurate word is **unassigned**, not forbidden. Any argument that the adapter *must* hand the prefix through to shared code is therefore not grounded in the design.
2. **`check()` cannot express a prefix at all.** Its signature is `check(const char* type_name, const uint8_t* bytes, size_t size, uint8_t hash_out[32])` (`session_codec.hpp:33-36`) — no base offset. It reads `version` from bytes 0-1 via `be16(bytes)` (`session_codec.cpp:49`) and hashes the same window it validates (`:75,:199,:233,:256`). So a prefix is not merely absent from the data, it is inexpressible in the current API, and whether prefix bytes would be inside the length/hash window is an undecided question, not a detail.
3. **Datagram object types are missing from the schema entirely.** `InputSampleV1` (spec:787, fixed 144 bytes starting `version=1 u16 || reserved_zero[6] || session_id[16]`), `FRAME_BEACON` (spec:805, a bare field list with no size and no header), Video, `AUDIO_DATA` and `AUDIO_XOR_FEC` exist **only in spec prose**; the schema's `messages` set is exactly {FrameCursorV1, EvidenceCursorV1, CanonicalInputBundleV1, ChannelBindV1, ChannelBindProofV1, ChannelResumeSummaryV1}. The three Input families therefore have no in-band discriminator and no schema definition, so the §11.4 Input row is not implementable from the frozen artifacts.
4. **The house idiom already exists elsewhere and was never applied to the 7 QUIC app channels.** `GATTLogicalMessageV1` (spec:312) is `version u8 || type u8 || reserved_zero u16 || body_length u32be || body || logical_hash32`; the pre-bind record (spec:386) is `u32be(1+body_length) || logical_type u8 || exact_body`; `BootstrapEnvelopeV1` (spec:373) is `type u8 || reserved_zero[7] || …`. So "a leading type byte" is this design's established pattern for other transports. **Adopting it for the QUIC app channels would be a new design decision, not the implementation of an existing one**, and must be approved as such.

Because the two fail-closed entry points reject before inspecting bytes, no existing behaviour constrains the choice — the design space is genuinely open.

#### C2b design addendum — approved by the owner 2026-09-11, corrected after independent review

The owner approved adopting the repository's existing in-band type-byte idiom (answer "可以" to the eight questions above). This is a **design addition**, not an edit of the frozen design: the approved spec's blob stays `c88683050f52cb72773917bb6c97573bdddae8af`. It is recorded here for design §30 centralised review before any cross-platform integration depends on it.

**Revision note (two corrections, both found by independent review):** the first version put the kind byte outside the validated-and-hashed window to avoid changing `check()`, which inverts the one security-relevant property of the idiom it claimed to follow. The second version assumed the type field could be an ObjectKind, which cannot carry the six schema `messages` — including `ChannelBindV1` and `ChannelResumeSummaryV1`, the objects pairing depends on most. Items 2, 4, 5, 6 and 7 below are corrected; neither earlier revision is to be implemented.

1. **Scope.** Applies to the 7 QUIC application channels only (Control, Input, StateCommit, Bulk, Rom, Video, Audio). The two pre-app bind streams keep their existing preambles unchanged (spec:439 `"FNR1"`, spec:441 `"FNB1"`).
2. **Framing and the type field.** Every application-layer record is framed as
   `u32be(frame_length) || frame_type u16be || exact_object_bytes`
   where **`frame_length` counts the 2-byte type field plus the body** (`frame_length = 2 + body_len`). This mirrors spec:386's `u32be(1+body_length) || logical_type u8 || exact_body`, where the length field counts the type byte, so a truncated or rewritten type tag is a length error.
   `frame_type` is a **two-namespace 16-bit tag**, both ranges derived from the schema so the schema stays the single source of truth:
   - `0x0001–0xEFFF` — **ObjectKind namespace**: the value is the schema's ObjectKind from its `kinds` entries.
   - `0xFF00–0xFFFF` — **message namespace**: `0xFF00 + index` of the entry in the schema's `messages` array (6 entries today, so `0xFF00`–`0xFF05`). This namespace is required because `ChannelBindV1`, `ChannelResumeSummaryV1`, `ChannelBindProofV1`, `CanonicalInputBundleV1`, `FrameCursorV1` and `EvidenceCursorV1` are explicitly "Not an ObjectKind" and carry no kind at all.
   - Any other value is rejected. The reserved `0xFF00–0xFFFF` range overlaps nothing in the schema today (values run `0x0001`–`0x0308`), but it is a **new design value the approved design does not define**, it must live in one named constant, and it goes to §30 review before cross-platform integration depends on it.
3. **Records per stream.** A reliable stream carries **zero or more** records back to back until end-of-stream and the receiver loops until EOF; a datagram carries exactly one record. A leftover of 1–3 bytes, or a `frame_length` larger than the bytes remaining, is an error. "Reject trailing bytes" therefore means **a partial record**, not "anything after the first frame" — design spec:439 and spec:441 mandate streams carrying six and three back-to-back length-prefixed records, so the naive reading would reject records 2..N of the design's own streams.
4. **`check()` and every existing golden stay unchanged.** The object body is still validated and hashed at offset 0; no base-offset parameter is added and no existing validator is modified.
5. **Classification tamper-evidence is a new, additional hash at the app-frame layer**, not a replacement for the object hash:
   `app_frame_hash = SHA256("flynes-app-frame-v1" || u8 channel || u16be frame_type || u32be body_len || body)`
   **Why it is required:** `ChannelBindV1` (`session_codec.cpp:173-201`) and `ChannelResumeSummaryV1` (`:294-299`) both have an *empty* hash domain and therefore both hash as a bare `sha256(bytes, size)`; a 176-byte input that satisfies both layouts yields a byte-identical hash under two different classifications, and the unprotected kind byte would be what chooses between them. Covering the kind in a framing hash is what makes the classification tamper-evident once bound into a later transcript.
   **This is NOT authentication.** A validated frame is not an authenticated one and nothing here may reach the trusted evidence seam.
6. **Tag → type name** through a table with an explicit namespace column, then the existing `check(type_name, body, body_len)`. The table is **hand-authored and kept honest by two tests**: there is no schema source table to generate it from, because `quic_channels` is only `{id, name, form}` and no kind carries a channel field. Test one asserts the ObjectKind entries are exactly the schema's `kinds`; test two asserts the message entries are exactly the schema's `messages` array in order, so a re-ordering in the schema fails loudly instead of silently remapping wire values. A schema addition carrying per-kind channel membership is follow-on work.
   The schema declares 59 kinds, of which only 29 have both a validator in `check()` and a golden. The table should contain all 59 so that an unvalidatable kind is an explicit `UnknownKind` from `check()` rather than an unidentified tag; the implementer must state which choice was made.
7. **Per-channel allow-list is hand-authored as well** (the earlier "generated from one table" claim is withdrawn — there is no source to generate from), each entry justified from design §11.4. Control and State Commit populated; `CanonicalInputBundleV1` — now selectable as a message-namespace tag — on **State Commit only** (resolving the spec:459/460 contradiction); Input, Video and Audio **empty and explicitly failing closed**, because those objects do not exist in the schema yet.
8. **u16 kind is necessary, with exact evidence:** the schema declares **59 kinds** with values `0x0001`–`0x0308`, and **45 of 59 exceed 255**, so a u8 field is impossible. The schema already encodes ObjectKind as `u16be` at `flynes_session_v1.schema:1201` and `:1884`, so u16be is also the schema's own width for a kind reference.
9. **spec:468's stream-kind prefix is not replaced by the frame kind.** It identifies a stream's family at open; `object_kind` identifies each record's object. The two are complementary, and neither is implemented yet.
10. **Consequence and next work:** `InputSampleV1`, `FRAME_BEACON`, the Video objects, `AUDIO_DATA` and `AUDIO_XOR_FEC` exist only in spec prose and must be added to `shared/schema/flynes_session_v1.schema` with goldens before the Input/Video/Audio channels can be anything but fail-closed.
11. **Cost.** 6 bytes of framing per record, which is small against a 144-byte `InputSampleV1` and negligible against reliable-stream messages.

### C3 — CI coverage for the shared session suite — DONE

`scripts/ci-check.ps1` previously built only `core`, so the shared session/ABI suite could regress silently. Added **Check 4/5 "shared session host test"**: it reuses the canonical toolchain that Check 3 already bootstraps (`$hostTools` / `$hostCMake` / `$hostCTest` / `$hostZlibRoot`, so no extra bootstrap and no extra network), configures `shared` with `FLYNES_BUILD_TESTS=ON` into `.artifacts/build/shared-host`, builds it and requires `100% tests passed`. The Android build was renumbered to Check 5/5.

Verified 2026-09-11: a fresh configure of `shared` with the canonical generator/toolset (`Visual Studio 17 2022`, x64, `ZLIB_ROOT=<pinned zlib 1.3.1 install>`, `TrackFileAccess=false`, `CMAKE_TRY_COMPILE_CONFIGURATION=Release`) succeeded, the full build succeeded, and **41/41 CTests passed in 13.71s** from that clean directory. Script parse check: 0 errors.

Not verified end-to-end: the whole `ci-check.ps1` gate was not run, because Check 3 first bootstraps the pinned zlib/toolchain into `.artifacts/host-deps` of this worktree (that directory is currently absent; the worktree build dirs point `ZLIB_ROOT` at the main checkout's `.artifacts/host-deps/zlib-1.3.1-install`). The verification above substituted that same zlib install, so the only unexercised link is Check 3's pre-existing bootstrap step.

### A0 — session surface on the three platform bindings (DECIDED ownership: the shared/session line, not A1)

Decided 2026-09-11 (this is a work-ownership decision, not a product decision). The A1 spec's open question D4 asked whether the A1 implementer or the session owner adds `fly_session*` to the platform bindings. Answer: **the shared/session line owns it**, as slice A0, because:

- it is ABI work that must stay consistent with `flynes_session.h` and versioned once, not invented independently three times;
- it must be reviewed together with the C2/C2b ABI decisions instead of being frozen by a UI task (design §30);
- it is what makes A1's live-value rendering possible at all.

Verified starting state: **zero** `fly_session` / `flynes_session` references exist in any of the three bindings — Android `app/src/main/cpp/flynes_app_jni.cpp`, Harmony `harmony/entry/src/main/cpp/napi_init.cpp` (+ `cpp/types/libentry/Index.d.ts`), iOS `ios/app/bridge/FlyNesAppBridge.{h,mm}`. `FlyNesAppBridge.h:10-37` exposes only create/settings/controlLayout/catalog/scan.

Ordering: A0 depends on C2 (and on the C2b decision only for anything that consumes received bytes). A1 tasks that lay out pages and render blocked/empty states do **not** depend on A0 and may proceed; A1 tasks that render a live session value must wait for A0.

### A1 — three-platform entry + friends/nearby + pairing + lobby + in-game status pages

Decomposed per platform so the three can proceed independently: A1a Android, A1b Harmony, A1c iOS.

Each platform slice delivers: the "附近联机" entry on the game-center screen; pages for 好友/附近设备, 配对, 大厅, 游戏中状态; and the permission/Info.plist declarations the path needs. Every page must render the **actual** session/platform state, and every unimplemented capability must display the exact blocking stage with its reason (design §22.2 last bullet: 权限/发现/认证/Wi-Fi/QUIC/版本/codec) — no fake friends, no fake connections, no navigate-only buttons (handoff §8.3).

Acceptance per platform: that platform's build command succeeds and the new pages are registered in its navigation mechanism; all new user-visible strings exist in that platform's i18n resources.

### Device-gated (do not start until devices are attached and Harmony signing is configured)

- M0a phone↔phone QUIC (needs 2 phones + signed `com.flynes.nearbyprobe`).
- M2 real BLE/QR/Wi-Fi bearer + Keystore/Keychain/HUKS.
- M3 HOST_STREAM, M4 DUAL, M5 nine-direction matrix, p95 latency gates (§25.3).

## Rules carried forward

- No merge, no push, no force-clean. Preserve `harmony/build-profile.json5`, `harmony/entry/oh-package-lock.json5`, `harmony/.clang-tidy`, `harmony/.clangd` and `docs/acceptance/2026-09-09-nearby-device-audit.md` exactly as found.
- Do not weaken the approved design; ABI conflicts go back to §30 review instead of being frozen unilaterally.
- Do not advertise 三端联机首版 while M5 is unpassed.

## A1 — three-platform product-UI spine — implemented 2026-09-13

Delivered A1a (Android), A1b (HarmonyOS) and A1c (iOS) against `2026-09-11-nearby-ui-spine-slice-spec.md`, plus the A1-8 i18n set. The approved design and its normalized blob are untouched. Nothing here is a live session value: every page renders the exact stage or blocked reason that is missing, with no fake friend, peer, session or connection.

Deliverables. Android: `NearbyFriendsActivity`/`NearbyPairingActivity`/`NearbyLobbyActivity`/`NearbyFriendsManageActivity`, the shared `NearbyStagePipeline` renderer and `view_nearby_stage_pipeline` include, `NearbyInGameStatus` (drawer rows + D6 banner), the game-center entry, the Settings 好友管理 row (D2) and the manifest registrations. Harmony: `NearbyService.ets` (pure decision logic + `nearbyString` resolution), `NearbyFriends`/`NearbyPairing`/`NearbyLobby`/`NearbyFriendsManage` pages, `pauseDrawer` status block and run-surface banner in `RunGame.ets`, Settings row, `main_pages.json` registration. iOS: `NearbyFriends`/`NearbyPairing`/`NearbyLobby`/`NearbyFriendsManage` SwiftUI views, route + toolbar + `navigationDestination` wiring, `CMakeLists.txt` registration, pause-drawer rows and the banner in `RunSurfaceViewController.mm`, Settings row. All six locale files carry the full §2 vocabulary.

Two vocabulary additions beyond §2, both page titles, recorded here because they are the only ids not in the spec's tables: `nearby.pairing.title` and `nearby.lobby.title`. Rationale: a sub-page titled with the feature's own name is a hierarchy defect, and the 附近设备 tab needs a label for the one navigate-only entry §4 permits. The anonymous-join control set (D4/§4 line 188) is deliberately **not** built, and 大厅 deliberately has no navigate-only entry, because §4 permits that exception only into 好友/附近设备 and 配对.

Evidence from this session (commands actually run, newest last):

| Check | Command | Result |
|---|---|---|
| Shared host CTest | `cmake --build .artifacts\nearby-host --config Release` + `ctest -C Release` | **46/46 passed**, 15.32 s |
| Harmony host CTest | same, `.artifacts\nearby-harmony-host`, `--config Debug --parallel 1` | **12/12 passed**; Debug is required because two suites use bare `assert`, which Release strips via `NDEBUG` |
| Android unit tests | `gradlew :app:testDebugUnitTest` | **427 tests, 0 failures, 0 errors** |
| Android nearby UI, emulator | `gradlew :app:connectedDebugAndroidTest -Pandroid.testInstrumentationRunnerArguments.class=…Nearby*` (UTP, emulator-5554) | **19/19 passed** |
| Android full e2e, emulator | `gradlew :app:connectedDebugAndroidTest -P…notClass=…MotionComputeParityTest,…MotionShadowPresenterTest,…GlCapabilityProbeInstrumentedTest` | **113 run, 111 passed, 2 failed** — both fail inside their own `getUiAutomation()` setup step, see below |
| Harmony package | `hvigorw assembleHap -p product=default -p module=entry@default -p buildMode=debug` | exit 0, `entry-default-signed.hap` |
| Harmony Hypium, emulator | `hdc … aa test -b com.flynes.emu -m entry_test -s unittest OpenHarmonyTestRunner` | **31/31 passed** |
| Versioning regression | `Invoke-Pester tools\versioning\tests\Versioning.Tests.ps1` | **5/5 passed** |

Explicitly **not** verified: iOS compiles nothing on this machine (no Xcode, no Apple SDK, no generated project). A1c evidence is static only — file registration, route wiring, key-count parity (94 nearby keys in each locale, key sets identical) and a syntax review. The iOS binary, the Harmony page rendering beyond the Hypium-covered service logic, and any human walkthrough of the pages remain unverified.

The two Android e2e failures are pre-existing and independent of this slice. `SettingsMasterDetailTest.twoHundredPercentFontKeepsMasterTargetsVisible` and `FirstRunNavigationTest.largeFontKeepsStatusCardAndCtaFullyVisible` both call `shell("settings put system font_scale …")` as their **first** statement, and that call throws `IllegalStateException: Not connected!` / `…already registered!` from `UiAutomation`, so neither test ever reaches a layout assertion; the run then dies at teardown with `Cannot call disconnect() while connecting UiAutomation`. The three excluded classes need EGL/GL, which this emulator does not provide (`EGL_CONTEXT_UNAVAILABLE`). Emulator animations must be off (`window_animation_scale` etc. = 0) or the haptic RecyclerView scroll assertion fails on its own.

Version alignment. The worktree was on `1.0.4` while the mainline is `1.1.4`, and Harmony was still at `1.0.0` inside that same worktree. Root cause: this branch's `tools/versioning/Versioning.ps1` predates mainline `abcde1b`, whose Harmony regex tolerates the trailing comma plus CRLF that `harmony/AppScope/app.json5` actually has — so `Sync-Version.ps1` matched Android and silently skipped Harmony. `VERSION` is now `1.1.4`, all three platform metadata sets are synchronized, and `abcde1b`'s fix plus its Pester regression test are ported so every later commit here keeps them in step. This was a targeted port of one mainline commit, not a merge: the branch is still 15 commits behind mainline.
