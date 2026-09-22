# Nearby landscape frontend restoration

## Frontend integration into main (2026-09-22)

- At the user's request, only the approved frontend/test delta and this evidence document were
  applied to the existing `main` working directory. This is a working-tree integration, not a Git
  merge commit; existing main debugging changes remain uncommitted and unstaged.
- The 31 frontend/test files match the accepted worktree content (four XML files gained a final
  newline). No historical backend snapshot, build/signing configuration, or version metadata was copied.
- SHA-256 aggregate checks across 2,915 other tracked/untracked files confirmed that their contents
  were unchanged. Main HEAD and the index tree were unchanged as well.
- Android `:app:assembleDebug` and Harmony `assembleHap` both passed in the main working directory.
  Only integration builds and diff checks were run; no expanded font/device matrix was repeated.
- Cleanup requested afterward: the Git worktree registration was removed. Before removal, all 32
  frontend/test/document files and 580 evidence files were backed up and hash-verified under
  `.artifacts/ux-integration/ux-worktree-archive-20260922/` (`source/` and `ux-evidence/`).
  Git removal hit Windows long paths; the remaining `.worktrees/ux-landscape` directory could not
  be moved because it was in use. Residual files still require cleanup after the owning process
  releases them. The historical branch was retained. Main sources were not removed.
  Integration build logs are in main's ignored `.artifacts/ux-integration/` directory.

The sections below record the earlier isolated implementation and device acceptance.

## Scope and isolation

- Worktree: `.worktrees/ux-landscape`; branch: `codex/nearby-landscape-ux`.
- During isolated implementation and acceptance, main was not edited or merged into.
- `8423cbb` is a LOCAL snapshot of the then-uncommitted main wiring, not a backend change authored for this UX task.
  Review/apply the frontend delta **after this snapshot**, not the entire branch versus main.
  Main may have progressed independently; reconcile its current frontend wiring before integration.
- Native/shared code, session owners, network interfaces, parameters, QR payload and pairing lifecycle remain unchanged from that snapshot.
- Android host creation/regeneration/cancellation and Harmony Scan Kit join retain their original implementations.

## Presentation

- Original design reference: `docs/archive/nearby-2026-09-21/docs/superpowers/specs/assets/nearby-ui-parity-review.html`.
  Its colors, typography hierarchy, underline tabs and rounded controls are retained.
  Historical capabilities are not reintroduced.
- Entry: proportional landscape columns, aligned bottom actions, minimum 48 dp/vp touch areas.
- Invitation: readable body-sized status, square QR constrained by available height, cancel left / primary action right.
  No platform-specific test instructions appear in the actual invitation.
- Lobby: game / host / seat cards with fixed footer; technical details are paged, not expanded into a scrolling document.
  Dynamic confirmation status remains visible.
- Management: two-by-two action grid. Unsupported actions show the localized “暂不支持” message.
- Short-height / large-font layouts remove redundant decorative copy before sacrificing essential content.
  No vertical ScrollView/Scroll is used for these page bodies.

## Verification

- Red evidence: old Android layout clipped the regenerate action and styled failure messages as a 29 sp, .17 em invitation code.
  `.artifacts/ux-evidence/baseline-tests.log`.
- Android debug app and instrumentation build: PASS.
- Android unit tests: 526, zero failures/errors.
- Targeted Android emulator instrumentation: 28/28 PASS.
  Includes QR decoding, original confirmation guards, actual Activity bounds, and a 72-case layout matrix:
  four layouts × zh-CN/en × font scales 1/1.3/2 × usable widths 640/736/844 dp at 312 dp usable height.
  Lobby matrix includes its summary content and visible waiting-confirmation status.
- Dedicated emulator only: `emulator-5580`, 2800×1260 at 560 dpi for final native screenshots.
  This matches the inspected Android device's pixel dimensions/density, but is not physical-device certification.
- Harmony host CTest: 14/14 PASS.
- Harmony app and Hypium HAP compilation: PASS; compilation alone does not establish device layout correctness.
- Independent read-only review identified hidden lobby confirmation feedback; the final UI retains it.
- Compared Android pairing code from `onDestroy()` onward and Harmony pairing methods from
  `aboutToAppear()` to `build()` with `8423cbb`: unchanged.
- `git diff --check`: PASS.

## Physical-device acceptance

- User explicitly authorized both device acceptance and temporary reuse of the existing local test signature.
- Device: HBN-AL80, system-reported OpenHarmony-6.1.1.120, landscape 2844×1260.
- Existing 1.7.11 installation was replaced with the 1.8.1 worktree test app using `hdc install -r`.
  Both app and test HAP installation succeeded. No uninstall or data clear was performed.
- Hypium: **7/7 PASS, Failure: 0, Error: 0** on the physical device.
  Entry, scan page, management grid, lobby summary and paged details were exercised at font scales 1/1.3/2.
  Tests check control bounds and 48 vp minimum target heights without swiping.
- Fifteen native PNG screenshots were saved (five views × three font scales).
  Native screenshots were inspected for clipping, alignment and legibility.
  A separate physical tap screenshot confirms “暂不支持” feedback.
- Device-only findings fixed: border assignment overwrote some button corner radii;
  value-passed ArkUI builder labels became stale after tab/detail state changes.
  Added real-device label assertions failed **6/6** before the fix and passed afterward.
