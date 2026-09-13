# Shared application icon

`app-icon-master.png` is the approved mint/teal pixel NES cartridge with a coral
star on cream. Its dark cartridge outline is part of the illustration. There is
no exterior icon frame or baked-in platform corner mask.

The original imagegen output was
`exec-60b17a79-0a17-4f07-b929-4a6fa5413505.png`, selected on 2026-09-13.
It was copied without pixel changes from the imagegen output directory for task
`01a090ef-ee22-7ad0-9ab3-dc44bfe9d155`.

Master: 1254 × 1254, RGB PNG.
SHA-256: `755f5950a9c6f41aa1ca746d01f91112af30de1d0ed1da4233d421ab98f1a48b`.
Measured artwork bounds are approximately 76.4% wide and 77.3% tall. The cream
has slight source texture; its most common top/left edge color is `#FEF7E7`.
The generator preserves that texture and uses the sampled color for Android's
adaptive padding/background. It does not recolor or redraw the master.

## Regeneration

```sh
python -m pip install -r tools/branding/requirements.txt
python tools/branding/generate_app_icons.py
python tools/branding/generate_app_icons.py --check
python tools/branding/test_app_icons.py
python ios/tests/test_app_icon.py
python tools/branding/generate_app_icons.py --check --preview .artifacts/icon-layout/montage.png
```

The old `tools/ios/generate_app_icon.py` entry point delegates to this shared
generator. It cannot restore the retired Android controller artwork.

## Platform mapping

- iOS: the complete master is scaled to the existing nine iPhone/marketing
  AppIcon slots (eight distinct pixel dimensions). Every PNG is opaque RGB.
- Android: both launcher entry names retain legacy, API 26 adaptive, and API 33
  themed definitions. Five density variants supply the complete legacy image.
  Adaptive artwork uses a 432px (108dp at xxxhdpi) layer. A 288px (72dp) master
  placement is the starting point, then the artwork is fitted inside the 66dp
  safe circle. This master produces a 254px placement with 89px margins, so the
  cartridge is slightly smaller than iOS and stays intact under circular masks.
  The themed silhouette is derived from the same master, never the old vector.
- Harmony: `AppScope/resources/base/media/app_icon.png` is a flat 288px version
  of the complete master. Existing `$media:app_icon` application, ability and
  start-window references are unchanged. The old same-named SVG is removed to
  avoid duplicate resources. No layered-image schema is introduced.

Platform masks in the montage are layout simulations, not device evidence.
Actual launcher appearance still depends on the platform and theme. Android's
safe-circle fit follows the [Android adaptive icon guidance](https://developer.android.com/develop/ui/compose/system/icon_design_adaptive).
