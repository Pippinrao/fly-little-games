# Display-quality implementation preflight

Source commit: `52d8affadd64dae1edb8a952b84e11d91d301651`

## Status

| Area | Status | Consequence |
|---|---|---|
| Immutable design and current-renderer inputs | `FROZEN` | Production implementation may use these references. |
| Windows host dependency/toolchain | `READY` | The pinned bootstrap completed twice and its Pester contract passes. |
| Clean install | `MAY_CONTINUE` | No upgrade artifact is required for clean-install development. |
| Upgrade install | `BLOCKED_BY_MISSING_ARTIFACT` | No provably shipped, release-signed APK/certificate pair is available. |
| ROM certification fixtures | `BLOCKED_UNRESOLVED_FIXTURES` | All five ROM variants are pinned, but every fixed save-state artifact/hash is missing. |
| Physical-device acceptance | `BLOCKED_BY_MISSING_PHYSICAL_DEVICE` | The available named target is an AVD; emulator evidence cannot certify a physical device. |

These blockers must not be converted into passes. They do not block the unblocked implementation or clean-install work.

## Immutable visual baselines

[Reference metadata](../../design/display-quality/reference/metadata.json) is the canonical index for 14 prototype exports and two current-app renderer captures. It records source hashes, viewport, font scale, state, scroll position, AVD identity, and each PNG SHA-256. Every committed image was independently checked for the PNG signature and exact dimensions. The reviewed prototype source is guarded at 81,687 LF bytes / SHA-256 `c79d6b85f61c7075e10e82668965f65b98c0cb132da3ff09267ae1ab907b4491`; metadata separately records the CRLF capture-transport identity. A same-tab CRLF/LF/CRLF comparison produced identical DOM snapshots and byte-identical screenshot output.

Prototype states are frozen at both `2340×1080` and `1280×720`:

- Current / Balanced
- Custom with the expanded axes visibly in frame
- locked Extreme with the lock explanation
- Extreme risk dialog
- system-request fallback
- severe-thermal fallback after the safe drain
- 2.0 font scale

The prototype is an HTML intent reference. It is explicitly **not** an Android dp golden or a pixel-perfect Android acceptance threshold.

The current renderer captures were freshly built from the source commit above, installed on `MIT_Phone_API35`, and captured at `2340×1080`: a From Below title frame and an early gameplay frame frozen by the app pause sheet. The AVD reported API 35, x86_64, physical panel `1080×2340` at 440 dpi, and only `60.000004 Hz`. Its serial at capture was `emulator-5554`; serials are observational and not stable identities. The renderer APK SHA-256 is `933f2e2102a6836f1380774b134515e1dbf25c549647b50a7c5368076c204dd5` and the built-in ROM SHA-256 is `1a3ac4faf4b35640505344059ae5d91dae07cd47e1fb4d9d2a33c76391f1c555`.

## Upgrade-history inventory

`schema 0` is the loader's missing-marker sentinel, not a declared stored schema. The inventory below reflects committed history rather than inferred aliases.

| Schema | Canonical keys | Display values/defaults |
|---|---|---|
| 0 / marker absent | Immediately before schema 1 the same preference file had `controls.haptic_level` and `controls.distinct_ab`; there were no display keys. | No persisted display spelling. |
| 1 | `schema`; `video.aspect`, `video.filter`, `video.refresh`; `controls.layout`, `controls.button_scale`, `controls.vertical_offset`, `controls.opacity`, `controls.joystick_scale`, `controls.dead_zone`, `controls.haptic_level`, `controls.distinct_ab`; `audio.enabled`, `audio.focus_policy`; `general.locale`, `general.autosave`, `general.last_rom`. | Aspect: `FOUR_BY_THREE` (default), `SQUARE_PIXELS`, `INTEGER_SCALE`. Filter: `HQ4X` (default), `NEAREST`, `SMOOTH`. Refresh: `AUTO` (default), `HZ_60`, `HZ_90`, `HZ_120`. |
| 2 | Same canonical key set as schema 1. | Aspect/refresh unchanged. Filter: `EDGE_ENHANCED` (default), `SHARP_BILINEAR`, `NEAREST`, `CRT`. |
| 3 | Schema 2 keys plus `controls.direction_mode`. | Display spellings/defaults unchanged from schema 2. |