- The first broad test registration crashed while eagerly loading an unrelated game's raw-resource fixture,
  before Hypium's class selection. The test-only `-s nearbyUxOnly true` option registers only the UX suites.
  No product resource loader, backend, or unrelated gameplay fixture was changed to bypass that issue.
- Temporary signing configuration was restored in a `finally` block; the worktree has zero tracked
  signing entries, and the main signing-profile file hash was unchanged.
- Font scale was returned to 1 and the device left on the Nearby entry page.
- This is a **local test signature**, not a store/production certificate. Installation was verified only on
  this connected compatible device. No camera capture, real QR pairing, or two-device gameplay is certified here.

Reproduction after configuring an authorized local test signature:

```text
hdc -t <target> install -r harmony/entry/build/default/outputs/default/entry-default-signed.hap
hdc -t <target> install -r harmony/entry/build/default/outputs/ohosTest/entry-ohosTest-signed.hap
hdc -t <target> shell aa test -b com.flynes.emu -m entry_test -s unittest OpenHarmonyTestRunner -s nearbyUxOnly true -s timeout 60000 -w 60
```

Installed artifact provenance: local snapshot `8423cbb` plus the uncommitted frontend/test delta
in this worktree, version 1.8.1. Do not merge the snapshot's backend files over newer main work.

| Artifact | SHA-256 |
| --- | --- |
| entry-default-signed.hap | 6086BD69D5DC4851E53910B01A84AC91FF2CE1AA0332F868838C442F57685196 |
| entry-ohosTest-signed.hap | 4CD1BBF8BDFB0B50A99010E531D036B83CE2C7ACFCC05E3D12DEF87AC3FF4BFA |

## Android physical-device acceptance (2026-09-22)

- User narrowed acceptance to **1x font scale only**, normal page layout and actions. No further
  enlarged-font matrix, full-platform sweep or two-device gameplay was pursued after that instruction.
- V2324A / Android 16, landscape 2800x1260, 560 dpi. System font scale remained 1.0 throughout.
- Same-signature `adb install -r` installed worktree 1.8.1; local debug signature, not production signing.
  No uninstall/data clear. All 69 original app files remained present; 65 hashes unchanged, four updated
  during app startup/use. This is file-presence evidence, not a claim that every preference is byte-identical.
- Native screenshots and actual taps verified entry tabs/management navigation, create invitation/QR,
  regenerate/cancel, management's four unsupported actions, lobby confirmation feedback and details
  next/previous/cancel. All four nearby pages fit at 1x without scrolling or clipped controls.
- Lobby was opened by a temporary test-only foreground launcher, not by claiming a completed network
  pairing. Its real Activity bounds/no-scroll assertion passed. The helper and log are preserved only
  in ignored evidence (`NearbyLandscapeTest-physical-review.java`, `android-device-lobby-1x.log`);
  the manual wait helper was removed from the normal regression source.
- Broad ActivityScenario runs did not complete on this device: app-initiated background launches did
  not reach the foreground. Launching the exported HomeActivity from shell first allowed the bounded
  lobby check. Do not report those interrupted runs as passing or as product crashes.
- Initial test APK contained only x86_64 test-native libraries. Built the same existing test CMake target
  for arm64 into ignored generated output, then repackaged. No backend/build source change was needed.
- Before scope was narrowed, a 2x English narrow-layout assertion exposed a clipped lobby status;
  card vertical padding was reduced from 16dp to 12dp. The installed final layout was checked at 1x;
  enlarged-font physical acceptance is explicitly not claimed.
- App build/unit run succeeded. Final app APK SHA-256:
  `43D33353EF9CCFE4A07CA7CA88204C7AEBCADA01ACC661502C63DB3A7D4EBC52`.
- Evidence: `.artifacts/ux-evidence/screens/Android-device-{entry,invite,manage,lobby,unsupported}-1x.png`
  and `Android-device-details-{next,previous}-1x.png`. Device left on nearby entry at 1x.
- Main and backend interfaces remain untouched. No merge or commit was performed.

## Local evidence

Evidence and generated packages remain ignored. Logs and native screenshots:

- `.artifacts/ux-evidence/android-build-final.log`
- `.artifacts/ux-evidence/android-ux-tests-final.log`
- `.artifacts/ux-evidence/host-tests-final.log`
- `.artifacts/ux-evidence/harmony-build-final.log`
- `.artifacts/ux-evidence/harmony-test-build-final.log`
- `.artifacts/ux-evidence/harmony-signed-build.log`
- `.artifacts/ux-evidence/harmony-signed-test-build.log`
- `.artifacts/ux-evidence/harmony-device-ux-tests-final.log`
- `.artifacts/ux-evidence/harmony-label-regression-red.log`
- `.artifacts/ux-evidence/screens/Harmony-{entry,scan,manage,lobby,details}-{1,1.3,2}.png`
- `.artifacts/ux-evidence/screens/HarmonyUnsupported.jpeg`
- `.artifacts/ux-evidence/screens/NearbyFriendsActivity.png`
- `.artifacts/ux-evidence/screens/NearbyPairingActivity.png`
- `.artifacts/ux-evidence/screens/NearbyLobbyActivity.png`
- `.artifacts/ux-evidence/screens/NearbyFriendsManageActivity.png`

This UX verification does not claim two-device gameplay completion or production/store signing.
