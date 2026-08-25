# FlyNES Display Quality and Power Modes Implementation Plan

> **Execution rule:** use `subagent-driven-development` or `executing-plans`, and apply `test-driven-development` to every production change. Do not expose a mode until its build, capability, complete-configuration qualification and evidence gates all pass.

**Goal:** ship honest user-selectable power/quality modes, remove redundant rendering, add real pixel-art reconstruction, verify whether native-time 120Hz provides useful latency benefit, and keep experimental 60→120 motion compensation impossible to activate without exact vivo X100 Pro evidence.

**Frozen architecture decisions:**

1. Requested preferences, current constraints, effective configuration and runtime measurements are separate objects.
2. `DisplayQualityResolver` is the only component that produces `EffectiveVideoConfig`. Thermal logic and renderer failures feed constraints into it; they do not create a second effective state.
3. Balanced is profile-dependent: current builds resolve it directly to 60Hz/Native/Sharp with no fallback; a qualified complete configuration may resolve it to 60Hz/Native/MMPX.
4. Certification matches a complete `VideoConfigurationKey`: core-confirmed `SourceTiming`, resolution/mode, refresh, temporal, spatial, post effect and aspect, plus build, GPU/driver and implementation hashes. Separate mode sets may not be combined, and NTSC evidence may not be reused for PAL/UNKNOWN.
5. Before advanced rendering, replace `GLSurfaceView` with one `SurfaceView` plus one native-owned EGL presenter for every video mode. Java and native never own the same Surface concurrently.
6. Native-time 120Hz remains source-driven at the core-confirmed rate (NTSC about 60, PAL about 50 new buffer submissions/s). Swappy is introduced only when NTSC interpolation genuinely submits about 120 buffers/s.
7. Missing hardware or evidence is `UNVERIFIED`, never `PASS`.

## Delivery gates

- **Increment A — contracts, truth and safety:** Tasks 1–3. Internal integration checkpoint only; not releasable on the old continuously swapping renderer.
- **Increment B — unified presenter and efficiency:** Task 4. Must visually match the current renderer before old GL ownership is removed; after this, base-mode certification may start.
- **Increment C — spatial reconstruction:** Task 5. MMPX/ScaleFX remain unavailable until complete-configuration qualification.
- **Increment D — native-time 120Hz:** Task 6. Formal availability requires device evidence; ordinary AVD tests do not assert 120Hz.
- **Increment E — interpolation and certification:** Tasks 7–8. Experimental lab build first, final Release second.
- **Base Release acceptance:** Tasks 1–4, Task 8.1–8.2, the independent base-only matrix in Task 8.3a, the base rows of Task 8.4–8.6 and all Task 9 gates must pass before Eco/current Balanced can ship. Tasks 5–7 are not dependencies; their missing rows are `NOT_INCLUDED` and remain locked.
- **Advanced Release acceptance:** the applicable Task 5, 6 or 7 increment plus Task 8.3b–8.6 and Task 9 must pass separately for every advanced complete configuration entering that Release.

All native, Gradle and evidence commands are separate gates. Do not join them with PowerShell `;`, because a later successful command can hide an earlier failure.

## Task 0: Freeze reproducible inputs before production edits

**Files**

- Commit this spec, implementation plan and HTML prototype first.
- Create `docs/design/display-quality/reference/` for exported 2340×1080 and 1280×720 prototype PNGs plus metadata (viewport, font scale, state, SHA-256).
- Create `docs/acceptance/display-quality/preflight.md`.
- Create ignored `local-data/display-quality/rom-fixtures.json` from committed `tools/quality/schemas/rom-fixtures.schema.json` and `tools/quality/examples/rom-fixtures.example.json`; create `docs/acceptance/display-quality/rom-fixtures.redacted.json` with only non-path hashes/instructions. Never commit ROM bytes, source paths or device URI values.
- Create `tools/quality/bootstrap_host_zlib.ps1` plus `tools/quality/tests/BootstrapHostZlib.Tests.ps1`. It resolves and pins the Windows host toolchain, downloads the immutable zlib 1.3.1 release archive into ignored `.artifacts/host-deps/`, verifies SHA-256 `9a93b2b7dfdac77ceba5a558a580e74667dd6fede4585b91eefb60f03b72df23`, extracts it, configures a static Release build, and installs headers/library to `.artifacts/host-deps/zlib-1.3.1-install`. Never use the moving `current/` URL or an unverified system DLL for the Windows host gate.
- Create `tools/quality/invoke_pester_gate.ps1` and `tools/quality/tests/InvokePesterGate.Tests.ps1`. The wrapper imports exactly Pester 3.4.0 with `RequiredVersion`, rejects any other loaded version, recursively resolves only `*.Tests.ps1`, invokes the explicit files via `-PassThru`, and exits nonzero when discovery returns zero tests or when `FailedCount` is nonzero. Its own subprocess tests prove that a passing fixture returns 0 while failing and empty fixtures return nonzero. Every Pester command below goes through this wrapper; no gate relies on `Invoke-Pester`'s host-process exit behavior.

- [ ] **0.1 Capture immutable baselines**

Export current app renderer PNGs on a named fixed AVD before replacing GL ownership. Export prototype states for Current Balanced, Custom expanded, locked Extreme, system fallback, thermal fallback, risk dialog and 2.0 font. Record source commit and hashes; HTML itself is not an Android dp golden.

- [ ] **0.2 Audit upgrade history now**

Inventory schema 0/1/2/3 keys and previously shipped APK/certificate digest. If the prior signed APK is unavailable or uses a different certificate, mark the upgrade path `BLOCKED_BY_MISSING_ARTIFACT` before implementation; do not discover this only at final install. Clean-install work may continue.

- [ ] **0.3 Freeze external algorithm inputs**

ScaleFX source is pinned to `libretro/glsl-shaders@4f4eb801b2dbcaed0a9669a9deec1a098f3623d8`, files `scalefx-pass0.glsl` through `pass4`, with the revision notice. Name the independent CPU oracle `mmpx-cpu-oracle-v1`; Task 5 must commit and hash the oracle before writing the GPU port.

The Windows host dependency input is `https://github.com/madler/zlib/releases/download/v1.3.1/zlib-1.3.1.tar.gz` with the hash above. The bootstrap script runs CMake with `BUILD_SHARED_LIBS=OFF`, Release configuration and an explicit install prefix, then records the archive, extracted tree, installed header and static-library hashes plus license in the preflight manifest. Linux CI may use the distro package only as an additional compatibility job; the canonical host evidence uses the pinned archive/install.

The canonical Windows host toolchain is CMake/CTest 3.22.1 from `<sdk.dir>/cmake/3.22.1/bin` plus generator `Visual Studio 17 2022` and architecture `x64`. `bootstrap_host_zlib.ps1` accepts optional absolute `-CMakeExe`/`-CTestExe`; otherwise it parses `sdk.dir` from `local.properties`, resolves both executables, verifies they are the same 3.22.1 distribution, verifies that the generator and x64 MSVC compiler can configure a probe, and then uses those exact values for zlib. It emits ignored `.artifacts/host-deps/host-toolchain.psd1` containing absolute executable paths, generator, architecture and multi-config mode. Missing paths, wrong versions, unavailable generator/compiler, CMake/CTest directory mismatch, or an existing build directory created with a different generator/architecture are fatal. Pester covers every negative case. The preflight manifest records CMake/CTest versions and hashes, generator, architecture, detected MSVC compiler id/version and the generated toolchain manifest hash. All later host commands import this file and invoke absolute paths; no gate calls a PATH-dependent bare `cmake` or `ctest`.

- [ ] **0.4 Freeze test content identities**

Resolve the exact local variants for From Below, Super Mario, Contra, Tetris and River City Ransom. Record canonical game id, variant hash, region/source timing, fixed save-state hash and scene instructions in the ignored local manifest; validate it against the committed schema and generate the exact redacted manifest path listed above. Certification cannot start while any sample is ambiguous.

- [ ] **0.5 Commit the frozen design/preflight assets**

```powershell
pwsh -NoProfile -File tools/quality/invoke_pester_gate.ps1 -Path tools/quality/tests/InvokePesterGate.Tests.ps1
```

```powershell
git add docs/superpowers docs/prototypes docs/design docs/acceptance .gitignore tools/quality/bootstrap_host_zlib.ps1 tools/quality/invoke_pester_gate.ps1 tools/quality/tests/BootstrapHostZlib.Tests.ps1 tools/quality/tests/InvokePesterGate.Tests.ps1 tools/quality/schemas/rom-fixtures.schema.json tools/quality/examples/rom-fixtures.example.json
git commit -m "docs: freeze display quality design inputs"
```

## Task 1: Add atomic settings storage and the requested-preference contract

**Files**

- Modify `app/src/main/java/com/flynes/emu/settings/SettingsStore.java`.
- Modify `app/src/main/java/com/flynes/emu/settings/SharedPreferencesSettingsStore.java`.
- Create `app/src/main/java/com/flynes/emu/settings/SettingsBatch.java`.
- Create the enums and records under `app/src/main/java/com/flynes/emu/video/quality/`: `VideoQualityPreset`, `PhysicalRefreshPolicy`, `TemporalMode`, `RuntimeTemporalState`, `SpatialMode`, `PostEffect`, `CustomVideoSettings`, `VideoPreferences`.
- Modify `AppSettings.java`, `SettingsKeys.java`, `SettingsRepository.java` and every current caller of `filterMode()`/`refreshMode()` in the same task.
- Create `app/src/test/java/com/flynes/emu/settings/VideoSettingsMigrationTest.java` and extend existing settings tests.

- [ ] **1.1 Write failing storage and migration tests**

Cover schema 0/1/2/3 using the preflight key inventory, including every historical spelling of Nearest, Sharp, Edge Enhanced and CRT; fixed 60/90/120 and AUTO; aspect mode; adaptive default. Also cover schema 4 with a missing/corrupt key and a simulated interrupted legacy write. Assert:

- legacy values migrate to `CUSTOM` without silently selecting a new algorithm;
- `AUTO` becomes visible `LEGACY_AUTO_INTEGER_MULTIPLE`;
- CRT becomes Sharp + CRT and keeps aspect/refresh;
- all schema-4 keys and schema number are committed in one batch;
- a failed batch leaves the old snapshot readable and the next load safely retries;
- Balanced stores only the preset plus preserved Custom values, not a hidden MMPX request.

- [ ] **1.2 Add a real batch contract**

`SettingsStore.commit(SettingsBatch)` must map to one `SharedPreferences.Editor` and one synchronous `commit()` for this small settings record. The fake store can fail before commit to test crash recovery. No caller may save the six video fields with separate `apply()` calls.

- [ ] **1.3 Implement schema 4 and migrate all compile-time callers**

`VideoPreferences` contains `preset`, preserved `CustomVideoSettings`, and `adaptiveProtection`. Presets ignore Custom axes; switching back to Custom restores them. Keep deprecated compatibility accessors only if needed for one incremental commit, and remove their production callers before this task ends.

- [ ] **1.4 Run gates**

```powershell
.\gradlew.bat :app:testDebugUnitTest --tests com.flynes.emu.settings.VideoSettingsMigrationTest --no-daemon
```

```powershell
.\gradlew.bat :app:testDebugUnitTest --no-daemon
```

Expected: all prior and new JVM tests pass; no half-written schema can be observed.

- [ ] **1.5 Commit**

```powershell
git add app/src/main/java/com/flynes/emu app/src/test
git commit -m "feat: add atomic video quality preferences"
```

## Task 2: Build one complete-configuration resolver and constraint state machine

**Files**

- Create under `video/quality`: `SourceTiming`, `DisplayModeCapability`, `DisplayObservation`, `GlCapabilities`, `DisplayCapabilities`, `BuildAlgorithmAvailability`, `VideoConfigurationKey`, `EvidenceLevel`, `EvidenceValidityPolicy`, `EvidenceInvalidReason`, `PhysicalScanEvidenceState`, `PersistedEvidenceClockState`, `BootSessionIdentity`, `BootSessionIdentityProvider`, `AuthenticatedTimeSource`, `AuthenticatedTimeSample`, `EvidenceExpiryTombstone`, `PersistedEvidenceClockAnchor`, `AuthenticatedReleaseTime`, `TrustedEvidenceClockSnapshot`, `TrustedEvidenceClock`, `EvidenceValidityWatchdog`, `CertifiedVideoConfiguration`, `DeviceIdentity`, `DeviceQualityProfile`, `AdaptiveTransition`, `AdaptiveTriggerClass`, `AdaptiveQualityPolicy`, `FallbackReason`, `RuntimeFailure`, `RuntimeConstraints`, `SessionSafetyDirective`, `EffectiveVideoConfig`, `MotionRiskConsent`, `DisplayQualityResolver`.
- Create under `video/power`: `ThermalBand`, `ThermalPowerSnapshot`, `ThermalPowerMonitor`, `AdaptiveQualityController`, `TemporalTransition`.
- Create `app/src/main/assets/device-quality-profiles.json` with an empty `certifiedConfigurations` list.
- Create tests under `app/src/test/java/com/flynes/emu/video/quality/` and `video/power/`.

- [ ] **2.1 Write failing resolver truth-table tests**

Construct full `DisplayModeCapability` records instead of integer-Hz sets. Assert:

