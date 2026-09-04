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

`FLYNES_BUILD_TESTS` defaults to `OFF`. When enabled, CTest registers a C++ ABI
contract test and a real C11 consumer/link test.

## ABI and ownership rules

The public header is `include/flynes/flynes_app.h`. Public structures start with
`uint32_t struct_size` and `uint32_t version`. Callers set both fields to the
matching V1 constants. The library accepts a larger structure when its known V1
prefix is present, rejects shorter structures, and rejects unknown versions.

Configuration roots are non-empty NUL-terminated UTF-8 strings. They and the
capabilities pointer are borrowed only during `fly_app_create`; a successful app
owns copies of both roots. App and snapshot handles are opaque. Destroy and
release are NULL-safe.

`fly_catalog_snapshot` returns a separately owned immutable view. It remains
valid after its app is destroyed and must be released explicitly. Catalog entry
text is copied into caller-provided buffers; no temporary or library-owned
string pointer crosses the ABI. A new app's snapshot has generation and count
zero, and an indexed get returns `FLY_RESULT_OUT_OF_RANGE` without modifying the
entry or buffers.
