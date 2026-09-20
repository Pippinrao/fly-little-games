# TYPO-00.S — freeze typography roles and failing assertions

Work from: `E:\workspace\codes\games\fly-little-games\.worktrees\nearby-ui-acceptance-fixes`
HEAD: `6ec6605de0f5275295fc08b5905fed3d329f37a4` on `codex/nearby-ui-acceptance-fixes`
Do **not** create another worktree. Do **not** git commit (user did not authorize commit). Do **not** push.

## Scene

This is the first card of `docs/superpowers/plans/2026-09-17-nearby-typography-correction-and-completion-plan.md`. It freezes the approved nearby text roles so Harmony/Android/iOS workers can consume them. It does **not** restore native pages, does **not** fix dual_mvp freeze, and does **not** rewrite the nearby UI screen contract.

Authority: original HTML `docs/superpowers/specs/assets/nearby-ui-parity-review.html` (SHA-256 `C69FF8F5CC3A346071DBF2BC6E0F2291232ECB6732A8F38970FFCE1126577C16`) and approved UX `docs/superpowers/specs/2026-09-13-nearby-ui-parity-design.md`. Do not edit those files.

## TDD (mandatory)

NO production checker/schema without a failing test first.

1. Create `tools/quality/tests/test_nearby_typography.py` that constructs **wrong** data and asserts FAIL:
   - muted/subtitle size 14 (must fail; approved is 12)
   - paneTitle/headline size 24 (must fail; approved is 21)
   - action/button size 16 (must fail; approved is 14)
   - inviteCode tracking `.12` / `.12em` (must fail; approved is `.17em`)
   - typography file missing must FAIL
2. Run: `python -m unittest discover -s tools/quality/tests -p test_nearby_typography.py`
3. Capture RED output. Tests must fail because the checker/file is missing or values are wrong — not because of import typos.
4. Then write the **minimum** checker + frozen JSON so the tests go GREEN.
5. Also run `python tools/quality/check_nearby_ui_contract.py .` and keep it passing. If it is already red due to **pre-existing dirty** `shared/schema/nearby_ui_v1.json`, report that; do **not** “fix” that file in this card.

## Write these files only

- NEW `shared/schema/nearby_typography_v1.json`
- NEW `tools/quality/check_nearby_typography.py`
- NEW `tools/quality/tests/test_nearby_typography.py`

Do not modify: wire/ABI, original HTML/UX docs, platform pages, `nearby_ui_v1.json`, `nearby_ui_state.hpp/.cpp`, `test_product_nearby_ui.cpp`, VERSION, Android/Harmony/iOS UI sources. Those dirty files belong to other cards.

## Frozen roles (from the plan; CSS px at default scale)

Each role must be separately named. Do **not** use one `body` token for title/muted/button.

| role | default size | weight | mockup lineHeight | tracking | applies to |
|---|---:|---:|---:|---:|---|
| pageTitle | 18 | 600 | 23.4 | 0 | Nearby h2 (`#fly-existing h2`, `.fn-header h2`) |
| paneTitle | 21 | 600 | 27.3 | 0 | left pane h3 (`#fly-existing h3`) |
| sectionTitle | 15 | 600 | 21.75 | 0 | h4 (`#fly-existing h4`) |
| body | 14 | 400 | 20.3 | 0 | ordinary labels/body (`#fly-existing` root `font:14px/1.45`) |
| muted | 12 | 400 | 17.4 | 0 | helper copy/footer/reason (`.fe-muted`) |
| action | 14 | 400 | 20.3 | 0 | ordinary buttons/tabs (`.fe-button`, `.fn-tab`; `button { font:inherit }`) |
| primaryAction | 14 | 600 | 20.3 | 0 | primary buttons (`.fe-launch`) |
| kicker | 11 | 400 | 15.95 | .13em | small labels above pane title (`.fn-kicker`) |
| inviteCode | 29 | 600 | 40.6 | .17em | invite code (`.fn-code`) |
| codeInput | 28 | 400 | 40.6 | .16em | join-code input (`.fn-code-input`) |

JSON must include, per role: CSS selector(s), default weight, units, applicable nodes. Numeric values are CSS target line boxes. Do not invent a new visual language.

Record **local mockup overrides** as overrides keyed by selector, do not guess default weight:

- `#fly-existing .fe-detail-title h3` → font-size 20 (G00 detail title; not paneTitle 21)
- `#fly-existing .fe-filter` → font-size 13
- `#fly-existing .fe-game-copy strong` → 14 / 600
- `#fly-existing .fe-game-copy small` → 11
- `#fly-existing .fe-thumb` → 11 / 600
- `#fly-existing .fe-count` → 12
- `#fly-existing .fe-list-hint` → 11
- `#fly-existing .fe-search input` → 16 (search field, not a nearby action role)

## Checker behavior

- Validate contract file exists; missing → FAIL.
- Validate every required role has size, weight, lineHeight, tracking, unit, selector, nodes.
- The four negative fixtures in the unit tests MUST fail.
- Measurement-record validation: if a measurement object is supplied, require platform, scaleMode, requestedFontScale, appliedFontScale, effectiveFontSizeLogical, source fingerprint fields, and **reject** records whose actuals are a copy of JSON constants with no native bounds/screenshotPath/platform metadata. Do not accept “I copied the contract numbers” as a native measurement.
- Suggested measurement fields (schema only; do not claim native evidence exists yet):

```json
{
  "elementId": "nearby_entry_subtitle",
  "role": "muted",
  "baseFontSize": 12,
  "effectiveFontSizeLogical": 12,
  "weight": 400,
  "scaleMode": "system",
  "requestedFontScale": 1,
  "appliedFontScale": 1,
  "clipped": false
}
```

Real measurements also need platform, OS version, density, display scale, iOS Dynamic Type category, source fingerprint, package hash, size/safe area, font family, native bounds, screenshotPath. Encode this as the expected record shape; this card does not produce native screenshots.

- Do not globally scale all sizes.
- Do not treat CSS px, Android sp, Harmony fp, iOS pt as the same physical unit in comments/docs inside the schema.

## Existing checker pattern to follow

`tools/quality/check_nearby_ui_contract.py`: load JSON, collect problems, print `FAIL:` lines, exit 1 on any problem, `OK:` on success. CLI: `python tools/quality/check_nearby_typography.py <repo-root>`.

`tools/quality/tests/` currently holds Pester tests plus this new unittest. Import the checker from `tools/quality/` via sys.path. `test_check_nearby_ui_parity.py` lives in `tools/quality/` (not under tests/); do not relocate it.

## After GREEN

Write report to `.superpowers/sdd/task-typo-00s-report.md` with TDD RED/GREEN evidence (commands + relevant output), files changed, and concerns.

Also run `python tools/quality/check_nearby_typography.py .` against the frozen file (must pass).

Do **not** create Swift/ArkTS/XML helpers in this card. Those come in TYPO-01.H / TYPO-02.A / TYPO-03.I and must be product-referenced then.
