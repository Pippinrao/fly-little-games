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

Windows host builds also pass the verified zlib 1.3.1 install as
`-DZLIB_ROOT=<prefix>`; its header and static-library hashes are checked during
configuration. Android and HarmonyOS builds resolve `zlib.h` and `libz` only
inside the selected SDK sysroot. iOS resolves zlib only from the absolute,
selected Apple SDK; ordinary Linux and macOS builds use CMake's target-aware
`find_package(ZLIB REQUIRED)` and `ZLIB::ZLIB`.

`FLYNES_BUILD_TESTS` defaults to `OFF`. When enabled, CTest registers the ABI,
C-header, ROM-fixture, and bounded-ZIP tests and requires a Python 3 interpreter
for the fixture-generator checks. Production-only builds do not discover or
require Python (or a C compiler).

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

## Bounded ZIP-open parity fixtures

`tests/fixtures/zip/open/v1/manifest.tsv` and its 71 `.zip` blobs are the single
language-neutral ZIP structural/open corpus. Each row supplies all seven scan
limits, a SHA-256, and either the exact stable error code/message or every
published entry metadata field in Java output order. Raw names and central
extras are lowercase hex; raw name plus local-header offset is the exact entry
identity. The JVM `BoundedZipOpenFixtureParityTest` and the CTest targets
`flynes_bounded_zip_archive` and `flynes_zip_open_fixture_loader` consume this
same directory. `flynes_bounded_zip_archive_edges` instead uses inline bytes for
direct API, limit-validation, null-view, and ownership checks.

Version 1 includes descriptor CRC/signature collisions, competing EOCD
candidates inside comments, policy-independent ambiguity checks, and explicit
rejection of prefix bytes. Tightening entry, name, inflated, ratio, encryption,
or compression-method policy cannot turn an ambiguous archive into a valid one.

The manifest and both loaders enforce schema version 1, safe basename-only
paths, unique case/blob names, exact SHA-256 values, regular non-symlink files,
canonical unsigned decimal limits, nonempty success names, stable nonblank
error messages, and exact directory enumeration. CTest runs both the generator's
black-box safety suite and a direct `--check` of the committed corpus. Generate
or non-mutatingly verify it with:

```sh
python shared/tests/fixtures/zip/open/generate_v1.py
python shared/tests/fixtures/zip/open/generate_v1.py --check
```

Generation refuses unexpected entries and reparse/symlink output roots. Files
are replaced atomically so an existing hardlink cannot cause an out-of-tree
alias to be truncated.

ZIP opening stops after central/local headers, compressed byte ranges,
descriptor metadata, declared inflated totals, and ratio validation. It never
eagerly inflates an entry, so an unselected malformed sibling cannot affect a
selected entry.

## Bounded ZIP-payload parity fixtures

`tests/fixtures/zip/payload/v1/manifest.tsv` and its 19 independent `.zip`
blobs freeze the existing Java `Entry.readPayload()` behavior. Each row selects
one entry by raw-name hex plus local-header offset, supplies all seven limits,
and records either exact payload length/SHA-256 or a stable error code/message.
The corpus covers stored and raw-DEFLATE success, empty and directory entries,
signed/unsigned descriptors, an unselected corrupt sibling, limit/error
precedence, malformed/truncated/trailing streams, declared-size mismatch, and
CRC mismatch. RFC 1950 wrapped zlib streams are intentionally rejected because
ZIP method 8 carries raw DEFLATE.

The C++ archive owns one copy of the physical bytes and keeps data offsets in a
private index. A payload read performs exact selection, allocates a new result,
and does not cache it; moving the archive or mutating an earlier returned payload
does not change a later read. The inflater uses bounded 8192-byte steps, checks
actual cumulative output before declared entry size, and reports malformed data
as value errors rather than allowing validation exceptions across the internal
boundary.

The JVM `BoundedZipPayloadFixtureParityTest` and CTest targets
`flynes_bounded_zip_payload`, `flynes_bounded_zip_payload_edges`, and
`flynes_zip_payload_fixture_loader` consume the same corpus. CTest also runs the
generator safety suite and a direct committed-corpus check:

```sh
python shared/tests/fixtures/zip/payload/generate_v1.py
python shared/tests/fixtures/zip/payload/generate_v1.py --check
```

Payload inflation remains an internal catalog rule. It adds no symbol or type to
the public `flynes_app.h` C ABI and does not implement scanning, import,
transport, runtime, or session behavior.

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