- current Balanced + no MMPX build resolves to 60Hz/Native/Sharp, `APPLIED`, no fallback;
- MMPX code present but unknown GL capability still resolves to Sharp;
- exact 60Hz/Native/MMPX compatibility-and-power entry resolves Balanced to MMPX;
- separate Motion and ScaleFX qualifications do not unlock Motion + ScaleFX;
- changed modeId, resolution, aspect, CRT, build hash, GPU/driver or algorithm hash fails closed;
- changed `profileId`, evidence-manifest hash or validity interval fails closed; a certificate is valid immediately before `validUntilEpochMs` and invalid at exactly that instant and afterward;
- `DEVICE_LAB` validity may not exceed 180 days and `COMPATIBILITY_AND_POWER` may not exceed 365 days; zero/reversed intervals, an interval longer than its policy, any future-issued profile and an expired profile are rejected, while a future issuance over 24 hours or a trusted-clock rollback/jump anomaly additionally yields `TIME_UNTRUSTED`; none can resolve to an advanced tuple;
- a fresh install, cleared app data, corrupt/partial anchor or new boot without fresh platform-trusted network time resolves every certificate-backed tuple to `TIME_BOOTSTRAP_REQUIRED`; base Nearest/Sharp remains available;
- current-boot time advances from the immutable anchor by `elapsedRealtime`; repeated 23-hour wall-clock rollbacks, process restarts and Resolver calls cannot freeze it, and an existing certificate deadline can never move later;
- after expiry has been observed, wall-clock rollback cannot reactivate the tuple; after uninstall/state loss, a new fresh trusted-time bootstrap must independently rediscover that expiry before any advanced tuple can unlock;
- Extreme cannot be activated from UI, deep-link-shaped input or migrated preferences without one exact `DEVICE_LAB` configuration;
- a runtime shader/FBO failure is returned as a typed fallback and selects the next qualified complete configuration;
- fixed 90 requested and system-active 60 is fallback, while FOLLOW_SYSTEM + active 60 is applied;
- stale or previous-generation display observations cannot affect a new request;
- Motion remains Immediate Native/0ms until a fresh same-generation compatible 119–121Hz active-mode observation has been stable for three seconds;
- an active Motion session reacts to the first fresh incompatible active-mode observation immediately with Buffered Native Hold; the three-second threshold only marks persistent fallback and suppresses request thrash;
- `StatusFreshness` is derived at consumption time from monotonic `now - observedAt` with a 1500ms TTL; a missing observation, three missed 500ms heartbeats, `STALE`, `UNKNOWN` or clock anomaly immediately sends active Motion through `TemporalTransition` to Buffered Native Hold with `DISPLAY_OBSERVATION_STALE`;
- NTSC-qualified Motion cannot activate for PAL or UNKNOWN source timing;
- 119999/120000 float representations match only through canonical milliHz/tolerance, never `float` record equality;
- severe/42°C requests 60Hz/Native/Nearest without claiming the system accepted 60; critical emits `PAUSE_FOR_CRITICAL_THERMAL`.
- every stable resolver result returns an atomic `resolvedConfigurationId + resolvedConfigurationKey` pair containing core-confirmed `SourceTiming`; transitional/unqualified output returns neither, never one without the other.

- [ ] **2.2 Write failing hysteresis and priority tests**

`AdaptiveQualityController` outputs only `RuntimeConstraints` and `TemporalTransition`; it has no `effective()` method. Its profile is an atomic `configurationId → configurationId` graph with a GPU budget per id and a recorded reverse recovery stack. One trigger walks exactly one edge; a simultaneous temporal/spatial change is one explicit edge. Test this priority:

Graph nodes are stable qualified configurations only. `BUFFERED_NATIVE_HOLD`, `SURFACE_SUSPENDED_HOLD`, `PRIMING`, `PRIMING_SHADOW` and `DRAINING` are transient `TemporalTransition` states on an edge and never receive a profile `configurationId`, certification entry or recovery-stack slot.

1. Critical pause.
2. Severe or battery ≥42°C safe configuration.
3. Battery saver or battery ≤15% (`SYSTEM_BATTERY_SAVER` and `LOW_BATTERY` are distinct).
4. Optional Moderate/40°C/GPU-budget degradation only when adaptive protection is enabled.

`DisplayObservation` is not an override tier and never creates a recovery-stack edge. It is the final external fact constraint: after every request produced by the priority list, resolver rechecks the same-generation active mode; incompatibility or lease loss can make the request partial/unsafe but can never “win over” or erase a battery/thermal policy.

Recovery timing starts when all recovery conditions first become true. Require Thermal ≤Light, battery >20%, battery temperature <38°C and GPU under the configuration budget for 120 continuous seconds; pop one recorded edge at a pause/scene-cut boundary. Include Sharp → Nearest in the conservative graph.

- [ ] **2.3 Implement pure resolution and fail-closed profile loading**

`BuildAlgorithmAvailability`, GL capability, core-confirmed `SourceTiming`, fresh same-generation `DisplayObservation` and exact profile qualification are independent inputs. Balanced expansion picks Sharp or MMPX atomically and does not emit a fake fallback. Candidate downgrade edges may reach ScaleFX → MMPX → Sharp → Nearest, but resolver skips any node lacking build/capability/complete-configuration qualification. Output retains requested display mode and system-reported active mode separately and emits the resolved id/key in one immutable snapshot; a key always includes `SourceTiming` and cannot be reconstructed from filenames or UI fields. Baseline Nearest/Sharp use deterministic `builtin:<canonical-key-sha256>` ids strictly for residency identity; tests prove this does not elevate `PhysicalScanEvidence` or unlock an advanced mode.

`DeviceQualityProfile.profileId` identifies the profile; each `CertifiedVideoConfiguration` stores `configurationId`, the full key and evidence identity plus `certifiedAtEpochMs` and `validUntilEpochMs`. ISO date strings are display-only and never used for comparison. `EvidenceValidityPolicy` allows at most 180 days for `DEVICE_LAB` and 365 days for `COMPATIBILITY_AND_POWER`. The deterministic baseline Nearest/Sharp `builtin:` identities do not carry a certificate and are exempt from profile expiry, but that exemption cannot be inherited by any advanced configuration.

`PersistedEvidenceClockState` plus the nullable `PersistedEvidenceClockAnchor` form one atomic, MAC-protected contract: `NONE` means no record, `COMPLETE` means one fully valid record, and any partial/checksum/MAC/range/boot-id ambiguity is `CORRUPT`. The complete anchor carries platform-derived boot-session identity, anchor evaluated epoch, anchor `elapsedRealtime`, nondecreasing `lastSeenValidatedEpochMs` and a nondecreasing expired-certificate tombstone set. Callers cannot provide a `persistedPairFromCurrentBoot` boolean: `TrustedEvidenceClock` derives same-boot status from `BootSessionIdentityProvider`. A true `NONE` state is allowed only as bootstrap input and cannot by itself validate advanced evidence.

On a fresh install, after app-data loss/reinstall, and once per new boot, certificate-backed features require a fresh `ANDROID_NETWORK_TIME` sample from the platform authenticated-time source; an ordinary editable wall clock is never sufficient to bootstrap. If the platform source is unsupported/unavailable, advanced rows resolve to `TIME_BOOTSTRAP_REQUIRED` and stay visibly locked while deterministic builtin Nearest/Sharp remains usable. A valid sample atomically creates the complete current-boot anchor. During that boot, `monotonicFloor = anchorEvaluatedAtEpochMs + (elapsedRealtimeNow - anchorElapsedRealtimeMs)` and `evaluatedAtEpochMs = max(authenticatedNetworkNowEpochMs when available, systemWallEpochMs, releaseBuildEpochMs, lastSeenValidatedEpochMs, monotonicFloor)`. Negative/overflowing elapsed deltas, a platform-time rollback, or wall/monotonic divergence beyond 24 hours is `TIME_UNTRUSTED`; `lastSeen` and the anchor epoch only move forward. Repeated sub-24-hour wall rollbacks therefore cannot stop `monotonicFloor`.

Every certificate gets a current-boot monotonic expiry deadline derived once from that anchor. A later observation may shorten the deadline but the same `(profileId, configurationId, evidenceManifestSha256, validUntilEpochMs, bootIdentity)` deadline is never recomputed later; process restart restores the earlier persisted lower bound and derives a deadline no later than before. At expiry, atomically persist the nondecreasing tombstone before emitting `EVIDENCE_EXPIRED`. The tombstone prevents rollback while app state survives; it is not claimed to survive uninstall. State loss forces a new trusted-time bootstrap, whose sampled epoch must be checked against `validUntilEpochMs` before any new anchor/profile qualification is written, so clearing data cannot make editable wall time revive an expired certificate.

`PhysicalScanEvidenceState` has only `VERIFIED` and `UNVERIFIED`. `VERIFIED` is legal iff clock trust is `TRUSTED`, `invalidReason == NONE`, all identities match and `certifiedAt <= evaluatedAt < validUntil`; every other combination is `UNVERIFIED` with exactly one non-`NONE` reason. Unknown serialized values fail closed. When failures overlap, the evaluator uses the spec's exact first-true order shared by UI/tests: certificate missing → profile → configuration/full key → build/driver → algorithm implementation → manifest → evidence level → invalid interval → excessive TTL → corrupt anchor → expiry tombstone → bootstrap required → time untrusted → future-issued → expired. Parameterized pairwise tests require the same first reason.

Profile JSON rejects unknown fields, duplicates, malformed hashes, invalid/overlong validity intervals and any profile already expired under the trusted clock. Profile state cannot be elevated by runtime conditions, a user benchmark or wall-clock editing. Tests cover fresh bootstrap, complete and partially corrupt anchor, repeated 23-hour rollback, process/device restart, cleared data/reinstall, no trusted time, trusted-time recovery, persisted expiry tombstone, deadline non-extension, one millisecond before/equal/after expiry, TTL maxima, future issue and arithmetic overflow.

- [ ] **2.4 Implement bounded platform signal sampling**

Register/unregister Thermal listeners with Activity lifecycle. Sample thermal headroom no more than every ten seconds, handle NaN/unsupported APIs, and never infer watts. Add battery and system-power events. Keep all monotonic timing in-process; after process restart status is unavailable until fresh samples arrive.

`EvidenceValidityWatchdog` reevaluates on process start, Activity resume, profile/configuration changes, before every resolver use and at least every 60 seconds, and owns the non-extendable monotonic deadline at the nearest `validUntilEpochMs`. Expiry, `TIME_BOOTSTRAP_REQUIRED`, `CLOCK_STATE_CORRUPT` or `TIME_UNTRUSTED` immediately locks advanced UI; active Motion enters the existing Buffered Native Hold/drain transaction, while an advanced spatial-only configuration atomically resolves to a qualified builtin baseline.

- [ ] **2.5 Run gates**

```powershell
.\gradlew.bat :app:testDebugUnitTest --tests "com.flynes.emu.video.quality.*" --tests "com.flynes.emu.video.power.*" --no-daemon
```

```powershell
.\gradlew.bat :app:testDebugUnitTest --no-daemon
```

- [ ] **2.6 Commit**

```powershell
git add app/src/main/java/com/flynes/emu/video app/src/main/assets/device-quality-profiles.json app/src/test
git commit -m "feat: resolve qualified video configurations centrally"
```

## Task 3: Replace ambiguous status and build the approved settings UI

**Files**

- Create under `video/status`: `StatusFreshness`, `PhysicalScanEvidenceState`, `PhysicalScanEvidence`, `VideoRuntimeStatus`, `VideoStatusAccumulator`, `VideoStatusRepository`, `DisplayCapabilitiesReader`, `DisplayStatusMonitor`, `GlCapabilityProbe`.
- Create injectable `DisplayPlatformFacade` and Android implementation under `video/platform/`.
- Create `app/src/main/java/com/flynes/emu/settings/MotionRiskConsentStore.java` and JVM tests.
- Modify `DisplayModeSelector`, `DisplayModeController`, `MainActivity`, `SettingsActivity`, `SettingsFragment`.
- Create `DisplaySettingsFragment`, `fragment_display_settings.xml`, `item_video_preset.xml`, `view_video_runtime_status.xml`.
- Delete old `settings/DisplayStatus*` only after all callers migrate in this same task.
- Add localized strings and pseudolocale support.
- Create `DisplayQualitySettingsTest`, `VideoStatusAccumulatorTest`, `DisplayCapabilitiesReaderTest`, `GlCapabilityProbeTest`, and `app/src/androidTest/java/com/flynes/emu/video/GlCapabilityProbeInstrumentedTest.java`.

- [ ] **3.1 Write failing metric tests**

The accumulator records separately: surface epoch, display request generation, core-confirmed SourceTiming/nominal fps, core-produced fps, copied unique fps, texture-upload fps, skipped source sequences, synthesis-slot submissions, motion-warped slots, app buffer submissions, requested full mode, system-reported active full mode, runtime temporal state, video/audio delay, queue depths, transition, cadence adjustments and freshness. Presenter publishes `activeConfigurationId + activeConfigurationKey` atomically from the actually bound temporal/spatial/post/aspect/pacer transaction; the accumulator may not join independent callbacks to invent a key. Transitional/emergency/resource-rebuild output publishes neither and invalidates a timed sample. `onBufferSubmitted()` must not be named “presented”. Native-time status derives expected submissions from source timing (NTSC about 60, PAL about 50) and never hard-codes 60.

Sequence gaps must distinguish “core produced more frames” from “app did not copy/upload them”. A one-time `Display.getMode()` sample is system-reported state, never physical-scan evidence. `PhysicalScanEvidence` is derived on demand from the currently matched profile/configuration and includes `profileId`, `configurationId`, full key, build/driver fingerprints, build/algorithm implementation hashes, evidence-manifest hash, capture/certification/valid-until/evaluated epochs, clock trust, state and typed invalid reason; never persist a standalone VERIFIED flag. Tests flip each identity independently and expect `UNVERIFIED`, and exercise the exact before/equal/after-expiry and `TIME_UNTRUSTED` boundaries.

- [ ] **3.2 Probe display and GL capabilities honestly**

Filter display modes by exact current width/height and retain modeId plus canonical milliHz. Every mode request increments a generation; monitor observations include requested/active full modes, generation, monotonic observation time and stable duration, while freshness is derived on every read rather than stored. While Motion is requested, priming or active, `DisplayStatusMonitor` polls the system-active mode every 500ms in addition to listener events; its lease expires at 1500ms. Process pause, display removal, unregister, read failure or time anomaly publishes `UNKNOWN` immediately. `GlCapabilityProbe` creates a bounded EGL pbuffer, records ES2 properties, attempts and destroys a separate ES3.1 context, then publishes UNKNOWN → KNOWN. Context loss invalidates runtime resources and triggers revalidation.