The exact schema-introducing commits are `8871809a566100ccf31b03c994065773b6933413` (v1), `ba10977e8e12b67f200c76207696d094faa07b65` (v2), and `fe335f8bc9b10670d80347820a28a70d4995e697` (v3). The only historically evidenced filter migrations are `HQ4X → EDGE_ENHANCED` and `SMOOTH → SHARP_BILINEAR`; `NEAREST` remains directly parseable. There is no persisted `SHARP`, `ADAPTIVE`, or `DEFAULT` spelling. Serialization uses the enum name, so spelling and case are significant.

`AUTO` runtime behavior changed while the stored schema was still v1: an earlier implementation chose the highest same-resolution refresh, while the later v1 implementation preferred 120 Hz and otherwise 60 Hz. Therefore the schema marker alone cannot identify historical AUTO runtime semantics. Upgrade tests must bind both the saved bytes and the source build being upgraded.

### Previously shipped APK/certificate audit

The read-only local APK inventory contained only the following candidates:

| Candidate basename | APK SHA-256 | Signature result |
|---|---|---|
| `app-debug-androidTest.apk` | `062c9154b473508d161864039d759bdc0946049175be8ad9562a2820d1c246a2` | Valid v2 Android Debug certificate; instrumentation APK, not a release candidate. |
| `app-debug.apk` | `be29f348751bdcce01cb861a9c72b8321abcb69722f9d7a7aa4dd0430049c421` | Valid v2 Android Debug certificate. |
| `app-release-local-test-aligned.apk` | `88c189c48d81fc564ef8de07118130792b4443939f2a81c932c25e27078a76b2` | Does not verify; unsigned. |
| `app-release-local-test.apk` | `8411039298a1033e36e9a63933760b9510242a4faf1b0e1a181a9917c317b452` | Valid v2/v3, but signed with the Android Debug certificate. The `local-test` filename supplies no shipped provenance. |
| `app-release-unsigned.apk` | `88c189c48d81fc564ef8de07118130792b4443939f2a81c932c25e27078a76b2` | Does not verify; unsigned and byte-identical to the aligned candidate. |

The debug certificate SHA-256 is `ad710f585bee2e26995939441773ebb38cdcea4d49bb9a694035889500f95bbf` (`C=US, O=Android, CN=Android Debug`). No inspected artifact is evidence of a previously shipped, release-signed APK. Upgrade installation is therefore `BLOCKED_BY_MISSING_ARTIFACT` until the actual prior APK and its expected release certificate digest are supplied and verified.

## Frozen external algorithm inputs

ScaleFX is pinned to `libretro/glsl-shaders@4f4eb801b2dbcaed0a9669a9deec1a098f3623d8`, and only `scalefx/shaders/scalefx-pass0.glsl` through `scalefx-pass4.glsl` are in scope. The [revision notice](../../design/display-quality/reference/scalefx-revision.md) records each upstream Git blob, byte count, SHA-256, author/date notice, and the rule to retain the embedded permission notice. A branch, tag tip, hybrid pass, or similarly named file is not an allowed substitute.

The independent CPU oracle is named `mmpx-cpu-oracle-v1`. Task 5 must commit and hash that oracle before a GPU port is accepted. No ScaleFX shader is to be silently treated as that independent oracle.

## Windows host zlib and canonical toolchain

`tools/quality/bootstrap_host_zlib.ps1` pins the release archive to:

- URL: `https://github.com/madler/zlib/releases/download/v1.3.1/zlib-1.3.1.tar.gz`
- archive SHA-256: `9a93b2b7dfdac77ceba5a558a580e74667dd6fede4585b91eefb60f03b72df23`

It rejects a moving `current/` URL, a wrong archive, output paths outside the repository-owned dependency root, reparse-point escapes, wrong/mismatched CMake or CTest, a stale generator/architecture, a missing VS generator/compiler, compiler/VS/toolset drift, and unbounded child processes. It configures with `BUILD_SHARED_LIBS=OFF`, `Release`, an explicit install prefix, generator `Visual Studio 17 2022`, and architecture `x64`, then builds only the `zlibstatic` target. The bootstrap creates a clean four-file static-only install itself and rejects any DLL, `zlib.lib` import library, reparse point, or unexpected file. It also applies MSVC `/Brepro` to compilation and static-library creation; two recreated-cache runs produced the same installed library hash. A system DLL is never a valid substitute.

