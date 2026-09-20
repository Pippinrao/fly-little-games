> 历史归档（2026-09-21）：本文的执行顺序、首版范围和进度结论已被[当前最小可玩方案](../../../nearby/README.md)取代。保留供追溯，不作为新增任务或发布承诺。

# TYPO-01.H — Harmony restore roles + scale entry

**Status:** DONE_WITH_CONCERNS

**HEAD:** `6ec6605` on `codex/nearby-ui-acceptance-fixes`  
**Commits created:** none (not authorized)

## Files changed (this card)

NEW:

- `harmony/entry/src/main/ets/ui/NearbyTypography.ets`
- `harmony/entry/src/main/ets/ui/NearbyTestFontScale.ets`
- `harmony/entry/src/ohosTest/ets/test/NearbyUxRestoration.test.ets`
- `tools/quality/tests/test_nearby_harmony_typography.py`
- `.superpowers/sdd/task-typo-01h-report.md`

MODIFIED:

- `harmony/entry/src/ohosTest/ets/test/List.test.ets` (registers NearbyUxRestoration)
- `harmony/entry/src/main/ets/entryability/EntryAbility.ets`
- `harmony/entry/src/main/ets/pages/NearbyFriends.ets`
- `harmony/entry/src/main/ets/pages/NearbyPairing.ets`
- `harmony/entry/src/main/ets/pages/NearbyLobby.ets`
- `harmony/entry/src/main/ets/pages/NearbyFriendsManage.ets`

Did not modify GameCenter, native engine, `nearby_ui_v1.json` / `nearby_ui_state`, `VERSION`, or icon glyph sizes (back 26 / info 16 / gear 18). `AppScope` `fontSizeScale` remains `followSystem`.

## TDD RED

Python host checks first, against current source (helper missing; N00 subtitle 14 / button 16; Want set `{1, 1.5, 2}`):

```
python -m unittest discover -s tools/quality/tests -p test_nearby_harmony_typography.py -v
```

```
FAIL: test_typo_h_n00_roles_helper_and_pages
AssertionError: False is not true : NearbyTypography.ets missing; N00 still uses subtitle 14 / button 16 (must become muted 12 / action 14)

FAIL: test_typo_h_scale13_want_is_accepted_and_logged_on_reject
AssertionError: False is not true : debug Want 1.3 is ignored; allow {1, 1.3, 1.5, 2}

FAIL: test_typo_h_scale2_min_tap_grows_and_pages_scroll
AssertionError: False is not true : scale-2 layout helpers live in NearbyTypography.ets

Ran 4 tests in 0.002s
FAILED (failures=3)
```

RED reason: missing helper / ignored 1.3 / fixed-height clipping helpers, not an import typo. Hypium file `NearbyUxRestoration.test.ets` was registered in `List.test.ets` before production existed (would not compile until the helper landed).

## TDD GREEN

Helper tokens + four Nearby pages import/consume `NEARBY_TYPE`. Action/primaryAction 14; muted 12; paneTitle 21/600/27.3; inviteCode 29/600 with `.17em` → letterSpacing 4.93; kicker `.13em` → 1.43. Text actions use `constraintSize minHeight 48` (grows); N00 subtitle/footer no longer `maxLines`+ellipsis. Debug Want parse accepts `{1, 1.3, 1.5, 2}`, logs `rejected=` for `1.2`/garbage, and `resolveTestFontSizeScale(..., false)` ignores override. `followSystem` stays true.

```
python -m unittest discover -s tools/quality/tests -p test_nearby_harmony_typography.py -v
```

```
....
Ran 4 tests in 0.004s
OK
```

```
python tools/quality/check_nearby_typography.py .
```

```
OK: nearby typography contract valid; 10 roles frozen at CSS px default scale; no native measurements claimed.
```

```
python tools/quality/check_nearby_ui_contract.py .
```

```
OK: nearby UI contract valid; 137 strings covered on android/harmony/ios; C01-C18 fixture present; no network results generated.
```

```
python -m unittest discover -s tools/quality/tests -p test_nearby_typography.py
```

```
Ran 8 tests in 0.034s
OK
```

Hypium cases in `NearbyUxRestoration.test.ets`: `TYPO_H_N00_roles`, `TYPO_H_scale13_applied`, `TYPO_H_scale2_reachable` (zh+en, 640×360, min 48vp that grows, no font shrink, no ellipsis-as-pass).

## HAP / emulator

```
hdc list targets → 127.0.0.1:5557
param get const.product.model → emulator
param get const.product.name → emulator
param get const.product.devicetype → phone
uname → Linux localhost ... x86_64
```

Confirmed emulator (not physical).

```
hvigor --mode module -p product=default -p buildMode=debug assembleHap --no-daemon
ENTRY_HAP_EXIT=0
BUILD SUCCESSFUL in 31 s 765 ms
WARN: Will skip sign 'hos_hap'. No signingConfigs profile is configured

hvigor --mode module -p product=default -p module=entry@ohosTest -p buildMode=debug assembleHap --no-daemon
OHOSTEST_HAP_EXIT=0
BUILD SUCCESSFUL in 7 s 876 ms
WARN: Will skip sign 'hos_hap'. No signingConfigs profile is configured
```

ohosTest ArkTS compile includes `NearbyUxRestoration.test.ets` (typecheck GREEN). Unsigned HAP was not installed. No local signingConfigs / `.p12` in this worktree; credentials were not copied.

**Hypium on-device: NOT_RUN** (unsigned HAP; install requires existing local debug signing).

No `config.fontSizeScale` measurement after Want `1.3`. No 640×360 scale-2 screenshot of long reason / primary / back.

## Remaining gaps

- On-device Hypium (`TYPO_H_*`) not executed; unit/host evidence only.
- Scale 1.3 applied vs still-1 is proven by parse table, not by reading emulator `config.fontSizeScale`.
- Scale 2 reachability is helper math + static layout (minHeight, Scroll, no ellipsis), not a measured 640×360 UI dump.
- `setFontSizeScale` ArkTS warn: API since SDK 12-compatible project (pre-existing EntryAbility path).
- Do not claim L3 playable or real-device PASS.
