# FlyNES Alpha Productization Program Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Deliver a public-playtest-quality FlyNES Alpha in 8–10 weeks with correct input, frame pacing, per-ROM data safety, product UI, i18n, controllers, accessibility, and release gates.

**Architecture:** Work is split into six independently testable plans joined by five stable contracts: `InputRouter`, `EmulationSession`, `FramePublisher`, `RomIdentity`, and `SettingsRepository`. Runtime correctness is a release gate; UI and library work may proceed in parallel only after their required contracts land.

**Tech Stack:** Java 17, Android Views/XML, AndroidX, Material Components 1.14.0, Room 2.8.4, C++17, JNI/C ABI, NestopiaUE, OpenGL ES, JUnit 4, AndroidX Test/Espresso, CMake, GitHub Actions.

---

## Source of truth

- Approved design: `docs/superpowers/specs/2026-08-21-flynes-productization-alpha-design.md`
- Audit evidence: `docs/audits/2026-08-21-demo-product-readiness-audit.md`
- Baseline commit before planning: `8e76002`

## Plan set and execution order

| Order | Plan | Primary owner | Calendar | Hard dependency |
|---|---|---|---|---|
| 1 | `2026-08-21-flynes-alpha-foundation-input-session.md` | Android lead + shared | Week 1–2 | None |
| 2A | `2026-08-21-flynes-alpha-frame-audio.md` | Native/runtime lead | Week 2–4 | Session/CoreFacade |
| 2B | `2026-08-21-flynes-alpha-save-rom-switch.md` | Native/runtime lead | Week 2–4 | Session + RomIdentity |
| 3 | `2026-08-21-flynes-alpha-ui-settings.md` | Android lead | Week 4–7 | Session, SettingsRepository, FramePublisher |
| 4 | `2026-08-21-flynes-alpha-library-i18n.md` | Android lead | Week 5–8 | RomIdentity + Room schema |
| 5 | `2026-08-21-flynes-alpha-controller-accessibility-release.md` | Shared | Week 7–10 | InputRouter + product UI |

Plans 2A and 2B may run in parallel after Plan 1 Task 5. Plans 3 and 4 may overlap after their contracts are merged. Plan 5 begins only when the product navigation and final gamepad node bounds are stable.

## Approved-design coverage

| Design section | Implemented by |
|---|---|
| 5.1 session state machine | Foundation Tasks 5–6; Save/ROM Task 5 |
| 5.2–5.3 unified input, ergonomics, A/B haptics | Foundation Tasks 2–4; Controller/Accessibility Tasks 1–3 |
| 5.4 frame publishing/presentation and high refresh | Frame/Audio Tasks 1, 3–5, 7 |
| 5.5 audio engine | Frame/Audio Tasks 2 and 6–7 |
| 5.6–5.7 ROM identity, saves, and switching | Save/ROM Tasks 1–6 |
| 5.8 settings | UI/Settings Tasks 2, 5–6 |
| 5.9 library and offline titles | Library/i18n Tasks 1–5 |
| 6 navigation, visual system, i18n, accessibility | UI/Settings Tasks 1, 3–7; Library/i18n Tasks 6–8; Controller/Accessibility Task 3 |
| 7 error/degradation model | Save/ROM Tasks 2–6; Library/i18n Tasks 2–4 and 7; Frame/Audio Task 6 |
| 8–9 ownership and delivery stages | This program's owner/order/cadence tables |
| 10 test strategy and release gates | Every plan's focused tests; Controller/Accessibility Tasks 4–6 |
| 11 release and compliance | UI/Settings Task 7; Controller/Accessibility Tasks 4 and 6 |
| 12 scope control | Week 10 buffer and child-plan definitions of done |

## Worktree and branch setup

- [ ] **Step 1: Create an isolated implementation worktree**

Run from `E:\workspace\codes\games\fly-little-games`:

```powershell
git worktree add ..\fly-little-games-alpha -b codex/flynes-alpha
```

Expected: a new worktree at `E:\workspace\codes\games\fly-little-games-alpha` on branch `codex/flynes-alpha`.

- [ ] **Step 2: Establish the clean baseline**

```powershell
Set-Location E:\workspace\codes\games\fly-little-games-alpha
$env:ANDROID_HOME = Join-Path $env:LOCALAPPDATA 'Android\Sdk'
$env:ANDROID_SDK_ROOT = $env:ANDROID_HOME
.\gradlew.bat :app:assembleDebug :app:lintDebug --console=plain
.\core\build\host\Release\nes_core_test.exe
```

Expected: Gradle `BUILD SUCCESSFUL`; core test `PASS (0 failures)`.

## Two-person integration cadence

| Day | Developer A: Android/product | Developer B: native/runtime | Shared gate |
|---|---|---|---|
| Monday | Pull integration branch; select plan tasks | Pull integration branch; select plan tasks | Confirm interfaces unchanged |
| Wednesday | Merge one green vertical increment | Merge one green runtime increment | Run assemble/lint/unit/core |
| Friday | Device/UI smoke | Timing/save fault smoke | Tag weekly integration build |

No branch may remain unmerged for more than five working days. Changes to `InputRouter`, `CoreFacade`, `PublishedFrame`, `RomIdentity`, or `SettingsRepository` require cross-owner review.

## Milestone gates

### Gate A — Week 2: Input and lifecycle correctness

- START emits only NES START; independent pause freezes the session.
- A/B and START/SELECT hit regions have zero overlap.
- `ACTION_CANCEL`, focus loss, pause, and detach publish mask 0.
- Deterministic stop-before-start test passes 1,000 iterations.

### Gate B — Week 4: Runtime and data safety

- NTSC source frames 59.9–60.1; PAL 49.9–50.1.
- No UI/native framebuffer race under sanitizer and stress tests.
- ROM A→B→A autosave/SRAM remain isolated.
- Bad ROM and injected filter allocation failure keep the current game usable.

### Gate C — Week 7: Product-complete Alpha

- Home/Library, Game, Pause, Settings, Licenses use the new theme and navigation.
- 4:3/square-pixel/integer-scale modes and Auto/60/90/120 settings work.
- English and Simplified Chinese core flows are complete.
- Source add/refresh/rebind, search no-result, recent, and favorite flows work.

### Gate D — Week 9: Release candidate

- USB/Bluetooth controller and TalkBack smoke pass.
- API 24/29/34/36, 60/90/120 Hz, gesture/three-button navigation matrix passes.
- GPL source URL, built-in ROM evidence, target API 36, and release checklist pass.

### Week 10 buffer

Only release blockers, performance regressions, accessibility blockers, and compliance failures may enter Week 10. Free-drag controls, advanced shaders, complete title databases, and manual multi-slot saves move to Later if they threaten Gate D.

## Global verification command

Run after every plan and before every milestone tag:

```powershell
$env:ANDROID_HOME = Join-Path $env:LOCALAPPDATA 'Android\Sdk'
$env:ANDROID_SDK_ROOT = $env:ANDROID_HOME
.\gradlew.bat :app:assembleDebug :app:lintDebug :app:testDebugUnitTest --console=plain
.\core\build\host\Release\nes_core_test.exe
git diff --check
```

Expected: all commands exit 0, Gradle reports `BUILD SUCCESSFUL`, and the core reports `PASS (0 failures)`.

## Program completion definition

- [ ] All six implementation plans have every checkbox completed.
- [ ] Gates A–D have dated evidence under `docs/acceptance/alpha/`.
- [ ] No open P0 issue from the audit remains.
- [ ] A signed release-candidate APK passes the physical-device matrix.
- [ ] Release source and content-license links are valid and immutable.