The install directory and both manifests are published as one fixed-name rollback transaction. Its private journal identity uses ordinal, type-tagged records for the install root, every directory, every file, and every file hash; the existing canonical preflight hashes remain unchanged. All three pending artifacts and, on upgrade, all three rollback copies are hash-verified before the journal marker is published. Before any final-state mutation or evidence cleanup, startup recovery validates every present journaled pending/final artifact and every required rollback artifact, including empty-directory topology. Marker deletion is the sole commit point. Recovery runs before previous-manifest integrity checks and fails closed on a corrupt journal, modified journaled topology, incomplete/corrupt rollback, noncanonical managed path, or reparse point. Subprocess fault injection at the three supported publication cut points—after old-install removal, after new-install publication, and after publication of only the toolchain manifest—proves that first-run and upgrade recovery restore a consistent pre-run state without touching an unmanaged sentinel; no broader arbitrary-instruction crash guarantee is claimed.

The canonical ignored preflight manifest records:

| Item | Frozen value |
|---|---|
| CMake | `3.22.1-g37088a8-dirty`; SHA-256 `41d609bae2a65a9a8e2060bb222d6e031d33c0546054d354137eb490933cb8ac` |
| CTest | `3.22.1-g37088a8-dirty`; SHA-256 `9a52248ac13e32df80210528784eeddaac7977d77f242814c5b8b47feb6a92f3` |
| Generator / architecture | `Visual Studio 17 2022` / `x64`; multi-config `true` |
| Compiler | `MSVC 19.44.35227.0`; executable SHA-256 `9eb43db58d6d07b5f552ec11be86a01293df4589bba04ef383548add5f0fdc9f` |
| VS instance / toolset / Windows SDK | `BuildTools` / `v143` / `10.0.26100.0` |
| Toolchain manifest | SHA-256 `775933aff7b2a65e044b48912cb126b27c9ac9aac4d989bbf5547bf9ec1bb6d9` |
| Extracted tree | canonical SHA-256 `61a712eab0f8b66e86ff99290b0e672fc2d701c4e7ad506d3e0fa3b8a304fcab` |
| Installed `zlib.h` | SHA-256 `8a5579af72ea4f427ff00a4150f0ccb3fc5c1e4379f726e101133b1ab9fc600c` |
| Installed `zconf.h` | SHA-256 `b4962930aabbbc4b54a67220b4b3ed2cae69a13688c06a564bda1bc7429d2ab2` |
| Installed `zlibstatic.lib` | SHA-256 `64ebc5489d54d283af93a614c48c82443f9cf146b04db728c57a008f4a4f3a45` (identical across the final two recreated-cache runs) |
| Installed license | SHA-256 `845efc77857d485d91fb3e0b884aaa929368c717ae8186b66fe1ed2495753243` |
| Exact four-file install tree | canonical SHA-256 `bbc819ff1ebcbe9006a2f2038b1dcd50f64cc5fa774bc685068df4aa40af924b` |

On this host, default MSBuild FileTracker launch left a compiler process suspended after its tracker parent vanished. A direct compiler probe passed; default MSBuild timed out; and a fresh CMake probe completed only when `TrackFileAccess=false` was propagated into `try_compile`. The bootstrap therefore disables MSBuild node reuse and consistently applies `-DCMAKE_VS_GLOBALS=TrackFileAccess=false`, `-DCMAKE_TRY_COMPILE_CONFIGURATION=Release`, and `-DCMAKE_TRY_COMPILE_PLATFORM_VARIABLES=CMAKE_VS_GLOBALS;CMAKE_TRY_COMPILE_CONFIGURATION`, in addition to a fail-closed child timeout. Each child is created suspended, assigned to a kill-on-close Windows Job Object, then resumed; one monotonic deadline covers root exit, capped stdout/stderr drains, tree termination, and an `ActiveProcesses == 0` cleanup proof. A descendant that holds either redirected stream after its root exits is a bounded nonzero failure. A job-owned Visual Studio helper that does not hold either stream is terminated and proved gone before the root result is honored. This is a host-specific deterministic workaround and containment boundary, not a relaxed gate.

The probe and zlib build directories are deleted and configured anew on every run after any stale generator/architecture check. The ignored manifest records both fresh `CMakeCCompiler.cmake` hashes/timestamps. In the final run, the probe identity was written at `2026-08-26T00:00:11.1875705Z`, the dependency identity at `2026-08-26T00:00:15.3497493Z`, the toolchain manifest at `2026-08-26T00:00:19.7137209Z`, and the preflight manifest at `2026-08-26T00:00:19.7307840Z`; both identity files have SHA-256 `98bdded1e548dbda5f379b3409a68fc0a6487ce0cbff98d01e95b2d850205cb2` and resolve to the same compiler executable/hash, VS instance, toolset, and SDK.

