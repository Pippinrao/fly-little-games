> 历史归档（2026-09-21）：本文的执行顺序、首版范围和进度结论已被[当前最小可玩方案](../../../nearby/README.md)取代。保留供追溯，不作为新增任务或发布承诺。

# TYPO-00.S — freeze typography roles and failing assertions

**Status:** DONE

**HEAD:** `6ec6605` on `codex/nearby-ui-acceptance-fixes`  
**Commits created:** none (not authorized)

## Files changed (this card only)

NEW:

- `shared/schema/nearby_typography_v1.json`
- `tools/quality/check_nearby_typography.py`
- `tools/quality/tests/test_nearby_typography.py`
- `.superpowers/sdd/task-typo-00s-report.md`

Did not modify wire/ABI, original HTML/UX docs, platform pages, `nearby_ui_v1.json`, native UI sources, or VERSION. Pre-existing dirty UX/engine files were left untouched.

## TDD RED

Wrote `tools/quality/tests/test_nearby_typography.py` first (wrong muted 14, paneTitle 24, action 16, inviteCode tracking `.12`/`.12em`, missing file, copied-constant measurement). Production checker/JSON did not exist yet.

```
python -m unittest discover -s tools/quality/tests -p test_nearby_typography.py
```

```
E
======================================================================
ERROR: test_nearby_typography (unittest.loader._FailedTest.test_nearby_typography)
----------------------------------------------------------------------
ImportError: Failed to import test module: test_nearby_typography
Traceback (most recent call last):
  ...
  File "...\tools\quality\tests\test_nearby_typography.py", line 19, in <module>
    import check_nearby_typography as checker  # noqa: E402
ModuleNotFoundError: No module named 'check_nearby_typography'

----------------------------------------------------------------------
Ran 1 test in 0.000s

FAILED (errors=1)
```

RED reason: checker missing, not an import typo.

## TDD GREEN

Minimum checker + frozen JSON with the ten named roles (not one `body` token), mockup overrides without guessed weights, and measurement-record rejection for copied JSON constants.

```
python -m unittest discover -s tools/quality/tests -p test_nearby_typography.py
```

```
........
----------------------------------------------------------------------
Ran 8 tests in 0.016s

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

Negative fixtures fail as required: muted/subtitle 14, paneTitle/headline 24, action/button 16, inviteCode tracking `.12`/`.12em`, missing `nearby_typography_v1.json`. Copied contract numbers without platform/native bounds/screenshotPath/fingerprint are rejected.

## Frozen roles (CSS px at default scale)

| role | size | weight | lineHeight | tracking |
|---|---:|---:|---:|---:|
| pageTitle | 18 | 600 | 23.4 | 0 |
| paneTitle | 21 | 600 | 27.3 | 0 |
| sectionTitle | 15 | 600 | 21.75 | 0 |
| body | 14 | 400 | 20.3 | 0 |
| muted | 12 | 400 | 17.4 | 0 |
| action | 14 | 400 | 20.3 | 0 |
| primaryAction | 14 | 600 | 20.3 | 0 |
| kicker | 11 | 400 | 15.95 | .13em |
| inviteCode | 29 | 600 | 40.6 | .17em |
| codeInput | 28 | 400 | 40.6 | .16em |

Overrides recorded by selector only; weight omitted unless the mockup sets it. Units note that CSS px, Android sp, Harmony fp, and iOS pt are not the same physical unit. No global scale.

## Concerns

- `shared/schema/nearby_ui_v1.json` is still pre-existing dirty from other cards. `check_nearby_ui_contract.py` currently **passes**; this card did not rewrite it.
- Schema encodes the expected native measurement shape only. This card produced no screenshots or native bounds.
- Platform helpers (Harmony/Android/iOS) are out of scope (TYPO-01.H / TYPO-02.A / TYPO-03.I).
