# Nearby Multiplayer Certified Bearer Selection Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Deliver the first runnable shared component for the approved nearby multiplayer design: canonical capability validation and deterministic certified bearer intersection.
**Architecture:** Reuse the committed cross-platform shared tree from `88ddf56`. Add a small internal C++17 module beside the existing session wire codec; keep radio operations, authentication, durable plan locking, and the public C ABI out of this component. Selection alone grants no permission to connect.
**Tech Stack:** C++17, existing domain SHA-256, CMake 3.22.1, MSVC 2022, CTest.
**Worktree:** E:/workspace/codes/games/fly-little-games/.worktrees/nearby-multiplayer
**Branch:** codex/nearby-multiplayer
**Approved design:** docs/superpowers/specs/2026-09-04-cross-platform-nearby-multiplayer-design.md, 2026-09-09 revision (main commit 38953b3).

## Scope and dependencies

This is the first bounded implementation slice, not the full M0–M5 program. Existing runtime/checkpoint and session-codec work is reused. The existing session reducer is a stub; its historical “M1 frozen” label does not demonstrate that the full schema or multiplayer works.

| Design coverage | This slice |
|---|---|
| §9.2 PairCapabilitySummaryV1 and plan intersection | Implement and test |
| §10 bearer role separation and one confirmation role | Validate encoded roles and deterministic preference |
| §11 shared explicit wire parsing | Reuse existing wire status/hash helpers |
| §8, §13–20 session/input/media/recovery | Subsequent slices; no runtime behavior changed here |
| §21, §25–27 real device certification | M0a/M5 evidence required; no device connected on this host |
| §30 cross-task ownership | Reuse committed shared tree, preserve other worktree dirty files |

## Task 1: Establish isolated baseline

- [x] Create ignored .worktrees/nearby-multiplayer from committed 88ddf56; do not copy dirty files.
- [x] Bring the approved design revision into this branch using apply_patch.
- [x] Initialize the pinned Nestopia submodule (4470a2e99199d8010322eef4bf680fb3760f6eda).
- [x] Configure, build and run the complete shared baseline: 37/37 CTests passed on 2026-09-09; build succeeded with existing vendor warnings.

```powershell
$cmakeExe = 'C:/Users/pippin/AppData/Local/Android/Sdk/cmake/3.22.1/bin/cmake.exe'
& $cmakeExe -S shared -B .artifacts/nearby-host -G 'Visual Studio 17 2022' -A x64 -DFLYNES_BUILD_TESTS=ON -DZLIB_ROOT=E:/workspace/codes/games/fly-little-games/.artifacts/host-deps/zlib-1.3.1-install -DCMAKE_VS_GLOBALS=TrackFileAccess=false -DCMAKE_TRY_COMPILE_CONFIGURATION=Release
& $cmakeExe --build .artifacts/nearby-host --config Release --parallel 6
& 'C:/Users/pippin/AppData/Local/Android/Sdk/cmake/3.22.1/bin/ctest.exe' --test-dir .artifacts/nearby-host -C Release --output-on-failure
```

Expected: configure/build succeeds; record actual baseline test count. Vendor warnings are baseline evidence, not new module warnings.

## Task 2: Validate and select canonical pair capabilities (TDD)

**Files:**
- Create: shared/src/session/wire/pair_capability.hpp
- Create: shared/src/session/wire/pair_capability.cpp
- Create: shared/tests/test_pair_capability.cpp
- Modify: shared/CMakeLists.txt

Internal namespace: `flynes::session::wire`. Use `std::array<uint8_t,48>` for exact plan bytes, not a serialized C struct. API:

```cpp
using BearerPlanBytes = std::array<std::uint8_t, 48>;
enum class PairSelectionStatus {
    Ok, InvalidInitiator, InvalidResponder, NoCommonPlan
};
Status validate_pair_capability(const std::uint8_t* bytes, std::size_t size) noexcept;
PairSelectionStatus select_pair_plan(
    const std::uint8_t* initiator, std::size_t initiator_size,
    const std::uint8_t* responder, std::size_t responder_size,
    BearerPlanBytes& selected) noexcept;
```

Output `selected` is all zero on every non-Ok return. Callers hash successful exact bytes using the existing `domain_hash("flynes-selected-bearer-plan-v1", ...)`; this module does not certify devices or authenticate transcripts. No new public C symbols or schema registry claims.