- [ ] **3.3 Build the high-fidelity page and generic Fragment routing**

`SettingsActivity` must hold a generic `Fragment`; Display uses `DisplaySettingsFragment`, other sections keep `SettingsFragment`. Remove Display preferences from the old preference screen rather than adding an extra nested click.

Match the approved prototype: four radio cards, current-build/qualification/runtime states kept separate, current Balanced shown as fully applied Sharp, locked Extreme focusable with an explanation, Custom axes, risk dialog, 3-second display-lease waiting/Priming states, physical-scan `UNVERIFIED` label, buffered/surface-suspended Native states and audio delay. The confirmation button only stores consent and starts a new display generation; it never labels Motion active before the fresh-lease and Priming gates finish. `MotionRiskConsentStore` accepts only an exact `profileId` + `configurationId` + full configuration key + evidence-manifest hash + evidence certification/valid-until/evaluated epochs + build/algorithm hashes + risk-copy version; every mismatch invalidates consent. On the confirmation click it must discard the dialog-open snapshot, obtain a new `TrustedEvidenceClockSnapshot`, re-match the current profile/certificate/key, set both `acceptedAtEpochMs` and `evidenceEvaluatedAtEpochMs` to that one click snapshot's `evaluatedAtEpochMs`—never `System.currentTimeMillis()`—and save only through a compare-and-set on the same profile generation. Consent is valid only while `certifiedAt <= consent.evidenceEvaluatedAt == acceptedAt <= currentEvaluatedAt < validUntil`, and is rechecked through `TrustedEvidenceClock` on every Motion request. Tests independently change `profileId`, every key field, each hash, copy version and validity identity, force the generation CAS to fail, and test a lagging wall clock, dialog crossing expiry, profile replacement while the dialog is open, just-before/equal/after expiry, future issue, bootstrap loss and clock rollback. UI, migrated preferences and deep-link-shaped input call the same gate. Use 48dp targets, fixed AVD PNG references, 1.0/1.3/2.0 fonts, Chinese/English/pseudolocale and TalkBack order.

The status card shows user request, protection-adjusted request and system-reported active mode in separate fields. Custom controls may expose algorithms present in the build, but every resulting tuple still goes through exact configuration qualification; an unqualified Cartesian combination is visibly partial and resolves to one qualified tuple. All preset/custom power labels use one qualitative estimator, so identical effective tuples always show the same level.

- [ ] **3.4 Add lifecycle-safe status observation**

`VideoStatusRepository` is thread-safe and in-process only (for example an `AtomicReference` plus lifecycle observers). Register Display listeners only while visible, except the lifecycle-owned 500ms Motion heartbeat required while a game session is requested/priming/active. A dedicated session-safety scheduler owns one replaceable `(surfaceEpoch, requestGeneration, deadline)` token and invokes `TemporalTransitionController` at 1500ms even if the monitor/poll thread is blocked; compute and presenter also recheck the same atomic lease before every synthesis and Swappy swap. A fixed request mismatch >1Hz for three seconds emits persistent `SYSTEM_OR_DEVICE_POLICY`; FOLLOW_SYSTEM has no numeric request. This UI debounce must not delay the immediate Motion safety transition on the first incompatible observation or on the 1500ms freshness deadline. Unit tests advance a fake monotonic clock across 1499/1500ms, drop three heartbeats, stall the read executor and scheduler independently, change epoch/generation and inject read failure; no cached `FRESH` flag or queued synth/swap may survive. Do not infer thermal reason in the display monitor.

- [ ] **3.5 Run gates**

```powershell
.\gradlew.bat :app:testDebugUnitTest --tests "com.flynes.emu.video.status.*" --no-daemon
```

```powershell
.\gradlew.bat :app:connectedDebugAndroidTest -Pandroid.testInstrumentationRunnerArguments.class=com.flynes.emu.DisplayQualitySettingsTest --no-daemon
```

```powershell
.\gradlew.bat :app:connectedDebugAndroidTest -Pandroid.testInstrumentationRunnerArguments.class=com.flynes.emu.video.GlCapabilityProbeInstrumentedTest --no-daemon
```

```powershell
.\gradlew.bat :app:lintDebug --no-daemon
```

Expected: ordinary AVD tests use simulated capabilities and do not require a 120Hz panel.

- [ ] **3.6 Commit**

```powershell
git add app/src/main/java/com/flynes/emu app/src/main/res app/src/test app/src/androidTest
git commit -m "feat: add truthful display quality settings"
```

## Task 4: Publish frames once and migrate to one native EGL presenter

**Files**

- Modify `core/include/nes/nes.h`, `core/src/nes_core.cpp`, `core/tests/test_core.cpp`.
- Modify `AudioPump`, `AudioThread`, `NesCore`, `nes_jni.cpp` and the input bridge used by `InputRouter`/`MainActivity`.
- Create `FrameStepResult`, `FrameAvailableSignal`, `FrameDispatchExecutor`, `FrameBufferPool`, `FrameLease`, `FrameCopyResult`, `GameSurfaceView`, `NativeVideoPresenter`, `ClockDomainCalibrator`, `InputLatencyTracker` under `app/src/main/java/com/flynes/emu/video/`.
- Create `app/src/main/cpp/video/egl_presenter.h/.cpp`, `presentation_coordinator.h/.cpp`, `baseline_pipeline.h/.cpp`, `video_metrics.h/.cpp`. `PresentationCoordinator` is the sole per-surface-epoch owner of frame-rate votes and direct/paced swap selection; Task 7 extends it with Swappy rather than adding a second caller.
- Modify `app/src/main/cpp/CMakeLists.txt`.
- Modify `core/CMakeLists.txt` so the Windows host gate takes `ZLIB_ROOT` as the **installed prefix**, resolves only `${ZLIB_ROOT}/include/zlib.h` and the Release static library under `${ZLIB_ROOT}/lib` with `NO_DEFAULT_PATH`, creates an imported target and rejects a missing/version/hash-mismatched dependency. It never treats the extracted source tree as linkable and never silently continues after lookup fails.
- Create `tools/quality/bootstrap_host_zlib.ps1`, `tools/quality/invoke_pester_gate.ps1` and `tools/quality/tests/BootstrapHostZlib.Tests.ps1` in Task 0; Task 4 only consumes those frozen tools.
- Remove `GlFrameView` and Java `FrameRenderer` only after baseline parity passes.
- Extend `NativeFrameSourceTest`, `AudioPumpTest`, `EmulationSessionTest`; create `FrameDispatchExecutorTest`, `ClockDomainCalibratorTest`, `InputLatencyTrackerTest`, `NativePresenterIntegrationTest`.
- Create the shared, mode-agnostic certification harness now: runtime-retained `app/src/androidTest/java/com/flynes/emu/test/DeviceCertification.java`, `SingleDeviceCertificationRunner.java` requiring one explicit verified serial, and `BaseVideoCertificationTest.java` for Native60/Nearest and current Balanced/Sharp. This harness must not import or require Task 5, 6 or 7 classes; later tasks only add optional cases.

- [ ] **4.1 Write ABI compatibility and no-change tests first**

Increase the ABI minor and snapshot struct version. Add explicit `NES_WARN_NO_VIDEO_CHANGE = 7` and region enum values. New `nes_copy_video_frame_if_new` returns:

- `NES_OK`: copied pixels and full metadata;
- `NES_WARN_NO_VIDEO_CHANGE`: destination bytes and sequence remain untouched;
- negative `nes_err`: failure.

Use `offsetof` and caller `struct_size` before writing every tail field. Test old-size snapshots, new-size snapshots, undersized structures and the existing copy function. Do not require `struct_size >= sizeof(new_snapshot)` from old callers.

Also add versioned `nes_input_sample` plus `nes_get_last_input_sample()`: input generation, sampled pad bits and native monotonic timestamp written at the core's actual pad-read boundary. Host tests prove the generation changes only when sampled, not when Java/native setter is called.

- [ ] **4.2 Define a non-ambiguous Java bridge**

JNI returns an integer status and fills metadata only on `NES_OK`; Java maps it to `FrameCopyResult.NEW/NO_CHANGE/ERROR`. Do not overload a sequence-valued `long` with errors. Keep native and Java monotonic timestamps in separate domains unless an explicit calibration sample exists.

- [ ] **4.3 Add the real frame-arrival chain**

Replace the ambiguous sample-count result with `FrameStepResult(framesRun, audioSamples, sequence, sourceTiming)`. After a completed frame, invoke `FrameAvailableSignal(sequence)`. Native mode may coalesce pending work to the latest sequence, but must increment skipped-sequence metrics; its gate is “each copied sequence at most once”, not “every core sequence copied”. Add a dispatcher policy boundary now so Task 7 can switch Motion to a lossless bounded ring. There is no draw → poll → requestRender cycle.

The DirectBuffer pool uses leases. Presenter enqueue copies into an owned native staging ring before returning and releasing the lease. Cover capture makes its own immutable copy. No observer may retain a two-buffer `PublishedFrame` across future polls.

Add an input generation to every pad-state change. Core records the generation and native monotonic timestamp when it actually samples pad bits. `ClockDomainCalibrator` uses paired JNI samples to map Java elapsed time to native time and rejects windows with >1ms uncertainty. `InputLatencyTracker` reports touch-to-core only from calibrated samples; Java write time is not the endpoint.

- [ ] **4.4 Migrate every mode to one Surface owner**

`GameSurfaceView extends SurfaceView` forwards create/change/destroy to `NativeVideoPresenter`. Native code owns EGLDisplay, context, EGLSurface, swap and all shaders. Start with a pixel-identical port of Nearest/Sharp/legacy-edge/CRT and new-frame-driven swap. `PresentationCoordinator` assigns a monotonically increasing `surfaceEpoch`, is the only code allowed to call `ANativeWindow_setFrameRate`, and direct-swaps only new source frames. Java may request `preferredDisplayModeId` but never calls `Surface.setFrameRate` on the game Surface. Link `EGL`, `GLESv2`, `android` and `log`; do not add Swappy yet.

In Task 4, where Motion does not yet exist, Surface destroy blocks new enqueue, cancels the Native-only bounded queue, clears active id/key, destroys EGL resources and releases `ANativeWindow`; recreation starts a new epoch and atomically reapplies resolver id/key. Implement this as a typed lifecycle transition, not an unconditional queue-clear hidden in the presenter: Task 7 must replace the Motion branch with `SURFACE_SUSPENDED_HOLD`, frozen A/V queues, a new display generation and the three-second fresh-lease recovery transaction. Tests already assert epoch changes and reject callbacks/votes from an old epoch. Only after screenshot parity and lifecycle tests pass may Java GLSurfaceView classes be removed.

- [ ] **4.5 Configure and run host gates from a clean checkout**

Bootstrap the exact host dependency first; the script is idempotent and rechecks the toolchain, archive, extracted-tree and installed-artifact manifests on every reuse. Internally it invokes the resolved absolute CMake 3.22.1 path with `-G "Visual Studio 17 2022" -A x64 -S <verified-source> -B <host-deps-build> -DBUILD_SHARED_LIBS=OFF -DCMAKE_INSTALL_PREFIX=<zlib-1.3.1-install>`, then the same executable for `--build ... --config Release` and `--install ... --config Release`; any step, generator/compiler mismatch or hash mismatch is fatal.

```powershell
pwsh -File tools/quality/bootstrap_host_zlib.ps1 -OutputDirectory .artifacts/host-deps
```

```powershell
pwsh -NoProfile -File tools/quality/invoke_pester_gate.ps1 -Path tools/quality/tests/BootstrapHostZlib.Tests.ps1
```

```powershell
$hostTools = Import-PowerShellDataFile .artifacts/host-deps/host-toolchain.psd1
& $hostTools.CMakeExe -S core -B .artifacts/build/core-host -G $hostTools.Generator -A $hostTools.Architecture -DNES_BUILD_TESTS=ON -DZLIB_ROOT="$PWD/.artifacts/host-deps/zlib-1.3.1-install"
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
```

```powershell
$hostTools = Import-PowerShellDataFile .artifacts/host-deps/host-toolchain.psd1
& $hostTools.CMakeExe --build .artifacts/build/core-host --config Release --target nes_core_test
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
```

```powershell
$hostTools = Import-PowerShellDataFile .artifacts/host-deps/host-toolchain.psd1
& $hostTools.CTestExe --test-dir .artifacts/build/core-host -C Release --output-on-failure
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
```

- [ ] **4.6 Run Android gates**

```powershell
.\gradlew.bat :app:testDebugUnitTest --tests "com.flynes.emu.video.*" --tests com.flynes.emu.AudioPumpTest --no-daemon
```

```powershell
.\gradlew.bat :app:connectedDebugAndroidTest -Pandroid.testInstrumentationRunnerArguments.class=com.flynes.emu.NativePresenterIntegrationTest --no-daemon
```

```powershell
.\gradlew.bat :app:connectedDebugAndroidTest -Pandroid.testInstrumentationRunnerArguments.notAnnotation=com.flynes.emu.test.DeviceCertification --no-daemon
```

Expected: when the dispatcher keeps up, 60 source frames cause 60 copies/uploads/swaps; under an injected stall, Native explicitly reports coalesced/skipped sequences and never duplicates a copied sequence. Pause/destroy leaves no callbacks; baseline PNG diff stays within the approved tolerance. The shared certification runner is buildable and its base rows are discoverable, but no physical result is claimed without the Task 8 candidate/evidence transaction.

- [ ] **4.7 Commit**

```powershell
git add core app/src/main/cpp app/src/main/java/com/flynes/emu app/src/test app/src/androidTest
git commit -m "perf: unify event-driven native video presentation"
```

## Task 5: Add qualified MMPX and ScaleFX spatial pipelines

**Files**

