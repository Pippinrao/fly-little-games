# Repository instructions

These instructions apply to the whole repository.

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
