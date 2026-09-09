# HarmonyOS parity TDD emulator evidence — 2026-09-10

Source revision at the start of the final gate: `88ddf5636c8149293a326f8f6699929e03f05502`
(the gate metadata records the dirty implementation tree that is committed after verification).

## Gate result

| Stage | Result | Evidence |
| --- | --- | --- |
| Host | PASS, CTest 12/12 | `out/evidence/completion-host-final/` |
| HarmonyOS Phone emulator | PASS, Hypium 20/20 | `out/evidence/completion-harmony-emulator-final/` |
| Android API 35 emulator | PASS, instrumentation 99/99 plus unit/package | `out/evidence/completion-android-emulator-final/` |

The Android run completed as one full instrumentation invocation. No Launcher3 ANR or test
isolation workaround was needed in the final run.

## Measured HarmonyOS emulator behavior

- A 30-file batch containing 61,440 valid ZIP members was imported into the managed test
  library. The files contained duplicate ROM payloads, so canonical catalog deduplication kept
  the visible count at 9 games.
- The cancel control became observable 1,133 ms after requesting a rescan. The cancellation
  request returned at 1,366 ms and the UI reported `scan cancelled` at 2,220 ms. The old source
  snapshot remained at 9 games and `ready` after a cold process restart.
- Ten rescan/navigation rounds returned to Game Center successfully. Back-command response was
  226–244 ms, every round kept the same process ID, and a separate run entered Settings while
  scanning.
- Debug fixture cleanup removed 31/31 fixture ZIPs in 1,075 ms. A sandbox listing confirmed zero
  matching fixture files remained, while the source remained at 9 games and `ready`.
- The 30-minute render/audio run kept one process alive for 1,802 seconds: 60.0988 source fps,
  no present failures, no audio drops, no audio callback lock misses, and zero post-fallback
  underflows. The emulator rejected 120 Hz and the runtime explicitly reported an observed
  60 Hz fallback. FAST audio accumulated 14 startup/stall underflows before switching to NORMAL;
  the post-fallback count remained zero.

Raw timelines and status snapshots are in `out/evidence/harmony-emulator/`, including
`scan-cancel-rescan-timeline.txt`, `scan-navigation-10-rounds.txt`,
`debug-cleanup-assertions.txt`, `audio-final-30m-start.json`, and
`audio-final-30m-end.json`.

## Qualification boundary

T0–T8 are complete for the tested host and emulator configurations. T9 remains unqualified until
both physical devices are connected. Emulator results do not certify physical 90/120 Hz modes,
touch-to-core latency, GPU budgets, Motion latency, A/V skew, thermal protection, or the device
FAST audio path. Advanced options continue to expose observed capability and fallback reasons;
they must not be unlocked as device-certified from this evidence alone.

Build warnings retained for follow-up are the SDK 13 availability of `setFontSizeScale` and
`packToData` under compatible SDK 12, deprecated router/dialog calls, exception-analysis warnings,
and phone-dependent folder-selection capabilities. None failed the tested emulator flows.
