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

| Shader | SHA-256 |
|--------|---------|
| Nearest.metal | ac7aa8308399372d27782173c813ddb89ff4bd3fb49bb3080a79409ff6cc0d2f |
| SharpBilinear.metal | d6c687a794903469d17a00af46888c675e4797617aeb2205510d8bdf3d89eb09 |
| Crt.metal | 67b0248a41d28c747370b92cf77182a9596f69d673839d263ae8326166cdcca2 |
| Mmpx2x.metal | ee77494062b50cd566af685873adaf7f21293bc395c87449379a186f21c5c85f |
| ScaleFxPass0.metal | 5d363481baacabd68b8ffac21ccb8e181b1032bd48eedca4ed0fdced908a5377 |
| ScaleFxPass1.metal | f30b265395774aef88beb31b242e105e59aeb9bf6752059bc996a411694313bc |
| ScaleFxPass2.metal | 3632ba59dfab768ce68aa51fff8a241ecef19af8cf7827699470823628e00677 |
| ScaleFxPass3.metal | e6de44655121ce81b6332c5466b0a37c3cd987be3f23bedec0412adad59ba818 |
| ScaleFxPass4.metal | 9eb7b74ec390cbe16301dc5999656de946d3554b6e9db892d9ebe95af01bd2a3 |

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