Validation contract:
- Exactly 512 bytes, no native struct memcpy. version u16be at0 equals1; platform8 in1..3; count9 in0..8; OS API level12..15 is opaque unsigned value.
- Reserved bytes2..7,10..11,16..31,480..511 are zero. Matrix/profile hashes32..63 and64..95 must each contain a nonzero byte.
- Entries start96, count up to8,48 bytes each; unused entries all zero. Used entries strictly lexicographically increasing by all48 bytes.
- Entry offsets: bearer0,creator1,listener2,codec3,endpoint4,confirmation5,rank6,reserved7,certProfile8..11 big endian,adapterHash12..43,reserved44..47.
- Bearer only AWARE=2,NATIVE_P2P=3,TEMP_WPA2=4. Creator/listener1..2; confirmation0..2. Rank must respectively equal10,20,30.
- Codec1 NETWORK_NAME_PSK only TEMP_WPA2; codec2 PEER_SERVICE only AWARE/NATIVE_P2P. Numeric endpoints1/2 allowed; Apple endpoint3 only NATIVE_P2P,codec2,platformIOS.
- Cert profile ID and adapter hash nonzero; reserved bytes zero. Reject unknown enums, duplicate entries, unsorted entries, nonzero unused slots, and rank/codec/bearer mismatch.
- Selector validates both entire inputs before intersection. Only exact48-byte matches qualify. Choose lexicographic key by entry offsets6,5,0,1,2,3,4,8..11,12..43 (network-order cert ID).
- No matches returns NoCommonPlan. Do not guess fallback, mutate authority/seat, prefer inviter, or trigger radio/system UI.
- Summary support hashes may differ between devices; exact certified plan equality is what matters, so do not require whole summaries equal.

- [ ] Write behavioral tests before implementation. Register the CTest target `flynes_pair_capability`. Start with valid empty summaries and assert no fabricated plan:
```cpp
std::array<std::uint8_t, 512> a{};
a[1] = 1; a[8] = 1; a[32] = 1; a[64] = 1;
auto b = a; b[8] = 3;
BearerPlanBytes out{}; out.fill(0xff);
check(select_pair_plan(a.data(), a.size(), b.data(), b.size(), out)
      == PairSelectionStatus::NoCommonPlan, "empty intersection");
check(std::all_of(out.begin(), out.end(), [](auto x) { return x == 0; }),
      "failure clears selection");
```
- [ ] Run the new target and record RED caused by missing capability implementation (compile/link missing symbol accepted for the first new API).
- [ ] Implement explicit bounded byte validation and the deterministic intersection above. At most8x8 comparisons; no heap allocation or reinterpret_cast to wire structs.
- [ ] Expand tests:0/8/9 plans,511/513 bytes,null data, every reserved region,invalid platform/version/role/rank/codec/endpoint/zero hashes/zero cert ID, duplicate/descending entries, unused entry bytes, disjoint plans, differing contract hash, differing support database hashes, same hashes with differing entry fields, priority/tie-breakers, swapped inputs with matching global roles, repeatability, output clearing.
- [ ] Verify independent literal hash oracle using existing domain_hash on one selected48-byte entry; derive the expected hex separately with Python hashlib in a command, never use the same C++ hash helper as the oracle.
- [ ] Run new target under strict warning settings, full shared CTest and existing session codec/registry regression checks.
- [ ] Commit only these four code/build files after review.

## Task 3: Reviews and execution record

**Files:**
- Create: docs/superpowers/plans/2026-09-09-nearby-multiplayer-progress.md
- Update this plan's checkboxes.

- [ ] Independently inspect implementation against exact offsets, ordering, failure behavior and non-goals.
- [ ] After spec compliance passes, independently inspect quality/security/test coverage; fix findings and rerun affected checks.
- [ ] Record baseline and final counts, git commits, no-device limitation, and actual remaining work. Document cross-task message delivery only if the app reports success.
- [ ] Preserve this implementation branch/worktree for further work; do not merge main or mutate the other worktree.

## Next implementation order

After this slice: authenticated plan lock/confirmation budget → full schema/codegen coverage audit → session lifecycle reducer and durable command execution → real platform pairing/QUIC spike → HOST_STREAM and media timeline → canonical input/recovery → DUAL rollback and one-way downgrade → nine-direction device certification. Public ABI changes require auditing actual consumers first. M0a physical certification remains an independent release blocker throughout.