- Create native `shader_program`, `framebuffer_target`, `spatial_pipeline`, `mmpx_pass`, `scalefx_pipeline`, `gpu_timer_query` under `app/src/main/cpp/video/`.
- Create shader assets under `app/src/main/assets/shaders/mmpx/` and `scalefx/`.
- Create `app/src/test/java/com/flynes/emu/video/reference/MmpxCpuOracle.java` with a fixed oracle version/hash and source notice; Android GPU tests consume only its frozen expected fixtures.
- Add ScaleFX upstream commit/license notice under `app/src/main/assets/shaders/scalefx/NOTICE`.
- Add fixtures and goldens under `app/src/androidTest/assets/spatial-golden/`.
- Create `tools/quality/generate_mmpx_oracle_manifest.ps1`, its Pester test and `docs/acceptance/display-quality/mmpx-oracle-manifest.json`.
- Create `MmpxOracleFixtureTest`, `SpatialFilterGoldenTest`, `SpatialCapabilityFallbackTest`.

- [ ] **5.0 Freeze the independent oracle before any GPU port exists**

Implement only `MmpxCpuOracle`, its source notice, input/expected fixtures and `MmpxOracleFixtureTest`. The manifest generator writes oracle version plus canonical SHA-256 for every oracle/fixture/notice file and rejects unlisted input. Run the JVM/oracle fixture gate and Pester generator test, then inspect the staged diff to prove it contains no MMPX shader, GLES port, production `mmpx_pass` or golden generated by that future port.

```powershell
.\gradlew.bat :app:testDebugUnitTest --tests com.flynes.emu.video.reference.MmpxOracleFixtureTest --no-daemon
```

```powershell
pwsh -NoProfile -File tools/quality/invoke_pester_gate.ps1 -Path tools/quality/tests/GenerateMmpxOracleManifest.Tests.ps1
```

```powershell
git add app/src/test/java/com/flynes/emu/video/reference app/src/test/resources/spatial-golden/oracle tools/quality/generate_mmpx_oracle_manifest.ps1 tools/quality/tests/GenerateMmpxOracleManifest.Tests.ps1 docs/acceptance/display-quality/mmpx-oracle-manifest.json
git commit -m "test: freeze independent MMPX oracle"
```

Record that commit id in the acceptance index. All later implementation-manifest generation recomputes the frozen file hashes and fails if they differ; shader work may consume the fixtures but may not rewrite the oracle commit.

- [ ] **5.1 Write offscreen tests before production shaders**

Fixtures cover text, diagonals, 1px lines, checkerboards, sprites, palette transitions and edge dimensions. Assert exact Nearest; compare the self-authored GLES2 MMPX port pixel-by-pixel to the versioned CPU oracle; compare the standard ScaleFX 3× five-pass port to pinned goldens with a documented one-LSB float tolerance.

- [ ] **5.2 Implement explicit capability probes**

Probe fragment highp, max texture size, half/float FBO renderability and filterability, disjoint timer query and context version. An unsupported float FBO must produce a tested resolver constraint; do not simply discard that device's test.

- [ ] **5.3 Implement the native pipeline**

Use 512×480 MMPX or 768×720 ScaleFX intermediates from 256×240. Overscan is fixed 0/0/0/0 this release. Apply aspect/PAR and final sharp composite after integer reconstruction; CRT and controls remain separate. Pin ScaleFX to five passes and 3×. Do not call MMPX reference-equivalent until every oracle fixture passes.

- [ ] **5.4 Feed failures back to the resolver**

Compile/link/FBO/context failures emit `RuntimeFailure`. Presenter may use emergency Nearest for the current frame, but status must say emergency fallback until resolver returns the next qualified complete configuration. No silent local ScaleFX → MMPX switch is allowed.

- [ ] **5.5 Measure GPU work correctly**

If `EXT_disjoint_timer_query` is present, a native wrapper covers upload, all FBO passes, final composite and CRT; discard disjoint samples. Otherwise record GPU timing `UNAVAILABLE` and require AGI/Perfetto evidence for qualification. CPU wall time must not be labeled GPU time.

- [ ] **5.6 Run gates**

```powershell
.\gradlew.bat :app:connectedDebugAndroidTest -Pandroid.testInstrumentationRunnerArguments.class=com.flynes.emu.SpatialFilterGoldenTest,com.flynes.emu.SpatialCapabilityFallbackTest --no-daemon
```

```powershell
.\gradlew.bat :app:testDebugUnitTest :app:lintDebug --no-daemon
```

MMPX and ScaleFX remain unavailable when no exact profile entry exists, even if all implementation tests pass.

- [ ] **5.7 Commit**

```powershell
git add app/src/main/cpp app/src/main/assets/shaders app/src/test app/src/androidTest app/src/main/java/com/flynes/emu/video docs/acceptance/display-quality/mmpx-oracle-manifest.json
git commit -m "feat: add qualified pixel-art reconstruction"
```

## Task 6: Request and measure native-time 120Hz without duplicate submits

**Files**

- Modify `DisplayModeController`, `DisplayPlatformFacade`, `GameSurfaceView`, `NativeVideoPresenter`, native `PresentationCoordinator`, `VideoStatusAccumulator`.
- Create `DisplayRequestLifecycleTest`, `NativeTime120ContractTest` and device-only `NativeTime120CertificationTest`.
- Reuse the Task 4 runtime-retained `app/src/androidTest/java/com/flynes/emu/test/DeviceCertification.java` and `SingleDeviceCertificationRunner.java`; Task 6 adds 120Hz cases but does not create the shared base-certification harness.
- Create first version of `tools/quality/capture_display_evidence.ps1` and `docs/acceptance/display-quality/README.md`.
- Create `tools/quality/tests/CaptureDisplayEvidence.Tests.ps1` with mocked adb/process adapters.

- [ ] **6.1 Test request and clear lifecycle via the platform facade**

For HZ_120 + Native, Java requests only the exact same-resolution `preferredDisplayModeId`; the native render-thread `PresentationCoordinator` alone calls `ANativeWindow_setFrameRate(coreConfirmedSourceFps, DEFAULT)` for that surface epoch. No Java `Surface.setFrameRate` call exists. On FOLLOW_SYSTEM, pause, Surface destroy or mode change, coordinator clears its vote before Java clears/replaces the window mode request, then reapplies only after a new Surface epoch is valid. Fake-platform tests assert the ordered command log, old-epoch rejection, NTSC≈60/PAL≈50 votes and a single frame-rate writer. Task 7 extends the same coordinator with the explicit Native→Priming→Motion→Hold/Shadow/Drain→Native vote/pacer table; it may not bolt Swappy beside this path.

- [ ] **6.2 Keep the presenter source-driven**

For an NTSC fixture, about 60 unique frames/s cause about 60 uploads and new buffer submissions on a simulated 120Hz display; for PAL, the corresponding values are about 50. SurfaceFlinger retains the latest buffer for intervening scans. Status binds source/upload/submit expectations to `SourceTiming`, keeps synthesis slots at 0 and reports requested 120 separately from system-active mode.

- [ ] **6.3 Isolate hardware assertions**

Ordinary JVM/AVD suites assert calls and truthful fallback only. Mark physical 119–121Hz, SurfaceFlinger cadence, touch-latency and power tests `@DeviceCertification`; run them only with an explicitly verified single serial and certification runner.

- [ ] **6.4 Build a bounded evidence script**

The script requires `-Serial`, checks exactly that authorized device/model, verifies installed package/version/cert against the input APK, creates a new timestamped ignored directory, identifies the exact SurfaceFlinger layer, captures full display modes plus a bounded Perfetto window during the 30-minute run, and exits nonzero on unauthorized/multiple devices, missing layer, trace failure or app crash. It never overwrites or deletes evidence. Script tests cover quoting, nonzero propagation, duplicate devices, existing output, missing layer and trace cleanup without needing a phone.

- [ ] **6.5 Run automated gates**

```powershell
.\gradlew.bat :app:testDebugUnitTest --tests com.flynes.emu.video.DisplayRequestLifecycleTest --tests com.flynes.emu.video.NativeTime120ContractTest --no-daemon
```

```powershell
.\gradlew.bat :app:connectedDebugAndroidTest -Pandroid.testInstrumentationRunnerArguments.notAnnotation=com.flynes.emu.test.DeviceCertification --no-daemon
```

```powershell
pwsh -NoProfile -File tools/quality/invoke_pester_gate.ps1 -Path tools/quality/tests/CaptureDisplayEvidence.Tests.ps1
```

- [ ] **6.6 Run X100 Pro evidence only when connected**

Run 60Hz Native and 120Hz Native as at least three interleaved 30-minute paired trials at fixed scene/brightness/battery band. Record the core-confirmed source timing; NTSC and PAL expectations are separate, and an NTSC-only run cannot certify PAL cadence copy. A 120 sample is valid only while the exact requested mode remains system-active for the full measured window with no fallback. Compare SurfaceFlinger/Perfetto cadence, high-speed touch-to-visible p95, calibrated touch-to-core p95, power 95% CI and temperatures. If latency benefit is not measurable or power cost is unacceptable, keep 120Hz locked; do not market duplicated scans as smoother game motion.

- [ ] **6.7 Commit**

```powershell
git add app/src/main/java/com/flynes/emu/video app/src/main/cpp tools/quality docs/acceptance/display-quality app/src/test app/src/androidTest
git commit -m "feat: add evidence-gated native-time 120hz"
```

## Task 7: Add motion compensation to the existing native presenter

**Files**

- Create portable `core/video/temporal_interpolator.h/.cpp` and `temporal_scheduler.h/.cpp`.
- Modify `core/CMakeLists.txt`; create `core/tests/test_temporal_interpolator.cpp` and target `temporal_interpolator_test`.
- Create native `motion_compute_pipeline.h/.cpp`; extend the existing `egl_presenter` and sole `PresentationCoordinator`.
- Create `app/src/main/java/com/flynes/emu/video/audio/TemporalAudioDelay.java`, `AvSyncMonitor.java`, `TemporalTransitionController.java` and `DisplayLeaseWatchdog.java` with JVM tests.
- Create device tests `MotionComputeParityTest`, `MotionContextFallbackTest`, `SwappyFailClosedTest`.
- Modify `AudioThread`, `AudioPump`, `app/build.gradle`, `app/src/main/cpp/CMakeLists.txt`.

- [ ] **7.1 Write deterministic host fixtures and scheduler tests**

Test two-pixel motion → one-pixel midpoint, A/B palette union, HUD lock, occlusion, scene-cut/full-flash hold, unsafe ratio, and cadence driven by 120.000/119.88Hz presentation timestamps. At exact 120Hz with 60.0988 source, phase remains bounded and adjustments average about one per 5.06 seconds.

Add `video_algorithms` as a portable static library in `core/CMakeLists.txt`; both the host target and Android `nescore` link it. Do not point the core host test at an app-only source path.

- [ ] **7.2 Test the exact prime/hold/drain timeline**

Obtain A and B before output; after one-frame delay submit A, M(A,B), B. Test `PRIMING → MOTION_COMPENSATING → BUFFERED_NATIVE_HOLD → PRIMING_SHADOW → MOTION_COMPENSATING`, `HOLD → DRAINING → IMMEDIATE_NATIVE` and `MOTION/PRIMING → SURFACE_SUSPENDED_HOLD → new surfaceEpoch + new display generation + 3s fresh lease → PRIMING`. Assert queue depths, A/V skew, pause/scene-cut boundaries and severe-temperature drain. The 120-second hold recovery uses a non-presented two-second/at-least-120-pair shadow window; it resumes only with fresh ratio <10%, no >25% spike, continuous sequences and GPU budget pass, otherwise resets the cooldown.

`TemporalAudioDelay` owns a timestamped PCM ring buffer sized from sample rate/channels. Tests cover partial AudioTrack writes, silence, underrun, flush, one-frame removal and an 8ms equal-power crossfade around the removed ~16.7ms block. The crossfade masks the boundary; it does not itself remove the delay.

Motion switches `FrameDispatchExecutor` to a lossless bounded staging ring before priming. A sequence gap, overflow or missing `FrameStepResult` immediately stops synthesis, records `SOURCE_SEQUENCE_GAP` and enters Buffered Native Hold. It never interpolates non-adjacent frames.

Table-drive every active-Motion exit reason—display mismatch/lease expiry, artifact ratio, sequence gap/overflow, Swappy/ES3.1/motion-shader/context failure, Surface loss, system battery saver, low battery, adaptive edge, Severe and Critical—through `TemporalTransitionController`. Ordinary failures first Hold; Surface/context loss freezes CPU A/V queues in `SURFACE_SUSPENDED_HOLD`; only a user-paused/Critical transaction, or the explicitly paused Surface-recovery failure transaction, may atomically clear stopped queues. No resolver result may jump a running Motion session directly to Immediate Native/0ms.

On unpaused Surface loss, the session safety executor pauses core production and `AudioTrack`, releases inputs, preserves the last A/B staging pair plus matching audio-delay block, clears active id/key, increments `surfaceEpoch`, invalidates the old display generation/deadline and only then destroys EGL/`ANativeWindow`. A replacement Surface remains paused with no swap until a new generation has a compatible 119–121Hz lease continuously fresh for three seconds and ES3.1/Swappy/spatial resources are rebuilt. Success discards the frozen old presentation pair inside the paused transaction and reprimes from new adjacent frames; failure clears both queues inside that same paused transaction and resumes Immediate Native. Tests cover loss during Priming and Motion, repeated loss, stale old-epoch callbacks and ES3→ES2 fallback without A/V jump.

- [ ] **7.3 Implement the pixel-aware algorithm and ES3.1 compute path**

Use quarter-resolution 8×8 ±8 block search, full-resolution 4×4 ±2 refinement, forward/backward confidence and nearest sampling only from A/B RGB565 colors. Low-confidence slots hold the prior real frame. Scalar C++ is the oracle; ES3.1 compute is the candidate implementation. `MotionComputeParityTest` compares captured 256×240 intermediate buffers—not post-ScaleFX Surface pixels—fixture by fixture on device.

