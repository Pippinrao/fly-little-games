# FlyNES shared application ABI

This directory builds the platform-neutral FlyNES application/catalog slice as
the C++17 static library `flynes_app`. It does not depend on Android, iOS,
HarmonyOS, or the emulator core.

## Host build and tests

```sh
cmake -S shared -B out/shared -DFLYNES_BUILD_TESTS=ON
cmake --build out/shared --config Release
ctest --test-dir out/shared -C Release --output-on-failure
```

`FLYNES_BUILD_TESTS` defaults to `OFF`. When enabled, CTest registers the ABI,
C-header, and ROM-fixture tests and requires a Python 3 interpreter for the
fixture-generator check. Production-only builds do not discover or require
Python (or a C compiler).

## ROM parser parity fixtures

`tests/fixtures/rom/v1/manifest.tsv` and its `.bin` files are the single frozen
ROM-recognition corpus. Every manifest row records the exact blob SHA-256, the
recognition result, all analysis fields, and warnings in order. The JVM
`RomPayloadParserFixtureParityTest` reads this directory through its test-resource
source set, while the separate `flynes_rom_payload_parser` CTest reads the same
files directly.

Case IDs must match `[a-z][a-z0-9_]*`; blob fields must be safe basenames matching
`[a-z][a-z0-9_]*\.bin`; SHA-256 values are exactly 64 lowercase hexadecimal
characters. The version directory may contain only `manifest.tsv` and the blobs
referenced exactly once by that manifest.

Do not hand-edit generated blobs or hashes. Verify the tracked corpus without
modifying it with:

```sh
python shared/tests/fixtures/rom/generate_v1.py --check
```

Regenerate version 1 intentionally with:

```sh
python shared/tests/fixtures/rom/generate_v1.py
```

Generation refuses unexpected files or directories and names each entry that
must be removed explicitly. For an intentional contract change, edit the
deterministic generator, use a new version directory/schema when an existing
input or expected output changes, update both parity gates, regenerate, run
`--check`, and review every manifest/hash diff before committing.

## ABI and ownership rules

The public header is `include/flynes/flynes_app.h`. Public structures start with
`uint32_t struct_size` and `uint32_t version`. Callers set both fields to the
matching V1 constants. The library accepts a larger structure when its known V1
prefix is present, rejects shorter structures, and rejects unknown versions.

Configuration roots are explicit byte ranges: each pointer has a `uint32_t`
byte length that excludes any optional terminator. Lengths must be 1 through
`FLY_APP_ROOT_MAX_UTF8_BYTES` (4096), and the ranges need not be NUL-terminated.
Malformed UTF-8 and embedded NUL bytes are rejected. The ranges and capabilities
pointer are borrowed only during `fly_app_create`; a successful app owns exact
copies of both ranges. App and snapshot handles are opaque. Destroy and release
are NULL-safe.

`fly_catalog_snapshot` returns a separately owned immutable view. It remains
valid after its app is destroyed and must be released explicitly. Catalog entry
text is copied into caller-provided buffers; no temporary or library-owned
string pointer crosses the ABI. A new app's snapshot has generation and count
zero, and an indexed get returns `FLY_RESULT_OUT_OF_RANGE` without modifying the
entry or buffers.
