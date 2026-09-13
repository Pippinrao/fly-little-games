# Repository instructions

These instructions apply to the whole repository.

## What this repository is

FlyNES is an offline NES/Famicom emulator for **Android, HarmonyOS NEXT, and
iOS**: one shared C++ core (NestopiaUE) plus a shared product layer, with a
native UI per platform. It bundles seven licensed homebrew games declared once in
`content/assets/builtin-games.json`; users import their own ROMs.

Layout: `core/` (platform-free emulation + `nes_*` ABI), `shared/`
(cross-platform product layer + `fly_*` app ABI), `app/` Android, `harmony/`
HarmonyOS, `ios/` iOS, `content/` bundled-game source of truth,
`tools/content|versioning|quality/`, `docs/`.

**Read [docs/DEVELOPMENT.md](docs/DEVELOPMENT.md) before platform work.** It
holds the per-platform build, test, and debug commands, the toolchain versions,
the traps that have already cost this repo time, and the invariants the gates
enforce (no product source may name a bundled game; no platform may keep its own
ROM copy; the retired bundled game must stay deleted, binary assets included).

Local-only helper scripts that operate on a private ROM collection live in
`ios/scripts/local/` and are git-ignored on purpose: never commit them, and
never move their inputs into `content/`.

## Preserve local state

- Keep unrelated user changes and untracked evidence. Never clean, reset, stash, move, or delete another worktree's files.
- Never print or commit signing profiles, keystores, passwords, device credentials, private ROMs, or generated packages.
- Build outputs and local evidence belong under ignored directories.

## Version policy

- `VERSION` is the single semantic version source. `VERSION_MAJOR` is the protected major line.
- Only the user may authorize a major-version change. Agents and automation must keep the major unchanged unless the current conversation contains that explicit instruction.
- Install the repository hook with `tools/versioning/Install-GitHooks.ps1`. The pre-commit hook increments PATCH once for every new commit and synchronizes Android and HarmonyOS metadata.
- Create worktrees only with `tools/versioning/New-VersionedWorktree.ps1`; do not call `git worktree add` directly. The allocator serializes access through the shared Git directory and reserves a unique MINOR line.
- Do not hand-edit platform `versionName` or `versionCode`. Run `tools/versioning/Sync-Version.ps1` after an authorized version change.
- Version codes use `major * 1,000,000 + minor * 1,000 + patch`; MINOR and PATCH are limited to 0–999.

## Change and verification policy

- Use test-driven development for product behavior and bug fixes: demonstrate the failing assertion, implement the smallest fix, then run the relevant regression suite.
- Android shared/native changes require host tests and Android unit tests; UI changes require the emulator instrumentation suite.
- Harmony changes require host CTest, Hypium, and a signed-device install when a compatible device is connected. Emulator results never certify physical refresh rate, power, temperature, or latency.
- Do not enable Extreme or other hardware-qualified modes without matching device evidence. Unsupported paths must expose a reason and fall back safely.
- Nearby multiplayer is outside ordinary emulator/rendering work unless the user explicitly includes it.

## Release policy

- Release from `main` with a clean tracked tree and a version-matching tag.
- Sideload packages may use a documented local test signature. Never describe that signature as a store or production certificate.
- Publish source revision, package hashes, test results, and any device-specific install restriction with the artifacts.
