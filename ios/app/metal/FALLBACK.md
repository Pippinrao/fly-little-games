# iOS Metal quality: hashes and fallback

Windows host contracts cannot compile these shaders. macOS CI with Xcode 26.6
is the only place that produces a Metal library. Do not treat a Windows
workspace as Mach-O evidence.

## Algorithm origin

- Nearest / Sharp Bilinear / CRT: ported from `app/src/main/cpp/video/baseline_pipeline.cpp`
- MMPX 2x: ported from `app/src/main/assets/shaders/mmpx/mmpx_2x.frag` (Morgan McGuire / Mara Gagiu, MIT)
- ScaleFX: ported from `app/src/main/assets/shaders/scalefx/scalefx-pass0.glsl` … `pass4.glsl` (Sp00kyFox, MIT)

Android golden fixtures live under `app/src/test/resources/spatial-golden`. A
device qualification run should compare Metal output against those goldens with
the documented one-LSB ScaleFX tolerance. This tree does not claim Windows
produced those pixels.

## Source SHA-256

| Shader | SHA-256 |
|--------|---------|
| Nearest.metal | ac7aa8308399372d27782173c813ddb89ff4bd3fb49bb3080a79409ff6cc0d2f |
| SharpBilinear.metal | d6c687a794903469d17a00af46888c675e4797617aeb2205510d8bdf3d89eb09 |
| Crt.metal | 67b0248a41d28c747370b92cf77182a9596f69d673839d263ae8326166cdcca2 |
| Mmpx2x.metal | d575c05687988f16b4dba85247e81a98546320c75e11bfc9d2f8beb903343a34 |
| ScaleFxPass0.metal | 80284ae73e2bd4c945f3d291bb7f652ad8e5fac71819d3e110cc8491340b6a95 |
| ScaleFxPass1.metal | 0dc847e6ac280aeaccd1f97d8d18915e5860c8900978d4d229c92f013f6ad1d0 |
| ScaleFxPass2.metal | b72fa05f10cc6213dc86c171b918eb12101d7d089be1197ae33fb210fddc6192 |
| ScaleFxPass3.metal | ecb8a9875bb29752c8f352dd839f752fda07edbc7ea6cd9dd2df343047ad17d4 |
| ScaleFxPass4.metal | 2df0e3b1c9b1949ac321facf92fd2a16236568f0cdb8d941fd5b60dc4ad60b3c |

Rebuild of a shader requires updating this table in the same change.

## Atomic fallback

Shader or pipeline creation failure must replace the **entire** pipeline in
one assignment. A half-switched pipeline (new vertex function, old fragment,
or a live encoder using a failed library) is forbidden.

Order:

1. Requested spatial/post pipeline
2. Sharp Bilinear
3. Nearest

Keep presenting the last committed good pipeline until the next commit
succeeds. MMPX and ScaleFX never stay half-initialized.

## Motion compensation (60→120)

Enable 60→120 interpolation **only** when:

- `CADisplayLink` timestamps show a stable ~120 Hz interval, **and**
- present stats (`targetTimestamp` / presented frame counts) agree

Never treat `UIScreen.maximumFramesPerSecond` as 120 Hz evidence. That
property is a capability advertisement, not a measured cadence.

RGB565 upload uses `MTLPixelFormatB5G6R5Unorm`. Presentation aspect modes are
4:3, square-pixel, and integer scale.
