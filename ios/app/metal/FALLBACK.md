# iOS Metal quality: hashes and fallback

Windows host contracts cannot compile these shaders. macOS with Xcode 14.3.1 or newer
produces the Metal library. Do not treat a Windows
workspace as Mach-O evidence.

## Algorithm origin

- Nearest / Sharp Bilinear / CRT: ported from `app/src/main/java/com/flynes/emu/video/FrameRenderer.java`
- MMPX 2x: ported from `app/src/main/assets/shaders/mmpx/mmpx_2x.frag` (Morgan McGuire / Mara Gagiu, MIT)
- ScaleFX: ported from `app/src/main/assets/shaders/scalefx/scalefx-pass0.glsl` … `pass4.glsl` (Sp00kyFox, MIT)

Android golden fixtures live under `app/src/test/resources/spatial-golden`. A
device qualification run should compare Metal output against those goldens with
the documented one-LSB ScaleFX tolerance. This tree does not claim Windows
produced those pixels.

## Source SHA-256

Hashes use UTF-8 source with LF line endings so Windows and macOS checkouts
verify the same shader text. The table includes the RGB565 and precision fixes
already present in the baseline; updating this manifest does not qualify a device.

| Shader | SHA-256 |
|--------|---------|
| Nearest.metal | b41a43c2d1f6c3c4f5e5110d4880227da1ef01df25d0a4d3fa9eadd53ffb4b6b |
| SharpBilinear.metal | 3a2723ebad1a4a2eb60faff1344140f4002a2c332db0e9411f1f3fb8c5b27dac |
| Crt.metal | 497b923723b87b21005acba0bce35361c6803cbe470443ca9fe9738bba979abb |
| Mmpx2x.metal | afb22e38fe34ca4545bb069a787fba2e5e1433b69465edbce9c0179de39ee9f1 |
| ScaleFxPass0.metal | 457f90c1fed76fa91f1057d657c98b06d22ddf6d0cbc6ad68b729738dea6c6c0 |
| ScaleFxPass1.metal | 8f092a60f8db003afa5daac3cec9b70b4f838a77671d128432234dd01e45d70c |
| ScaleFxPass2.metal | 904e79202f399ef2c7002a8ac44cb4240ddd643dfd321c3d8f37f0826d07ebb5 |
| ScaleFxPass3.metal | d748a28e5af300b76f656fab7ec5426a42b5e71bd01f1ef10b745f5286d45cc4 |
| ScaleFxPass4.metal | 84e4a92537ef15c14b1e1e0110c8fcb995cc5a1766895fd971664a3f35379796 |

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
- actual drawable presentation timestamps and counts agree (targetTimestamp is only a prediction)

Never treat `UIScreen.maximumFramesPerSecond` as 120 Hz evidence. That
property is a capability advertisement, not a measured cadence.

RGB565 upload uses Simulator-compatible RGBA8. Advanced spatial shaders reconstruct original normalized RGB565 before their comparisons; ScaleFX intermediates use RGBA32Float. Presentation aspect modes are
4:3, square-pixel, and integer scale.

Actual presentation stats must come from completed drawable presentations, not display-link target predictions.
