> 历史归档（2026-09-21）：本文的执行顺序、首版范围和进度结论已被[当前最小可玩方案](../../../nearby/README.md)取代。保留供追溯，不作为新增任务或发布承诺。

# Task 2 Report: Shared ControlLayoutV2

## Summary

Implemented `flynes::product::ControlLayoutV2` in the existing `flynes_product` static library, porting the Android codec, validation rules, and recommended defaults verbatim.

## Files Changed

| File | Action |
|------|--------|
| `shared/include/flynes/product/control_layout.hpp` | Created |
| `shared/src/product/control_layout.cpp` | Created |
| `shared/tests/test_product_control_layout.cpp` | Created |
| `shared/CMakeLists.txt` | Modified (`flynes_product` source + test target) |

## TDD Evidence

### RED

Command:

```powershell
$cmake = "C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe"
$env:FLYNES_ZLIB_ROOT = "E:\workspace\codes\games\fly-little-games\.worktrees\ios-harmony-port\.artifacts\host-deps\zlib-1.3.1-install"
& $cmake -S shared -B out/shared -G "Visual Studio 17 2022" -A x64 "-DZLIB_ROOT=$env:FLYNES_ZLIB_ROOT" -DFLYNES_BUILD_TESTS=ON
& $cmake --build out/shared --config Release --target flynes_product_control_layout_test
```

Output (before implementation files existed):

```
CMake Error at CMakeLists.txt:190 (add_library):
  Cannot find source file:

    src/product/control_layout.cpp
```

### GREEN

Command:

```powershell
& $cmake --build out/shared --config Release --target flynes_product_control_layout_test
out\shared\Release\flynes_product_control_layout_test.exe
& $ctest --test-dir out/shared -C Release -R flynes_product --output-on-failure
```

Output:

```
flynes_product_control_layout_test: PASS

    Start 3: flynes_product_game_center
1/2 Test #3: flynes_product_game_center .......   Passed    0.03 sec
    Start 4: flynes_product_control_layout
2/2 Test #4: flynes_product_control_layout ....   Passed    0.03 sec

100% tests passed, 0 tests failed out of 2
```

## Implementation Notes

- **Wire format:** `v2|{opacity:.4f}|LANDSCAPE|{ELEMENT},{x},{y},{scale}` for `D_PAD`, `A`, `B`, `SELECT`, `START` in enum order.
- **Formatting:** `snprintf(..., "%.4f", ...)` for US-locale four-decimal encoding.
- **Validation:** centers in `[0,1]`, scale in `[0.5,1.8]`, opacity in `[0.4,1]`, all finite; throws `std::invalid_argument` on violation.
- **Decode:** throws on malformed input; `decode_or_recommended` catches all exceptions and returns `recommended()`.
- **Recommended defaults:** DPad `(0.10, 0.76)`, A `(0.94, 0.64)`, B `(0.87, 0.86)`, Select `(0.09, 0.28)`, Start `(0.94, 0.28)`, opacity `0.52`, landscape only.

## Self-Review

| Check | Result |
|-------|--------|
| Added to existing `flynes_product`, not a second library | Pass |
| Task 1 `flynes_product_game_center` still passes | Pass |
| Encode element names match Java (`D_PAD`, etc.) | Pass |
| Five elements only, no Pause | Pass |
| `move` / `with_opacity` builder pattern matches brief | Pass |
| `operator==` enables round-trip test | Pass |
| Strict warnings enabled on test target | Pass |
| Only brief-listed files committed | Pass |

### Minor Notes

- C++ uses `decode_or_recommended` / `with_opacity` (snake_case) per brief; wire format and semantics match Java.
- `decode("")` throws (Java throws on null); `decode_or_recommended("")` returns recommended per brief test.

## Commit

```
9b3564e feat(product): share ControlLayoutV2 codec and Android defaults
```

## Fix: Locale-Independent Decimal Encoding

### Finding

`snprintf` / `strtof` follow the process C locale. Under comma-decimal locales, encode could emit `A,0,9400,...` which the comma-delimited decoder rejects. Android uses `Locale.US` (`%.4f` always dot).

### Covering Tests

| File | Tests added |
|------|-------------|
| `shared/tests/test_product_control_layout.cpp` | `test_encode_decode_uses_us_decimal_under_comma_locale`, `test_recommended_wire_format` |

- Sets `French_France.1252` (Windows) / `fr_FR.UTF-8` (POSIX) via `LocaleGuard`, restores on exit.
- Asserts `0.6300` present, `0,6300` absent, round-trip under comma locale.
- Asserts full `recommended().encode()` wire string (`v2|0.5200|LANDSCAPE|D_PAD,...|START,...`).

### Implementation

- `format_float`: `std::ostringstream` with `std::locale::classic()`, `fixed`, `setprecision(4)`.
- `parse_float`: `std::from_chars` (locale-independent, dot decimal only).

### TDD Evidence

RED (before fix, comma-locale test):

```
FAIL: opacity uses dot decimal
FAIL: opacity avoids comma decimal
```

Command:

```powershell
$cmake = "C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe"
$env:FLYNES_ZLIB_ROOT = "E:\workspace\codes\games\fly-little-games\.worktrees\ios-harmony-port\.artifacts\host-deps\zlib-1.3.1-install"
& $cmake --build out/shared --config Release --target flynes_product_control_layout_test
& $ctest --test-dir out/shared -C Release -R flynes_product --output-on-failure
```

GREEN output:

```
flynes_product_control_layout_test: PASS

    Start 3: flynes_product_game_center
1/2 Test #3: flynes_product_game_center .......   Passed    0.01 sec
    Start 4: flynes_product_control_layout
2/2 Test #4: flynes_product_control_layout ....   Passed    0.02 sec

100% tests passed, 0 tests failed out of 2
```

### Fix Commit

```
e61792b fix(product): encode ControlLayoutV2 with locale-independent decimals
```
