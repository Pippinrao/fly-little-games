# iOS simulator readiness execution

Approved by the user on 2026-09-10. Continue the existing iOS implementation;
do not recreate the product. First qualify on the simulator, then connect an
iPhone for hardware qualification. Preserve macOS 13.0.1.

## Baseline

- Source branch: `codex/ios-harmony-port`, snapshot `091dfd8`.
- Work branch: `codex/ios-simulator-ready`; independent local worktree.
- Previously uncommitted shared source-removal and Chinese ZIP fixes are now
  committed in the source snapshot. No outstanding core/shared/iOS source
  changes remained to copy. Harmony local config and agent reports stay there.
- Mac: SSH `apple`, Intel MacBookPro14,1, 8 GiB RAM, macOS 13.0.1.
- Space rechecked: 68 GiB available. No cleanup performed by this task.
- Xcode download requires interactive Apple authentication. User asked to
  download Xcode 14.3.1 from Apple to the Mac's Downloads directory.

## Work checklist

- [x] Create isolated worktree and initialize the pinned core submodule.
- [x] Install/verify CMake, Xcode 14.3.1 and iOS 16.4 Simulator on apple.
- [x] Adapt product to Swift 5.8 / iOS 16.4 and x86_64 simulator; keep arm64 device support.
- [ ] Wire continuous emulation, Metal presentation, PCM audio, input and lifecycle.
- [ ] Complete catalog, persistent directory access, scanning, external ROM launch, favorites and recents.
- [ ] Complete Android-equivalent settings, layout editing, localization and licenses.
- [ ] Complete spatial Metal passes and preserve advanced-quality qualification gates.
- [ ] Run host regression, real Xcode builds, simulator UI and playback tests.
- [ ] Run 30-minute gameplay and ten pause/settings/library/relaunch cycles.
- [ ] Capture logs/screenshots and report feature-level evidence; then qualify on iPhone.

## Acceptance and boundaries

Match existing Android functionality and layout/interaction semantics using the
existing native iOS skin, shared app/product/runtime contracts and persistence.
No nearby multiplayer or public distribution in this phase. A successful
source-contract test or unsigned build is not playable-app evidence. Simulator
checks must demonstrate frames advancing without input, audible PCM, external
ROM import, persisted saves/settings/sources, correct cancellation and failure
handling. Hardware haptics, real multi-touch and high-refresh qualifications
must be separately verified on the later iPhone run.

## Known baseline defects

The run view steps only on changed input, does not instantiate its renderer or
pacer, and lacks a system audio output. Sources is a placeholder. Play allows
only a name-matched builtin. Several SwiftUI APIs require iOS 17. Build accepts
only arm64. ScaleFX pass 3 and the multi-pass render graph are incomplete.

## Progress evidence

Add actual command results, revisions and artifact locations as work finishes.
Never label an unexecuted simulator/device check as passed.

## Verified progress and device target, 2026-09-10

- Xcode 14.3.1 build14E300c, verified app signature, first-launch components,
  iOS16.4 simulator and CMake3.31.8 installed on apple.
- Full product build/install/launch succeeded on x86_64 iPhone14 simulator.
- Latest runtime/input XCTest:9 tests passed. Tests cover continuous video/PCM,
  checkpoints and bounded audio queues, plus multi-touch and direction ownership.
  This does not establish audible output or complete input parity.
- Game Center changed from list/detail navigation to Android's 30/70 split,
  selected detail and horizontal two-row cards, with persistent selection/query/category.
  Sources now appears within the Game Center with34/66 split and close action.
- Latest UI suite:2 tests passed (selection keeps grid visible with launch on left,
  source open/close, game launch,3 pause/resume cycles, return and app relaunch).
  Mac evidence logs:build/ios-home-build.log and build/ios-home-run.log.
- Settings/layout/localization/license implementation committed9b2389f and0f5b4d2;
  review found controls-reset and haptic-preview behavior still needing corrections.
- Input review fixes in progress; hardware controllers and keyboard remain to audit.
- User clarified target simulator AND physical device are iPhone16ProMax.
  Current apple is MacBookPro14,1 (2017), macOS13.0.1. simctl currently has only
  iPhone14-generation device types and iOS16.4 runtime. Therefore the target has
  NOT been switched or tested. iPhone16-generation simulation needs Xcode16+
  and a supported newer macOS; Xcode16 requiresmacOS14.5+, and the2017MacBookPro
  is outside Apple's official Sonoma compatibility list. A compatible Mac and
  the phone's actual iOS version are pending user input. Existing simulator
  results remain baseline regression only, not iPhone16ProMax acceptance.

Remaining acceptance: Android title/cover metadata parity, settings behavioral
fixes, input review closure, real picker import/restart/error checks, actual
played audio proof, permanent video-failure behavior,30-minute gameplay,
10 complete pause/settings/library/relaunch cycles, target-device simulation,
then physical-device qualification. No percentage or completion claim yet.

User follow-up: continue on the existing simulator and ensure the final app is
compatible with iPhone16ProMax. New-Mac availability no longer blocks baseline
implementation. Real16ProMax testing remains a later qualification step. Keep
layout based on view bounds and system safe areas, arm64 device output and
compatible deployment target. Do not infer120Hz qualification from the model.

## User-requested checkpoint, 2026-09-11

Stop after this stage, write handoff, and commit/push the work branch. Final
build and19runtime/input/audio tests PASS;3UI tests including10complete
pause/settings/library/reopen cycles PASS.9Python iOS checks andMacMetal GPU
check PASS. Shared-host37/38PASS with documented zlib golden-encoder mismatch.
Title/cover helpers in de7676a are tested but deliberately NOT wired into app.
Authoritative next-session handoff:ios/docs/2026-09-11-stage-handoff.md.
No30-minute gameplay or iPhone16ProMax qualification claimed. Keep worktree.
