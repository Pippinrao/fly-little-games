# Spatial reconstruction acceptance record

## Frozen sources

- Independent MMPX oracle commit:
  `a0d5540c7b21889da6a432a79c1553518a4d4d14`.
- Official MMPX supplement SHA-256:
  `1211c4b59d5bea3c5ebeb1dc3b31fe8a2f0c58f64955cee50e939f96543996e1`.
- ScaleFX source repository: `https://github.com/libretro/glsl-shaders`.
- ScaleFX source commit: `4f4eb801b2dbcaed0a9669a9deec1a098f3623d8`.
- ScaleFX configuration: standard five passes, default parameters, 3x final output.
- Bundled MMPX implementation composite SHA-256:
  `92256e17caa772247282d3bd185f236e35be964e65675d7f19af686ea1987a0d`.
- Bundled ScaleFX implementation composite SHA-256:
  `9689c6c12ec3caefdc2494181dd24c3c521f9ecebbd5d5c4f944265127b44140`.

The implementation hashes are SHA-256 over sorted `repository-path NUL file-sha256` rows for
the shared spatial pipeline/program/FBO/baseline files plus each algorithm's pass and shader
files, with text line endings canonicalized to LF. They are packaged by
`BundledAlgorithmAvailability`; a future qualified profile must match them exactly.

The MMPX expected arrays were produced by the official JavaScript reference before the GPU
port existed. The ScaleFX goldens were produced by an Android-test-only Java GLES executor
that runs the unmodified, pinned libretro shaders; the production implementation is native
C++ and has separate EGL, program, framebuffer and pass-binding code.

## Pixel contract

- Nearest 2x: exact packed `AABBGGRR` equality.
- MMPX 2x: exact equality against the frozen independent oracle; no tolerance.
- ScaleFX 3x: each RGBA channel must be within one LSB of both the frozen golden and the
  independent upstream executor.
- Fixtures cover solid fields, diagonal edges, one-pixel lines, checkerboards, palette/corner
  transitions, transparency, narrow edges and odd dimensions.
- Source overscan is fixed to zero for this release. Aspect/PAR and final sharp composition
  occur after integer reconstruction.

## Capability and failure contract

- MMPX requires fragment `highp` and a texture size of at least 512.
- ScaleFX requires fragment `highp`, a texture size of at least 768, and an actually rendered,
  sampled and read-back float or half-float color target. Extension strings alone are not
  accepted as proof.
- Shader, framebuffer and GL failures are counted by the native presenter. The failed mode is
  latched, the current frame receives explicit emergency Nearest output, and runtime status
  exposes `RUNTIME_FAILURE`; ScaleFX is never silently replaced with MMPX.
- GPU duration is reported only by `EXT_disjoint_timer_query`. Disjoint samples are discarded;
  unsupported or pending timing remains `UNAVAILABLE/PENDING` with duration `-1`, never CPU
  wall time.
- Advanced modes remain unavailable to normal user resolution when no exact device-quality
  certificate matches, even when their implementation conformance tests pass.
- Normal gameplay now consumes only `DisplayQualityResolver` output. The legacy view adapter
  accepts the immutable effective snapshot (including real MMPX/ScaleFX filter ids), never the
  raw advanced request. Native shader/FBO/GL failures are mapped back into `RuntimeFailure`,
  re-resolved, and atomically published as a stable configuration or explicit transition.

## Fixed AVD evidence

Device: `MIT_Phone_API35`, Android 15, x86_64. Renderer reported by SurfaceFlinger:
`Android Emulator OpenGL ES Translator (NVIDIA GeForce RTX 4070 Ti)`, OpenGL ES 3.1.

Passing connected tests:

- `SpatialFilterGoldenTest`: Nearest, MMPX and ScaleFX contracts across six fixtures.
- `SpatialCapabilityFallbackTest`: concrete probe facts and fail-closed resolver behavior.
- `NativePresenterIntegrationTest`: every renderer mode compiles and submits through the
  real event-driven window surface without runtime fallback; GPU timing truth contract; normal
  gameplay publishes one atomic resolver configuration id/key.

This AVD record is implementation evidence only. It is not a device-quality certificate and
does not qualify MMPX or ScaleFX on a physical phone.