- [ ] **7.4 Integrate Swappy into the one existing presenter**

Enable Prefab and add `androidx.games:games-frame-pacing:2.1.3`. In CMake:

```cmake
find_package(games-frame-pacing REQUIRED CONFIG)
find_library(EGL_LIB EGL)
find_library(GLES3_LIB GLESv3)
target_link_libraries(nescore PRIVATE
    games-frame-pacing::swappy_static ${EGL_LIB} ${GLES3_LIB})
```

Motion can be selected only while paused. The existing presenter drains, destroys its ES2 context, attempts one ES3.1 context, rebuilds the same baseline/spatial resources, and only then primes Motion. On any failure it recreates ES2, resolves Native and resumes inside that paused transaction; it never leaves two contexts presenting. Returning to Native follows the same paused lifecycle. Runtime context/Surface loss follows the suspended-hold transaction above, not this user-selection shortcut.

`PresentationCoordinator` implements the spec's complete per-state owner table on one render-thread queue. Native/Hold/Shadow/Drain detach Swappy first, then write the core-confirmed source-rate vote and use direct EGL; Priming clears that vote before initializing Swappy and binding only the current epoch window; Motion leaves Swappy as the sole vote/pacer writer and only calls `SwappyGL_swap`; suspended hold has no window or vote. Every handoff is ordered after the prior swap/fence and publishes no active id/key until the entire target transaction succeeds. Java never calls `Surface.setFrameRate`.

For Motion set `SwappyGL_setAutoSwapInterval(false)`, `SwappyGL_setAutoPipelineMode(false)`, an explicit target interval derived from the fresh 119–121Hz active mode, and enable stats. Native paths remain source-driven and never run a 120Hz Swappy loop. Expose stats and fail closed if Swappy falls to 60, stats are stale or about 120 submissions cannot be sustained. Motion may prime only after a fresh same-epoch/same-generation compatible observation is stable for three seconds. `DisplayLeaseWatchdog` owns the independent 1500ms deadline token; the compute path checks it before every synthesized frame and presenter checks it before every Swappy swap, so a blocked monitor cannot extend a lease. The first incompatible/expired check immediately enters Hold. Tests cover exact vote clear/attach/detach order for NTSC and PAL, activation wait, independent deadline fire, stalled monitor, pre-synth/pre-swap expiry, immediate mismatch, no observation, heartbeat timeout, stale/unknown lease, epoch/generation change, ES3.1 creation failure, repeated Surface recreation, Swappy disabled and simulated 60Hz active mode. There is still one Surface producer and one frame-rate writer.

`AvSyncMonitor` reads actual `AudioTrack.getTimestamp()` playback position and presenter/Swappy presentation timestamps, maps both through the calibrated clock domain, and records skew plus uncertainty. Queue-depth estimates are diagnostic only and cannot satisfy the A/V gate.

- [ ] **7.5 Run host and Android gates**

```powershell
pwsh -File tools/quality/bootstrap_host_zlib.ps1 -OutputDirectory .artifacts/host-deps
```

```powershell
$hostTools = Import-PowerShellDataFile .artifacts/host-deps/host-toolchain.psd1
& $hostTools.CMakeExe -S core -B .artifacts/build/core-host -G $hostTools.Generator -A $hostTools.Architecture -DNES_BUILD_TESTS=ON -DZLIB_ROOT="$PWD/.artifacts/host-deps/zlib-1.3.1-install"
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
```

```powershell
$hostTools = Import-PowerShellDataFile .artifacts/host-deps/host-toolchain.psd1
& $hostTools.CMakeExe --build .artifacts/build/core-host --config Release --target nes_core_test temporal_interpolator_test
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
```

```powershell
$hostTools = Import-PowerShellDataFile .artifacts/host-deps/host-toolchain.psd1
& $hostTools.CTestExe --test-dir .artifacts/build/core-host -C Release --output-on-failure
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
```

```powershell
.\gradlew.bat :app:testDebugUnitTest --tests "com.flynes.emu.video.audio.*" --no-daemon
```

```powershell
.\gradlew.bat :app:connectedDebugAndroidTest -Pandroid.testInstrumentationRunnerArguments.class=com.flynes.emu.NativePresenterIntegrationTest --no-daemon
```

```powershell
.\gradlew.bat :app:connectedDebugAndroidTest -Pandroid.testInstrumentationRunnerArguments.class=com.flynes.emu.MotionComputeParityTest,com.flynes.emu.MotionContextFallbackTest,com.flynes.emu.SwappyFailClosedTest --no-daemon
```

Expected: implementation exists but public UI remains locked because no formal complete configuration is certified.

- [ ] **7.6 Commit**

```powershell
git add core app/build.gradle app/src/main/cpp app/src/main/java/com/flynes/emu/video app/src/test app/src/androidTest
git commit -m "feat: add gated pixel motion compensation"
```

## Task 8: Run two-stage X100 Pro certification and produce an installable Release

**Files**

- Create `tools/quality/generate_video_implementation_manifest.ps1`, `acquire_authenticated_release_time.ps1`, `resolve_android_release_tools.ps1`, `verify_certification_release_parity.ps1`, `freeze_certification_candidate.ps1`, `run_device_certification.ps1`, `generate_device_quality_profile.ps1`, `generate_evidence_manifest.ps1`, `verify_evidence_manifest.ps1`, `verify_release_artifact.ps1`, `run_release_regression.ps1`, `run_automated_release_gates.ps1`, `record_release_experience_gate.ps1`, `finalize_release_experience_gate.ps1` and `verify_release_completion.ps1`.
- Create Pester files using discoverable names: `GenerateVideoImplementationManifest.Tests.ps1`, `AuthenticatedReleaseTime.Tests.ps1`, `ResolveAndroidReleaseTools.Tests.ps1`, `CertificationReleaseParity.Tests.ps1`, `FreezeCertificationCandidate.Tests.ps1`, `RunDeviceCertification.Tests.ps1`, `GenerateDeviceQualityProfile.Tests.ps1`, `EvidenceManifest.Tests.ps1`, `VerifyReleaseArtifact.Tests.ps1`, `RunReleaseRegression.Tests.ps1`, `AutomatedReleaseGates.Tests.ps1`, `ReleaseExperienceGate.Tests.ps1` and `VerifyReleaseCompletion.Tests.ps1` under `tools/quality/tests/`.
- Create `tools/quality/schemas/display-quality-evidence.schema.json`, committed `tools/quality/release-time-authority.json`, `tools/quality/matrices/base-required-matrix.json`, `tools/quality/matrices/advanced-release-scope.json`, `tools/quality/matrices/release-automation-required.json` and `tools/quality/matrices/release-experience-required.json`, and exact lab-only paths `app/src/certification/AndroidManifest.xml`, `app/src/certification/java/com/flynes/emu/certification/CertificationControlService.java` and `app/src/certification/assets/candidate-device-quality-profiles.json`. Reuse the Task 4 runtime-retained `app/src/androidTest/java/com/flynes/emu/test/DeviceCertification.java` and single-device runner. The service is protected by a lab signature permission; the Release manifest and APK contain no evidence-control endpoint, permission declaration or lab profile.
- Modify profile loader/assets and Gradle signing contract.
- Add `keystore.properties.example` and ignore the real `keystore.properties`; never commit keys or passwords.

- [ ] **8.1 Define canonical identities and hashes**

Use four separate identities:

- `driverFingerprint`: normalized build fingerprint, GLES vendor/renderer/version/extensions and driver package/version where available;
- `algorithmImplementationHash`: canonical manifest of temporal/spatial shader sources, portable algorithm sources, presenter pacing, audio-delay code and relevant compiler parameters;
- `buildImplementationHash`: canonical production-equivalence manifest covering `app/src/main`, core production sources, Gradle/dependency locks, NDK/CMake inputs, packaged resources and release compiler flags.
- `resolvedVariantParityHash`: normalized resolved values for Java/NDK optimization, debuggable/JNI-debug flags, minify/resource shrink, ABI filters, CMake arguments, compiler/linker flags, dependency graph/versions, packaging options and generated production resources.

The build hash deliberately excludes the profile JSON being generated, signing material/certificate, APK container metadata, version code/name, acceptance evidence and the isolated `certification` source set. Those exclusions are enumerated and tested; nothing else is implicitly excluded. Candidate and final Release must have the same `algorithmImplementationHash`, `buildImplementationHash` and `resolvedVariantParityHash`.

`acquire_authenticated_release_time.ps1` must obtain and verify a nonce-bound signed time response from the pinned Release time authority; each create-new-only receipt contains its role (`candidate-build`, `profile-release` or `release-completion`), trusted epoch, nonce, authority/certificate identity, role-specific context hashes, response bytes hash, verification-tool identity and its own canonical hash. Local wall time, APK ZIP mtime and an unsigned CI environment variable are forbidden fallbacks. Offline Release verification consumes the frozen signed response rather than the app's runtime clock bootstrap. Synthetic Pester fixtures cover bad signature/chain/nonce/role/context, stale/future response, replay, missing authority and local-clock fallback; every case is fatal.

Gradle receives one exact role-bound receipt as an explicit input and emits a canonical, non-self-referential `video-build-identity.json`; that generated identity file is an explicitly tested hash exclusion, is protected by the APK signature, and carries all three hashes, schema version, receipt role/hash and its trusted build epoch. The certification APK uses a `candidate-build` receipt acquired before device testing. After all candidate evidence is frozen, profile generation and the final APK use a new `profile-release` receipt whose epoch is at or after every bound evidence capture; that second receipt supplies both `certifiedAtEpochMs` and the final `releaseBuildEpochMs`. Candidate/final parity requires the same three implementation hashes but deliberately requires different allowed receipt roles; no profile validity is derived from the older candidate time. Reissuing a profile with a later certificate interval requires new candidate evidence and a new attempt, while merely rebuilding an existing profile never changes its signed interval. The final verifier compares each APK identity with its correct frozen receipt and record, so time identity is never inferred from an APK or local clock by guesswork.

Pin `android { buildToolsVersion "35.0.0" }`, matching the AGP 8.13.2 build contract, instead of resolving whichever installed directory is newest. `resolve_android_release_tools.ps1` accepts an optional absolute SDK root or parses `sdk.dir` from `local.properties`, resolves exactly `<sdk>/build-tools/35.0.0/apksigner.bat`, parses that directory's `source.properties` and requires the uncommented `Pkg.Revision=35.0.0`, then separately runs `apksigner version` and requires/records its independent tool protocol version `0.9`. It hashes `apksigner.bat` plus the exact launcher JAR inputs and writes ignored `.artifacts/android-release-tools.psd1`. It rejects a missing executable/source.properties, a path outside the pinned directory, a wrong `Pkg.Revision`, an unexpected ApkSigner protocol version, malformed local properties or a nonzero probe. The portable build-tools revision enters `buildImplementationHash`; ApkSigner protocol/input hashes enter the preflight/evidence-tool manifest because verification does not alter production code. The machine-local absolute path is recorded only in preflight. `verify_release_artifact.ps1` requires the resolved absolute ApkSigner path and treats every nonzero verify exit as fatal; Pester independently covers missing path, wrong `Pkg.Revision`, wrong protocol version and nonzero verify. No Release gate invokes PATH-dependent `apksigner`.

- [ ] **8.2 Add a closed lab path, not a public bypass**

The `certification` build type is `initWith(release)`, non-debuggable and non-publishable. It has the same R8/resource shrink, Java/NDK optimization, ABI, CMake, dependency and packaging inputs as Release; the only allowed differences are lab signing/applicationId suffix, candidate profile overlay and isolated signature-protected driver. `verifyCertificationReleaseParity` serializes both resolved variant models, rejects any other difference and emits `resolvedVariantParityHash`; negative tests perturb each protected field and must fail. The lab build can exercise an exact candidate configuration before a formal profile exists. The runner requires exactly one verified ADB serial and refuses `offline`, `unauthorized` or multi-device sessions. Release builds reject the lab profile and contain no exported control component.

`freeze_certification_candidate.ps1` is the only way to begin a device run. It consumes the actual assembled certification APK, resolved variant manifests and verified `candidate-build` time receipt, verifies their signatures/hashes, and copies that receipt create-new-only into the new content-addressed attempt before recording its relative path/hash. It creates a canonical attempt descriptor containing the APK hash, source/build identities, tool hashes, candidate-time-receipt hash, creation nonce and the hashes of `base-required-matrix.json`, `advanced-release-scope.json`, `release-automation-required.json` and `release-experience-required.json`. The descriptor hash is the immutable `attemptId`; all files live under `.artifacts/certification/attempts/<candidate-apk-sha256>/<attemptId>/`. Its `candidate-record.json` contains the candidate APK relative path, SHA-256, certificate digest, source commit, `algorithmImplementationHash`, `buildImplementationHash`, `resolvedVariantParityHash`, `candidateAuthenticatedTimeSha256`, `candidateBuildEpochMs`, `baseRequiredMatrixHash`, `advancedReleaseScopeHash` and `releaseExperienceRequiredMatrixHash`. Create-new semantics apply to the entire attempt tree; retries create a new descriptor/id and never overwrite or delete an older attempt. Changing the candidate receipt, one advanced row between `INCLUDED` and `NOT_INCLUDED`, a tuple or experience dimension therefore requires a new Task 8.2 candidate attempt; no downstream script accepts a substituted input by path alone.

For convenience only, freeze atomically updates ignored `.artifacts/certification/active-attempt.psd1` with absolute validated paths for the selected immutable attempt, `CandidateAuthenticatedTime`, candidate record/evidence root and content-addressed documentation manifest path. This mutable pointer carries no evidence authority: every downstream script reopens the immutable record and validates its descriptor/hash. `run_device_certification.ps1` consumes that record rather than a loose APK path, re-hashes and installs that exact candidate, verifies the installed package/certificate, and writes the candidate-record/matrix hashes into every run. This makes the APK that was actually tested the sole parity reference for the final Release.