All later Windows host commands must import ignored `.artifacts/host-deps/host-toolchain.psd1` and use its absolute `CMakeExe`, `CTestExe`, generator, and architecture. Bare PATH-dependent `cmake` or `ctest` is not accepted.

## Frozen ROM fixture identities

The ignored local manifest was validated against [the committed schema](../../../tools/quality/schemas/rom-fixtures.schema.json), and its path-free projection was validated at [rom-fixtures.redacted.json](rom-fixtures.redacted.json). The committed example is at [rom-fixtures.example.json](../../../tools/quality/examples/rom-fixtures.example.json). The committed projection contains hashes and instructions only—no ROM source path, archive-entry path, device URI, or ROM/save-state bytes.

The schema requires `localSources` for every fixture only when `manifestKind` is `local`, and forbids that entire property for `redacted` and `example` manifests. Regression cases reject both absolute and relative `packagePath` and `entryLeaf` leaks. A fixture's selected variant hash must not be repeated in its alternative list.

| Canonical fixture | Variant status | Source timing | Fixed state |
|---|---|---|---|
| `from-below-1-0` | resolved: `1a3ac4fa…f1c555` | NTSC/default header; runtime confirmation required | **missing** |
| `super-mario-bros-pal` | resolved: `ec299b99…53586`; local J label rejected because the database match says NES-PAL/dump=ok | PAL 50.007 | **missing** |
| `contra-nes-ntsc` | resolved: `26541a55…e5519`; database match NES-NTSC/dump=ok | NTSC 60.0988 | **missing** |
| `tetris-bullet-proof-famicom` | resolved: `6552ed20…d01e6`; database match Famicom/dump status unknown | NTSC 60.0988 | **missing** |
| `downtown-nekketsu-monogatari-famicom` (`热血物语` / River City Ransom work) | resolved: `51a6172c…22076`; database match Famicom/dump status unknown | NTSC 60.0988 | **missing** |

The exact ROM choices are no longer ambiguous, but certification remains blocked until all five fixed save states are created outside Git, their SHA-256 values are populated in the ignored manifest, the redacted projection is regenerated, and the core confirms runtime `SourceTiming` matches. The audited commercial ROM-root source bytes were only read and hashed; none were copied, renamed, modified, or deleted.

## Gate evidence

- Exact-Pester wrapper self-tests: `7 passed, 0 failed`. The wrapper imports only Pester `3.4.0` by `RequiredVersion`, rejects any other loaded version, recursively discovers only `*.Tests.ps1`, invokes explicit paths with `-PassThru`, and returns nonzero for a failed or empty fixture.
- zlib bootstrap tests through the wrapper: `53 passed, 0 failed`, including wrong/mismatched tool versions, missing generator/compiler, stale generator/architecture, outside/reparse/no-side-effect containment, inherited-pipe process-tree timeout/no-survivor proof, isolated-helper cleanup, Release try-compile and FileTracker propagation, fresh-cache recreation, true first-run/upgrade crash recovery for all three transaction cut points, corrupt/orphaned-rollback fail-closed behavior, exact static-tree rejection, compiler/VS/toolset drift, disposable-source coverage, and nine rollback/pending/final empty-directory add/delete/rename attacks. Each topology attack returned nonzero before final mutation or evidence cleanup and preserved the complete evidence snapshot. The inherited-pipe regressions each remained under their three-second bound.
- Real zlib bootstrap: the final two recreated-cache runs completed successfully in `12.24 s` and `12.03 s`; `zlibstatic.lib` remained SHA-256 `64ebc5489d54d283af93a614c48c82443f9cf146b04db728c57a008f4a4f3a45`, the exact install contained no DLL or import library, and no transaction temporary survived.
- ROM schema validation: `13 passed, 0 failed`; local, redacted, and example manifests all passed, both committed manifest kinds reject all four path-leak cases, and local/redacted selected hashes do not recur in their alternative lists.
- Complete pinned-Pester quality gate: `73 passed, 0 failed` in `81.14 s`; the focused bootstrap-only run was `53 passed, 0 failed` in `75.36 s`.
- Current app: `:app:assembleDebug` succeeded after initializing the already-pinned Nestopia submodule; APK install and cold launch succeeded on the named AVD.

The final commit gate must rerun both Pester files through the wrapper, the JVM baseline, manifest checks, PNG signature/hash checks, `git diff --check`, and a staged scan for absolute paths, URI values, ROM bytes, and unintended ignored artifacts.