Run the script tests before touching a phone:

```powershell
pwsh -NoProfile -File tools/quality/invoke_pester_gate.ps1 -Path tools/quality/tests
```

```powershell
$candidateTime = '.artifacts/time-receipts/<new-nonce>/candidate-build.json'
pwsh -NoProfile -File tools/quality/acquire_authenticated_release_time.ps1 -Role candidate-build -AuthorityConfig tools/quality/release-time-authority.json -Output $candidateTime
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
```

```powershell
$candidateTime = '.artifacts/time-receipts/<same-new-nonce>/candidate-build.json'
.\gradlew.bat :app:verifyCertificationReleaseParity :app:assembleCertification -PauthenticatedBuildTime=$candidateTime --no-daemon
```

```powershell
$candidateTime = '.artifacts/time-receipts/<same-new-nonce>/candidate-build.json'
pwsh -NoProfile -File tools/quality/freeze_certification_candidate.ps1 -Apk app/build/outputs/apk/certification/app-certification.apk -CandidateAuthenticatedTime $candidateTime -RequiredMatrix tools/quality/matrices/base-required-matrix.json -RequiredAdvancedScope tools/quality/matrices/advanced-release-scope.json -RequiredAutomationMatrix tools/quality/matrices/release-automation-required.json -RequiredExperienceMatrix tools/quality/matrices/release-experience-required.json -AttemptStore .artifacts/certification/attempts -ActiveContext .artifacts/certification/active-attempt.psd1
```

- [ ] **8.3a Execute the base matrix required by every Release**

For From Below, Super Mario, Contra, Tetris and River City Ransom, first compare the fixed manifest's region/timing with core-confirmed runtime `SourceTiming`; any mismatch is `INVALID`. The committed, canonical `base-required-matrix.json` enumerates the complete Cartesian set of those fixtures with `Native60/Nearest` and current `Balanced/Sharp`, plus required paired power/thermal and sustained-play fields. It depends only on Tasks 1–4 plus this Task 8 harness; it must not name Task 5, 6 or 7 classes. Each comparison uses at least three interleaved paired 30-minute trials and passes only when the 95% CI upper bound is within the configured delta. Every timed sample atomically records `activeConfigurationId + activeConfigurationKey + surfaceEpoch + displayRequestGeneration` and requires exact equality with the target for the whole window.

The runner writes an immutable `base-candidate-gate-record.json` containing the required-matrix hash and exactly one terminal result for every required row. It exits nonzero for any missing, duplicate, `INVALID`, `FAIL`, `UNVERIFIED` or under-sampled row; it is impossible to turn a partial set of passing rows into a successful base gate. The record is stored inside the candidate evidence root and therefore becomes a hashed evidence entry.

```powershell
$attempt = Import-PowerShellDataFile .artifacts/certification/active-attempt.psd1
pwsh -NoProfile -File tools/quality/run_device_certification.ps1 -CandidateRecord $attempt.CandidateRecord -Matrix Base -RequiredMatrix tools/quality/matrices/base-required-matrix.json -GateRecord (Join-Path $attempt.CandidateEvidenceRoot 'base-candidate-gate-record.json') -Serial <verified-serial> -EvidenceRoot $attempt.CandidateEvidenceRoot
```

- [ ] **8.3b Execute only the advanced matrices included in this build**

The committed `advanced-release-scope.json` is the only authority for the advanced scope of this candidate. It enumerates every advanced complete `VideoConfigurationKey` contemplated for this Release—device/build identity, core-confirmed source timing, resolution/mode, refresh, temporal, spatial, post effect and aspect—and assigns exactly one declaration: `INCLUDED` or `NOT_INCLUDED`, with a stable row id and reason. MMPX/ScaleFX rows can be `INCLUDED` only when Task 5 is present, Native120 only when Task 6 is present, and Motion120 only when Task 7 is present. A base-only Release is represented by the same complete scope with every advanced row explicitly `NOT_INCLUDED`; those rows remain `UNVERIFIED` and locked and do not block the base gate. Omission of a known tuple is not equivalent to `NOT_INCLUDED` and is fatal.

`run_device_certification.ps1 -Matrix IncludedAdvanced` consumes both the frozen candidate record and `-RequiredAdvancedScope`. It writes immutable `advanced-candidate-gate-record.json` with the candidate-record hash, scope hash and exactly one terminal result for every scope row. An `INCLUDED` row must have result `PASS`; a `NOT_INCLUDED` row must have result `NOT_INCLUDED` and no profile/evidence claim. Missing or duplicate rows, an undeclared result, status/declaration mismatch, or `INVALID`/`FAIL`/`UNVERIFIED` for an included row makes the process nonzero. Motion is `INCLUDED` only for `NTSC_60_0988`; PAL/UNKNOWN Motion tuples are explicitly `NOT_INCLUDED` with a source-timing reason, and dedicated negative safety rows must pass to prove they cannot inherit NTSC evidence. No tool is allowed to discover the intended scope from whichever tests happened to run or profile rows happened to be generated.

Each applicable power comparison follows the same paired-trial rule as the base matrix. Motion additionally records every 500ms display heartbeat and requires its same-epoch/same-generation compatible observation lease to remain fresh; any 1500ms expiry marks the trial `INVALID`. Capture Perfetto, SurfaceFlinger, app counters, Swappy stats, thermal/power CSV, high-speed latency, heat-camera/patch temperature, A/V skew and two-person artifact review.

```powershell
$attempt = Import-PowerShellDataFile .artifacts/certification/active-attempt.psd1
pwsh -NoProfile -File tools/quality/run_device_certification.ps1 -CandidateRecord $attempt.CandidateRecord -Matrix IncludedAdvanced -RequiredAdvancedScope tools/quality/matrices/advanced-release-scope.json -GateRecord (Join-Path $attempt.CandidateEvidenceRoot 'advanced-candidate-gate-record.json') -Serial <verified-serial> -EvidenceRoot $attempt.CandidateEvidenceRoot
```

- [ ] **8.4 Freeze the formal profile in a clean production commit or refuse it**

Before creating a profile, freeze the content-addressed `$attempt.CandidateManifest` at `docs/acceptance/display-quality/attempts/<candidate-sha256>/<attemptId>/candidate-evidence-manifest.json` from the completed ignored **candidate** evidence root and verify it. Canonicalization is UTF-8 without BOM, LF line endings, lexicographically sorted object keys and evidence entries sorted by normalized relative logical path. Raw trace/CSV/video/PNG/log/gate-record entries each carry a normalized path relative to `EvidenceRoot`, byte size, SHA-256, media kind, capture-tool version, run/trial id, device identity, tested APK hash, exact configuration id/key and evidence capture epoch. The evidence schema also requires `capturedAtEpochMs` and evidence level on every certification claim; profile certificate bounds are added only after the later `profile-release` time receipt exists. The APK record is deliberately **not** an evidence entry: `-ArtifactRecord` is an independent top-level anchor outside `EvidenceRoot`; for this phase it is the candidate record. The generator validates and hashes it separately, and the manifest header stores `artifactRecordKind=candidate`, `artifactRecordSha256`, candidate-time receipt hash/epoch, the APK/hash identities, `baseRequiredMatrixHash`, `advancedReleaseScopeHash`, `advancedCandidateGateRecordSha256` and `releaseExperienceRequiredMatrixHash`.

Generation and verification require the base gate to contain the exact base row set with every status `PASS` and the advanced gate to contain exactly the canonical advanced scope with `PASS` for every `INCLUDED` row and `NOT_INCLUDED` for every excluded row. They independently hash and compare the scope recorded in the attempt descriptor; a changed scope path, added profile candidate or omitted advanced row is fatal. The future Task 9 experience gate does not exist at candidate freeze, so the candidate manifest binds the immutable experience-matrix hash, gate-record schema/hash contract and required logical output path `experience-release-gate-record.json`; the Release manifest later binds both that same matrix and the actual finalized gate-record hash. Generation rejects absolute/`..` evidence-entry paths, reparse points/symlinks, case-colliding duplicates, missing/unexpected evidence files and an existing frozen output. Verification separately re-hashes the explicit artifact-record argument, then reopens every listed file below the explicit evidence root and fails on a missing, added, replaced or size/hash-mismatched file. Raw evidence remains in a read-only content-addressed external bundle or the ignored local root; the committed manifest contains no ROM bytes, personal computer paths or secrets.

```powershell
$attempt = Import-PowerShellDataFile .artifacts/certification/active-attempt.psd1
pwsh -NoProfile -File tools/quality/generate_evidence_manifest.ps1 -EvidenceRoot $attempt.CandidateEvidenceRoot -ArtifactRecord $attempt.CandidateRecord -ArtifactRecordKind candidate -CandidateAuthenticatedTime $attempt.CandidateAuthenticatedTime -RequiredMatrix tools/quality/matrices/base-required-matrix.json -RequiredGateRecord (Join-Path $attempt.CandidateEvidenceRoot 'base-candidate-gate-record.json') -RequiredAdvancedScope tools/quality/matrices/advanced-release-scope.json -RequiredAdvancedGateRecord (Join-Path $attempt.CandidateEvidenceRoot 'advanced-candidate-gate-record.json') -RequiredExperienceMatrix tools/quality/matrices/release-experience-required.json -OutputManifest $attempt.CandidateManifest
```

```powershell
$attempt = Import-PowerShellDataFile .artifacts/certification/active-attempt.psd1
pwsh -NoProfile -File tools/quality/verify_evidence_manifest.ps1 -EvidenceRoot $attempt.CandidateEvidenceRoot -Manifest $attempt.CandidateManifest -ArtifactRecord $attempt.CandidateRecord -CandidateAuthenticatedTime $attempt.CandidateAuthenticatedTime -RequiredMatrix tools/quality/matrices/base-required-matrix.json -RequiredGateRecord (Join-Path $attempt.CandidateEvidenceRoot 'base-candidate-gate-record.json') -RequiredAdvancedScope tools/quality/matrices/advanced-release-scope.json -RequiredAdvancedGateRecord (Join-Path $attempt.CandidateEvidenceRoot 'advanced-candidate-gate-record.json') -RequiredExperienceMatrix tools/quality/matrices/release-experience-required.json
```

Only after the immutable candidate manifest passes, acquire the second time receipt. The request binds the candidate-record and candidate-manifest hashes in its nonce/context, uses role `profile-release`, and is written create-new-only below this attempt. Its trusted epoch must be at or after the latest captured evidence epoch. An old candidate receipt, host wall time or a receipt from another attempt cannot certify the profile.

```powershell
$attempt = Import-PowerShellDataFile .artifacts/certification/active-attempt.psd1
$releaseTime = Join-Path $attempt.AttemptRoot 'profile-release-authenticated-time.json'
pwsh -NoProfile -File tools/quality/acquire_authenticated_release_time.ps1 -Role profile-release -AuthorityConfig tools/quality/release-time-authority.json -BindCandidateRecord $attempt.CandidateRecord -BindCandidateManifest $attempt.CandidateManifest -Output $releaseTime
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
```

`generate_device_quality_profile.ps1` is the only profile writer. It independently recomputes the exact advanced profile set as `advanced-release-scope.configurationRows WHERE declaration == INCLUDED` and requires the same rows to be `PASS` in the candidate gate. The generated profile must equal that set exactly—no missing row, extra row, caller-selected subset or profile entry for `NOT_INCLUDED` is accepted—and binds one profile-level `profileId` plus the `profile-release` receipt hash; every entry carries the full `VideoConfigurationKey` (including core-confirmed `SourceTiming`), configuration id, identity, build/algorithm hashes, candidate-record hash, canonical candidate-manifest hash, evidence level, `certifiedAtEpochMs` and `validUntilEpochMs`. A base Release with every advanced row `NOT_INCLUDED` therefore produces an explicitly empty advanced profile and keeps the UI locked; it does not bypass the scope gate.

The writer sets `certifiedAtEpochMs` exactly to the verified `profile-release` trusted epoch, requires it to be at or after every valid `capturedAtEpochMs` for the row, and sets `validUntilEpochMs` no later than the policy maximum (180 days for `DEVICE_LAB`, 365 days for `COMPATIBILITY_AND_POWER`). It refuses a mismatched role/context/signature, future evidence capture, reversed or overlong interval. A power result is valid only when 100% of timed samples atomically report the target active id/key on one surface epoch and display generation; any transition with null id/key, key mismatch, emergency frame, fallback, pause, mode change or stale observation makes that trial `INVALID`, not a pass. Otherwise record the failed criterion and leave that row locked by failing the current attempt; never rewrite the scope or cherry-pick the remaining successes. Never reuse an NTSC certificate to unlock PAL/UNKNOWN. Pester mutates each scope row/status, receipt role/hash/context, profile id, validity boundary and capture time and requires profile generation to fail closed.

```powershell
$attempt = Import-PowerShellDataFile .artifacts/certification/active-attempt.psd1
$releaseTime = Join-Path $attempt.AttemptRoot 'profile-release-authenticated-time.json'
pwsh -NoProfile -File tools/quality/generate_device_quality_profile.ps1 -ProfileReleaseAuthenticatedTime $releaseTime -CandidateRecord $attempt.CandidateRecord -CandidateManifest $attempt.CandidateManifest -CandidateEvidenceRoot $attempt.CandidateEvidenceRoot -RequiredAdvancedScope tools/quality/matrices/advanced-release-scope.json -RequiredAdvancedGateRecord (Join-Path $attempt.CandidateEvidenceRoot 'advanced-candidate-gate-record.json') -Output app/src/main/assets/device-quality-profiles.json
```

Commit every production input—including the formal profile and its loader—before the final Release build. The worktree must then be clean. Redacted evidence manifests may be committed here; raw traces, device paths, ROM bytes and secrets remain ignored.

```powershell
git add app/src/main/assets/device-quality-profiles.json app/src/main app/src/certification app/src/androidTest app/build.gradle settings.gradle gradle core tools/quality docs/acceptance/display-quality .gitignore keystore.properties.example
git commit -m "test: freeze x100 pro video qualification profile"
```

- [ ] **8.5 Build final Release and rerun critical regression**

Release signing reads the ignored local properties/environment contract. Build only from the clean commit frozen in 8.4; do not edit or regenerate a production input afterward. Verify candidate/final implementation-hash equality, then build and inspect the final artifact:

```powershell
pwsh -File tools/quality/resolve_android_release_tools.ps1 -BuildToolsVersion 35.0.0 -OutputFile .artifacts/android-release-tools.psd1
```

```powershell
$attempt = Import-PowerShellDataFile .artifacts/certification/active-attempt.psd1
$releaseTime = Join-Path $attempt.AttemptRoot 'profile-release-authenticated-time.json'
.\gradlew.bat :app:assembleRelease -PauthenticatedBuildTime=$releaseTime --no-daemon
```

```powershell
$androidTools = Import-PowerShellDataFile .artifacts/android-release-tools.psd1
$attempt = Import-PowerShellDataFile .artifacts/certification/active-attempt.psd1
$releaseTime = Join-Path $attempt.AttemptRoot 'profile-release-authenticated-time.json'
pwsh -NoProfile -File tools/quality/verify_release_artifact.ps1 -Apk <release-apk> -ApkSigner $androidTools.ApkSigner -ProfileReleaseAuthenticatedTime $releaseTime -CandidateAuthenticatedTime $attempt.CandidateAuthenticatedTime -CandidateRecord $attempt.CandidateRecord -CandidateEvidenceManifest $attempt.CandidateManifest -CandidateEvidenceRoot $attempt.CandidateEvidenceRoot -RequiredMatrix tools/quality/matrices/base-required-matrix.json -RequiredAdvancedScope tools/quality/matrices/advanced-release-scope.json -AdvancedCandidateGateRecord (Join-Path $attempt.CandidateEvidenceRoot 'advanced-candidate-gate-record.json') -RequiredAutomationMatrix tools/quality/matrices/release-automation-required.json -RequiredExperienceMatrix tools/quality/matrices/release-experience-required.json -BaseCandidateGateRecord (Join-Path $attempt.CandidateEvidenceRoot 'base-candidate-gate-record.json') -AttemptRoot $attempt.AttemptRoot -ActiveContext .artifacts/certification/active-attempt.psd1
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
```

```powershell
$androidTools = Import-PowerShellDataFile .artifacts/android-release-tools.psd1
& $androidTools.ApkSigner verify --verbose --print-certs <release-apk>
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
```

The verifier recomputes the final APK's three implementation/parity hashes and compares them directly with the immutable candidate record; it accepts no caller-supplied expected hash. At this pre-Task-9 stage it independently verifies the two receipts that already exist: the `candidate-build` hash/epoch must match the candidate record/APK, while the later context-bound `profile-release` hash/epoch must match the signed profile/final APK and be no earlier than every candidate evidence capture. The two roles cannot be substituted, and local wall time cannot replace either. It also validates that the profile's candidate-evidence hash, candidate record, complete base gate and required-matrix hash agree; re-hashes every candidate evidence file; and independently recomputes the exact advanced profile set from the committed scope plus candidate gate. Extra, missing or `NOT_INCLUDED` profile rows, an advanced-gate mismatch, changed `profileId`, a profile whose `certifiedAtEpochMs` differs from the Release receipt, an overlong/expired validity interval at that epoch, or an invalid/replayed receipt is fatal. It fails if a required base row is absent/non-PASS, the candidate APK/record/evidence bundle is missing or altered, or the final APK includes the certification manifest, driver, lab profile, debug flag or exported evidence-control component.

On success it hashes a new Release-attempt descriptor and creates `<AttemptRoot>/releases/<release-apk-sha256>/<releaseAttemptId>/release-record.json`, Release evidence root and content-addressed documentation manifest path without overwriting any prior Release attempt; it then updates only the convenience active-context pointer. That immutable release record contains the final APK path/hash/certificate, three matched identities, both pre-completion receipt hashes/epochs/authorities, signed profile hash and validity summary, `advancedReleaseScopeHash`, `releaseAutomationRequiredMatrixHash` and `releaseExperienceRequiredMatrixHash`, plus the authenticated Release evaluation result. It becomes the independent anchor that the later `RELEASE_COMPLETION` receipt binds along with the finalized gates. The runtime still requires per-boot platform-trusted time bootstrap and reevaluates evidence expiry on every advanced request; a Release-time PASS cannot mint perpetual evidence.

Install the exact release record to a clean device and perform same-certificate upgrade from the preflight APK as separately recorded paths. `run_release_regression.ps1` re-hashes the Release APK before every install. Its Base run uses the same required-matrix contract as candidate qualification and exits nonzero for any missing/non-PASS row. Its `CertifiedProfile` run independently derives the exact expected advanced row set from `advanced-release-scope.json`, requires it to equal the signed profile and the candidate advanced gate, and writes exactly one Release result per included row. `profile-release-gate-record.json` must contain the signed-profile hash, scope hash and `PASS` for every included configuration; extra, missing, duplicate, expired or mismatched rows fail. When the scope has no included advanced entries it emits an explicit empty-set PASS bound to the all-`NOT_INCLUDED` scope rather than inventing a test.

```powershell
$attempt = Import-PowerShellDataFile .artifacts/certification/active-attempt.psd1
pwsh -NoProfile -File tools/quality/run_release_regression.ps1 -ReleaseRecord $attempt.ReleaseRecord -PreviousReleaseApk <previous-signed-apk> -InstallPaths Clean,Upgrade -Matrix Base -RequiredMatrix tools/quality/matrices/base-required-matrix.json -GateRecord (Join-Path $attempt.ReleaseEvidenceRoot 'base-release-gate-record.json') -Serial <verified-serial> -EvidenceRoot $attempt.ReleaseEvidenceRoot
```

```powershell
$attempt = Import-PowerShellDataFile .artifacts/certification/active-attempt.psd1
pwsh -NoProfile -File tools/quality/run_release_regression.ps1 -ReleaseRecord $attempt.ReleaseRecord -InstallPaths ReuseVerifiedInstall -Matrix CertifiedProfile -RequiredAdvancedScope tools/quality/matrices/advanced-release-scope.json -AdvancedCandidateGateRecord (Join-Path $attempt.CandidateEvidenceRoot 'advanced-candidate-gate-record.json') -GateRecord (Join-Path $attempt.ReleaseEvidenceRoot 'profile-release-gate-record.json') -Serial <verified-serial> -EvidenceRoot $attempt.ReleaseEvidenceRoot
```

On the final APK rerun profile matching, cold start, every profile entry, 30-minute gameplay, pause/resume, Surface recreation, A/V sync and thermal fallback. Candidate evidence alone never substitutes for these final-artifact rows.

- [ ] **8.6 Keep the Release evidence transaction open for Task 9**

Candidate qualification is already frozen and must never be reopened for append. Release regression uses the separate content-addressed `$attempt.ReleaseEvidenceRoot`, but **does not generate `$attempt.ReleaseManifest` yet**: Task 9 still has automated, visual, accessibility and sustained-play evidence to add. Every Task 9 capture receives the immutable `$attempt.ReleaseRecord` and writes through a create-new-only evidence helper; ad-hoc files and overwrites are rejected. The Release manifest and final completion verdict are created only in Task 9.6 after the last capture.

If production code, resources, profile or build inputs change, mark the current attempt failed but preserve its entire tree, then restart at **Task 8.2**: rebuild `assembleCertification`, call `freeze_certification_candidate.ps1` to create a new descriptor/id, rerun candidate qualification/profile freeze, and build a new Release attempt. If only a lab capture fails without a production change, also start a new attempt descriptor rather than deleting or appending to the failed frozen/partial tree. Fixed candidate/release output paths are forbidden.

## Task 9: Complete automated, visual, accessibility, gameplay and Release gates

`release-experience-required.json` is the canonical, committed acceptance contract rather than a list of evidence file kinds. Its static rows explicitly enumerate the Cartesian visual matrix (16:9/20:9, both landscape directions, gesture/three-button navigation, Chinese/English/pseudolocale, 1.0/1.3/2.0 fonts and applied/fallback/locked/stale states), named accessibility checks including required human TalkBack rows, and clean-install/same-certificate-upgrade flows. Its dynamic rule deterministically expands sustained-gameplay rows from the two required base configurations in `base-required-matrix.json` plus every exact advanced configuration in the signed profile, crossed with the five frozen fixtures. The expansion is canonical and produces `resolvedRequiredRowsHash`; callers cannot supply a filtered row list.

For Tasks 9.2–9.5, `record_release_experience_gate.ps1` independently resolves that row set from the immutable Release record, base matrix, signed profile and five-fixture manifest, rejects unknown row ids, and creates exactly one immutable sign-off receipt at the canonical row path. Every receipt contains the Release-record/APK/profile/matrix/resolved-row-set hashes, exact row id and dimensions, normalized evidence paths with byte hashes, `PASS`/`FAIL`/`INVALID`/`UNVERIFIED`, capture tool/device identity and timestamps; human-required rows additionally require a named tester, attestation text and sign-off timestamp and cannot be produced by an automation identity. Receipts are written only through the create-new-only helper. A terminal non-PASS receipt fails the attempt and is preserved; retesting requires a new Release attempt, never overwrite or a second receipt.

```powershell
$attempt = Import-PowerShellDataFile .artifacts/certification/active-attempt.psd1
pwsh -NoProfile -File tools/quality/record_release_experience_gate.ps1 -ReleaseRecord $attempt.ReleaseRecord -RequiredExperienceMatrix tools/quality/matrices/release-experience-required.json -RequiredBaseMatrix tools/quality/matrices/base-required-matrix.json -RequiredAdvancedScope tools/quality/matrices/advanced-release-scope.json -RomFixtures local-data/display-quality/rom-fixtures.json -RowId <exact-resolved-row-id> -Verdict PASS -EvidencePath <capture-or-signoff> -Tester <required-for-human-row> -EvidenceRoot $attempt.ReleaseEvidenceRoot
```

`ReleaseExperienceGate.Tests.ps1` removes, duplicates and renames receipts; flips every terminal status and evidence hash; substitutes a profile/base/fixture/matrix hash; omits a human signer; and attempts an extra or filtered dynamic row. The recorder/finalizer must return nonzero in every negative case and on zero-row discovery.

- [ ] **9.1 Run the full clean automated suite**

`release-automation-required.json` fixes the required stage ids and commands: host dependency bootstrap; the exact-version Pester wrapper; absolute-path CMake configure/build and CTest; Gradle unit tests and lint; and non-certification instrumentation tests. `run_automated_release_gates.ps1` consumes `release-record.json`, verifies the explicit device serial for the instrumentation stage, executes every stage as a separate subprocess, hashes the executable/tool identity, arguments, stdout/stderr receipt and exit code, and writes all receipts plus `automated-release-gate-record.json` through the create-new-only helper. Any missing/duplicate stage, wrong/offline/multiple device, nonzero exit or required-matrix mismatch makes the script nonzero; a manually run command is useful diagnostically but never counts toward Release acceptance.

```powershell
$attempt = Import-PowerShellDataFile .artifacts/certification/active-attempt.psd1
pwsh -NoProfile -File tools/quality/run_automated_release_gates.ps1 -ReleaseRecord $attempt.ReleaseRecord -RequiredAutomationMatrix tools/quality/matrices/release-automation-required.json -Serial <verified-serial> -EvidenceRoot $attempt.ReleaseEvidenceRoot -GateRecord (Join-Path $attempt.ReleaseEvidenceRoot 'automated-release-gate-record.json')
```

- [ ] **9.2 Run a reproducible visual matrix**

Run every exact visual row resolved from `release-experience-required.json`; starting named fixed AVDs/densities and capturing an arbitrary subset does not count. Compare Android PNGs to exported approved logical-canvas PNGs with diff images and a human sign-off. HTML CSS pixels are not used directly as Android dp. Check no clipping, sibling overlap, system-bar intrusion or double-scroll. Each row's PNG, hierarchy, diff and human verdict is hashed into its unique release-experience receipt in the still-open Release evidence root and tagged with the release-record and `resolvedRequiredRowsHash` anchors.

- [ ] **9.3 Run accessibility manually and automatically**

Execute every named accessibility row: radio semantics, locked-option explanation, focus restoration, reduced motion, switch labels and live-region rate, plus recorded TalkBack traversal at 1.0 and 2.0 fonts. An accessibility hierarchy dump alone is not human validation. Store the recording, hierarchy and named human verdict in that row's unique receipt; `record_release_experience_gate.ps1` rejects a TalkBack row without the required human signer.

- [ ] **9.4 Run final Release on the verified phone**

Re-verify ADB serial, manufacturer/model, installed certificate and APK hash against `release-record.json` before each exact install/upgrade row. Run clean install and signed upgrade, and exercise the row's required game start, pause/resume, display presets, control editor, vibration-off regression, Surface loss, language switch and certified-configuration checks. A generic “installed” file cannot satisfy either row. These signed-off receipts extend, but never overwrite, the Release evidence created in Task 8.5.

- [ ] **9.5 Sustain gameplay and review artifacts**

Play every deterministically expanded `(base or signed-profile configuration) × fixture` row for at least 30 minutes. Record core-confirmed `SourceTiming`, atomic active id/key residency, crash/ANR, audio drift, stuck input, cadence, thermal fallback and visual artifacts into the row receipt. Motion runs only on NTSC fixtures whose runtime timing matches the manifest; PAL/UNKNOWN negative cases must remain fail-closed. Motion requires two named people for real-time A/B review; screenshots are insufficient and the recorder rejects a Motion receipt without both attestations.

- [ ] **9.6 Freeze evidence-backed completion**

After the final Task 9 capture, freeze the separate content-addressed `$attempt.ReleaseManifest` under `docs/acceptance/display-quality/attempts/<candidate-sha256>/<attemptId>/releases/<release-sha256>/<releaseAttemptId>/`. Its top-level anchors include all three role-specific time-receipt hashes; `RELEASE_COMPLETION` is stored create-new-only outside the evidence root so it cannot be included in the gate set it signs, then independently hashed by manifest generation. Candidate and Release roots are never merged: the former proves qualification of the candidate used to create profiles; the latter proves the final signed APK and all Task 9 regressions.

First finalize the exact experience row set. `finalize_release_experience_gate.ps1` independently extracts and hashes the signed profile from the Release APK, verifies it exactly matches the included advanced scope, combines its configurations with the required base configurations and five frozen fixtures, and recomputes `resolvedRequiredRowsHash`. It requires exactly one valid sign-off receipt for every resolved static and dynamic row. A missing/duplicate/unknown row, invalid evidence hash/sign-off, or any `FAIL`, `INVALID` or `UNVERIFIED` result exits nonzero and no manifest may be generated.

```powershell
$attempt = Import-PowerShellDataFile .artifacts/certification/active-attempt.psd1
pwsh -NoProfile -File tools/quality/finalize_release_experience_gate.ps1 -ReleaseRecord $attempt.ReleaseRecord -ReleaseEvidenceRoot $attempt.ReleaseEvidenceRoot -RequiredExperienceMatrix tools/quality/matrices/release-experience-required.json -RequiredBaseMatrix tools/quality/matrices/base-required-matrix.json -RequiredAdvancedScope tools/quality/matrices/advanced-release-scope.json -RomFixtures local-data/display-quality/rom-fixtures.json -GateRecord (Join-Path $attempt.ReleaseEvidenceRoot 'experience-release-gate-record.json')
```

Now acquire the third, completion-time receipt. It is requested only after all four Release gate records exist, binds the candidate record/manifest, Release record and the canonical ordered hash set of base/profile/automation/experience gates, and independently checks that its trusted epoch is no earlier than any bound capture or human sign-off. The script also extracts the signed profile and requires every certificate-backed row to satisfy `certifiedAt <= completionEpoch < validUntil`; if Task 9 ran past expiry, no Release manifest is generated and re-certification is required. The older `profile-release` receipt cannot satisfy this role.

```powershell
$attempt = Import-PowerShellDataFile .artifacts/certification/active-attempt.psd1
$completionTime = Join-Path $attempt.AttemptRoot 'release-completion-authenticated-time.json'
$gateRecords = @(
  (Join-Path $attempt.ReleaseEvidenceRoot 'base-release-gate-record.json'),
  (Join-Path $attempt.ReleaseEvidenceRoot 'profile-release-gate-record.json'),
  (Join-Path $attempt.ReleaseEvidenceRoot 'automated-release-gate-record.json'),
  (Join-Path $attempt.ReleaseEvidenceRoot 'experience-release-gate-record.json')
)
pwsh -NoProfile -File tools/quality/acquire_authenticated_release_time.ps1 -Role release-completion -AuthorityConfig tools/quality/release-time-authority.json -BindCandidateRecord $attempt.CandidateRecord -BindCandidateManifest $attempt.CandidateManifest -BindReleaseRecord $attempt.ReleaseRecord -BindGateRecords $gateRecords -ReleaseEvidenceRoot $attempt.ReleaseEvidenceRoot -Output $completionTime
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
```

`AuthenticatedReleaseTime.Tests.ps1` and `VerifyReleaseCompletion.Tests.ps1` remove or swap each of the three receipts, mutate role/nonce/authority/context/gate hashes, replay a receipt from another attempt, move completion before one capture, and test completion at `validUntil - 1`, `validUntil` and `validUntil + 1`; only the first expiry boundary may pass.

```powershell
$attempt = Import-PowerShellDataFile .artifacts/certification/active-attempt.psd1
$profileReleaseTime = Join-Path $attempt.AttemptRoot 'profile-release-authenticated-time.json'
$completionTime = Join-Path $attempt.AttemptRoot 'release-completion-authenticated-time.json'
pwsh -NoProfile -File tools/quality/generate_evidence_manifest.ps1 -EvidenceRoot $attempt.ReleaseEvidenceRoot -ArtifactRecord $attempt.ReleaseRecord -ArtifactRecordKind release -CandidateAuthenticatedTime $attempt.CandidateAuthenticatedTime -ProfileReleaseAuthenticatedTime $profileReleaseTime -ReleaseCompletionAuthenticatedTime $completionTime -RequiredMatrix tools/quality/matrices/base-required-matrix.json -RequiredGateRecord (Join-Path $attempt.ReleaseEvidenceRoot 'base-release-gate-record.json') -RequiredAdvancedScope tools/quality/matrices/advanced-release-scope.json -RequiredAdvancedGateRecord (Join-Path $attempt.ReleaseEvidenceRoot 'profile-release-gate-record.json') -RequiredAutomationMatrix tools/quality/matrices/release-automation-required.json -RequiredAutomationGateRecord (Join-Path $attempt.ReleaseEvidenceRoot 'automated-release-gate-record.json') -RequiredExperienceMatrix tools/quality/matrices/release-experience-required.json -RequiredExperienceGateRecord (Join-Path $attempt.ReleaseEvidenceRoot 'experience-release-gate-record.json') -OutputManifest $attempt.ReleaseManifest
```

```powershell
$attempt = Import-PowerShellDataFile .artifacts/certification/active-attempt.psd1
$profileReleaseTime = Join-Path $attempt.AttemptRoot 'profile-release-authenticated-time.json'
$completionTime = Join-Path $attempt.AttemptRoot 'release-completion-authenticated-time.json'
pwsh -NoProfile -File tools/quality/verify_evidence_manifest.ps1 -EvidenceRoot $attempt.ReleaseEvidenceRoot -Manifest $attempt.ReleaseManifest -ArtifactRecord $attempt.ReleaseRecord -CandidateAuthenticatedTime $attempt.CandidateAuthenticatedTime -ProfileReleaseAuthenticatedTime $profileReleaseTime -ReleaseCompletionAuthenticatedTime $completionTime -RequiredMatrix tools/quality/matrices/base-required-matrix.json -RequiredGateRecord (Join-Path $attempt.ReleaseEvidenceRoot 'base-release-gate-record.json') -RequiredAdvancedScope tools/quality/matrices/advanced-release-scope.json -RequiredAdvancedGateRecord (Join-Path $attempt.ReleaseEvidenceRoot 'profile-release-gate-record.json') -RequiredAutomationMatrix tools/quality/matrices/release-automation-required.json -RequiredAutomationGateRecord (Join-Path $attempt.ReleaseEvidenceRoot 'automated-release-gate-record.json') -RequiredExperienceMatrix tools/quality/matrices/release-experience-required.json -RequiredExperienceGateRecord (Join-Path $attempt.ReleaseEvidenceRoot 'experience-release-gate-record.json')
```

```powershell
$attempt = Import-PowerShellDataFile .artifacts/certification/active-attempt.psd1
$profileReleaseTime = Join-Path $attempt.AttemptRoot 'profile-release-authenticated-time.json'
$completionTime = Join-Path $attempt.AttemptRoot 'release-completion-authenticated-time.json'
pwsh -NoProfile -File tools/quality/verify_release_completion.ps1 -CandidateAuthenticatedTime $attempt.CandidateAuthenticatedTime -ProfileReleaseAuthenticatedTime $profileReleaseTime -ReleaseCompletionAuthenticatedTime $completionTime -CandidateRecord $attempt.CandidateRecord -CandidateManifest $attempt.CandidateManifest -CandidateEvidenceRoot $attempt.CandidateEvidenceRoot -BaseCandidateGateRecord (Join-Path $attempt.CandidateEvidenceRoot 'base-candidate-gate-record.json') -AdvancedCandidateGateRecord (Join-Path $attempt.CandidateEvidenceRoot 'advanced-candidate-gate-record.json') -ReleaseRecord $attempt.ReleaseRecord -ReleaseManifest $attempt.ReleaseManifest -ReleaseEvidenceRoot $attempt.ReleaseEvidenceRoot -BaseReleaseGateRecord (Join-Path $attempt.ReleaseEvidenceRoot 'base-release-gate-record.json') -ProfileReleaseGateRecord (Join-Path $attempt.ReleaseEvidenceRoot 'profile-release-gate-record.json') -RequiredMatrix tools/quality/matrices/base-required-matrix.json -RequiredAdvancedScope tools/quality/matrices/advanced-release-scope.json -RequiredAutomationMatrix tools/quality/matrices/release-automation-required.json -RequiredAutomationGateRecord (Join-Path $attempt.ReleaseEvidenceRoot 'automated-release-gate-record.json') -RequiredExperienceMatrix tools/quality/matrices/release-experience-required.json -RequiredExperienceGateRecord (Join-Path $attempt.ReleaseEvidenceRoot 'experience-release-gate-record.json') -RomFixtures local-data/display-quality/rom-fixtures.json
```

`verify_release_completion.ps1` independently verifies all three nonce-bound signed time receipts and their role-specific hash/epoch closure: candidate receipt → candidate record/APK/manifest; context-bound profile-release receipt → profile/final APK/Release record; context-bound completion receipt → candidate anchors, Release record, exact four-gate hash set and Release manifest. Local wall time is not accepted. It proves `completionEpoch >= every Release capture/sign-off epoch` and re-evaluates every certificate at completion, with equality to `validUntil` already expired. It checks both frozen roots for added/missing/changed files; requires every candidate and Release base row to be exactly `PASS`; recomputes the complete advanced scope and requires every `INCLUDED` row in both the signed profile and candidate/Release advanced gates, with no extra or omitted profile row. It requires every committed automation stage exactly once with exit code 0. It then independently expands the experience matrix from the canonical static dimensions, required base modes, exact signed profile and five fixtures, compares `resolvedRequiredRowsHash`, and requires exactly one valid `PASS` receipt for every visual, accessibility, install/upgrade and gameplay row. Evidence-kind presence, a human file without its exact row/sign-off, or a subset of profile/configuration rows can never satisfy completion. Either verifier fails if a signed-time response, trace, CSV, video, PNG, log, receipt, APK/record, profile hash, matrix/gate record or final artifact has been replaced.

The acceptance README records Git commit, candidate/release record and evidence-manifest hashes, all three authenticated-time receipt hashes/epochs including completion, implementation manifests, final APK hash/cert, device identity, core-confirmed `SourceTiming`, target and atomic active configuration id/key, surface epoch/display generation, requested/system-active/physical-evidence rates, core/copy/upload/synthesis/submit counters, temporal state/queues, latency, power/temperature, content-addressed evidence references, failures and retests. Raw device paths and ROM bytes stay ignored. Re-run the implementation manifest generator and assert the production hash is unchanged, then commit only the acceptance prose and canonical Release manifest.

```powershell
git add docs/acceptance/display-quality
git commit -m "docs: record x100 pro display quality acceptance"
```

Before claiming completion, use `requesting-code-review` and `verification-before-completion`, resolve every P0/P1 finding, rerun affected gates, and commit the conclusions.

## Final completion criteria

- Current Balanced is fully applied Sharp until an exact MMPX configuration qualifies; missing roadmap work is never shown as a runtime failure.
- No string equates system refresh with independent game frames or calls system-reported mode a physical measurement.
- Native 60 and native-time 120 submit only new source frames; synthesis is exactly zero.
- Runtime can distinguish Immediate Native, Priming, non-presented Shadow Priming, Motion, Buffered Native Hold, Surface-suspended Hold and Draining, including surface epoch/display generation, atomic active id/key, audio/video delay and queue depth.
- Every renderer failure and adaptive decision flows through one resolver; user preferences survive temporary downgrade.
- MMPX/ScaleFX have pinned sources/notices, capability probes, oracle/goldens and exact complete-configuration evidence before exposure.
- Motion cannot activate without one exact Release profile whose full key includes `NTSC_60_0988`, a fresh same-epoch/same-generation display lease and first-use consent bound to the exact `profileId`, evidence identity and validity interval. Expired or time-untrusted evidence invalidates both qualification and consent.
- Candidate, profile/final artifact and final completion each bind a different role-specific authenticated-time receipt in strict chronology. `PROFILE_RELEASE` equals profile certification time and is no earlier than candidate captures; `RELEASE_COMPLETION` is no earlier than every final capture/sign-off and must still be strictly before certificate expiry. Missing, replayed, swapped, stale or locally fabricated time evidence fails Release.
- On-device advanced modes require a current-boot authenticated network-time anchor; fresh install, state loss or a new boot without it keeps only the builtin base modes available. Same-boot monotonic projection, non-extendable deadlines and retained-state tombstones prevent wall-clock rollback from reviving a certificate.
- Release is installable and signature-verified; clean install and same-certificate upgrade both pass.
- The verified base Release may ship Eco and current Balanced only after Tasks 1–4, Task 8.1–8.2, the base-only Task 8.3a matrix, its Task 8.4–8.6 candidate/final-artifact gates and all Task 9 Release gates pass. Both candidate and final Release gate records must contain the exact committed required-row set with every row `PASS`; profile-row omission cannot bypass this rule. `advanced-release-scope.json` must still enumerate every advanced tuple as `NOT_INCLUDED`, and the empty signed profile, candidate gate, Release gate and final verifier must agree on that exact scope. This path uses the Task 4 shared certification harness and has no Task 5–7 dependency. Missing physical 120Hz, Motion, ScaleFX or MMPX evidence leaves only those configurations `NOT_INCLUDED`/`UNVERIFIED` and locked; it does not manufacture a failure for the verified base modes.
- No Release passes because visual/accessibility/install/gameplay file kinds merely exist. The canonical experience matrix, its deterministically resolved row hash and exactly one signed-off `PASS` receipt for every required row must agree in the release record, Release manifest, experience gate and independent final verifier.
- The full display-quality milestone is incomplete until every mode named for that milestone has its physical refresh, power/thermal, latency, visual-artifact, accessibility and sustained-play evidence. Product copy and release notes must distinguish “base Release complete” from “advanced mode certified”.
