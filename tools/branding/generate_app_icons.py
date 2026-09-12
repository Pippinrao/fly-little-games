#!/usr/bin/env python3
"""Derive Android, iOS and Harmony icons from the approved checked-in master.

Install requirements.txt, then run this script, or --check for no writes.
Only resizing, adaptive padding and Android themed silhouette are derived.
--preview writes a layout-only mask montage; source artwork is never redrawn.
"""

import argparse
from collections import Counter
from io import BytesIO
import json
import math
from pathlib import Path

from PIL import Image, ImageChops, ImageDraw

ROOT = Path(__file__).resolve().parents[2]
MASTER = ROOT / "assets/branding/app-icon-master.png"
INFO = {"info": {"author": "xcode", "version": 1}}
ANDROID = "app/src/main/res"
HARMONY = "harmony/AppScope/resources/base/media"
IOS = "ios/app/Assets.xcassets"


def png(image):
    buffer = BytesIO()
    image.save(buffer, format="PNG", optimize=False, compress_level=9)
    return buffer.getvalue()


def json_bytes(value):
    return (json.dumps(value, indent=2) + "\n").encode("utf-8")


def background_color(master):
    # Match padding to the actual edge color; never replace source pixels.
    border = list(master.crop((0, 0, master.width, 10)).get_flattened_data())
    border += list(master.crop((0, 0, 10, master.height)).get_flattened_data())
    return Counter(border).most_common(1)[0][0]


def subject_mask(master, background):
    diff = ImageChops.difference(master, Image.new("RGB", master.size, background))
    r, g, b = diff.split()
    return ImageChops.lighter(ImageChops.lighter(r, g), b).point(lambda v: 255 if v > 48 else 0)


def adaptive_layers(master):
    background = background_color(master)
    mask = subject_mask(master, background)
    pixels = mask.load()
    cx, cy = master.width / 2, master.height / 2
    radius = max((math.hypot(x-cx, y-cy) / master.width
                  for y in range(master.height) for x in range(master.width)
                  if pixels[x, y]), default=0)
    if not radius:
        raise ValueError("Master has no distinguishable icon subject")
    # 432px at xxxhdpi = 108dp. Start with a 72dp master; then fit meaningful
    # artwork in the 66dp safe circle, allowing one pixel for antialiasing.
    side = min(288, int(131 / radius) // 2 * 2)
    offset = (432-side) // 2
    art = Image.new("RGBA", (432, 432))
    art.paste(master.resize((side, side), Image.Resampling.LANCZOS), (offset, offset))
    themed = Image.new("RGBA", (432, 432))
    alpha = Image.new("L", themed.size)
    alpha.paste(mask.resize((side, side), Image.Resampling.LANCZOS), (offset, offset))
    themed.putalpha(alpha)
    return art, themed, background


def bitmap_xml(source):
    return (f'<?xml version="1.0" encoding="utf-8"?>\n'
            f'<bitmap xmlns:android="http://schemas.android.com/apk/res/android"\n'
            f'    android:src="{source}" android:gravity="fill"\n'
            f'    android:filter="true" android:mipMap="true" />\n').encode()


def generated_files(master):
    if master.mode != "RGB" or master.width != master.height or master.width < 1024:
        raise ValueError("Approved master must be square, opaque RGB and at least 1024px")
    files, entries = {}, []
    slots = [("iphone", size, scale) for size in (20, 29, 40, 60) for scale in (2, 3)]
    slots.append(("ios-marketing", 1024, 1))
    for idiom, size, scale in slots:
        pixels = size * scale
        filename = f"AppIcon-{pixels}.png"
        entries.append({"idiom": idiom, "size": f"{size}x{size}",
                        "scale": f"{scale}x", "filename": filename})
        files[f"{IOS}/AppIcon.appiconset/{filename}"] = png(
            master.resize((pixels, pixels), Image.Resampling.LANCZOS))
    files[f"{IOS}/Contents.json"] = json_bytes(INFO)
    files[f"{IOS}/AppIcon.appiconset/Contents.json"] = json_bytes({"images": entries, **INFO})
    files[f"{HARMONY}/app_icon.png"] = png(master.resize((288, 288), Image.Resampling.LANCZOS))
    art, themed, background = adaptive_layers(master)
    files[f"{ANDROID}/drawable-xxxhdpi/ic_launcher_artwork.png"] = png(art)
    files[f"{ANDROID}/drawable-xxxhdpi/ic_launcher_themed.png"] = png(themed)
    files[f"{ANDROID}/drawable/ic_launcher_foreground.xml"] = bitmap_xml("@drawable/ic_launcher_artwork")
    files[f"{ANDROID}/drawable/ic_launcher_monochrome.xml"] = bitmap_xml("@drawable/ic_launcher_themed")
    hex_color = "#" + "".join(f"{channel:02X}" for channel in background)
    files[f"{ANDROID}/values/ic_launcher_colors.xml"] = (
        '<?xml version="1.0" encoding="utf-8"?>\n<resources>\n'
        f'    <color name="ic_launcher_background">{hex_color}</color>\n</resources>\n').encode()
    for density, pixels in [("mdpi", 48), ("hdpi", 72), ("xhdpi", 96), ("xxhdpi", 144), ("xxxhdpi", 192)]:
        files[f"{ANDROID}/mipmap-{density}/ic_launcher_legacy.png"] = png(
            master.resize((pixels, pixels), Image.Resampling.LANCZOS))
    for name in ("ic_launcher", "ic_launcher_round"):
        files[f"{ANDROID}/mipmap-anydpi/{name}.xml"] = bitmap_xml("@mipmap/ic_launcher_legacy")
        for version in (26, 33):
            mono = ('    <monochrome android:drawable="@drawable/ic_launcher_monochrome" />\n'
                    if version == 33 else "")
            files[f"{ANDROID}/mipmap-anydpi-v{version}/{name}.xml"] = (
                '<?xml version="1.0" encoding="utf-8"?>\n'
                '<adaptive-icon xmlns:android="http://schemas.android.com/apk/res/android">\n'
                '    <background android:drawable="@color/ic_launcher_background" />\n'
                '    <foreground android:drawable="@drawable/ic_launcher_foreground" />\n'
                f'{mono}</adaptive-icon>\n').encode()
    return files


def preview(master, path):
    art, themed, background = adaptive_layers(master)
    composite = Image.new("RGBA", art.size, background + (255,))
    composite.alpha_composite(art)
    android = composite.crop((72, 72, 360, 360)).convert("RGB")
    montage = Image.new("RGB", (1000, 650), "#E2E5E9")
    draw = ImageDraw.Draw(montage)
    examples = [("iOS / rounded-square", master, "round"),
                ("Android / round mask", android, "circle"),
                ("Android / rounded-square", android, "round"),
                ("Harmony / rounded-square", master, "round")]
    for index, (label, source, shape) in enumerate(examples):
        x = index * 250 + 17
        draw.text((x, 15), label, fill="#172333")
        for size, y in [(216, 45), (96, 310), (48, 460)]:
            icon = source.resize((size, size), Image.Resampling.LANCZOS)
            mask = Image.new("L", (size, size))
            md = ImageDraw.Draw(mask)
            if shape == "circle":
                md.ellipse((0, 0, size-1, size-1), fill=255)
            else:
                md.rounded_rectangle((0, 0, size-1, size-1), radius=size*.22, fill=255)
            montage.paste(icon, (x + (216-size)//2, y), mask)
    draw.text((18, 565), "Layout preview only: simulated system masks; no corner mask is baked into shipped artwork.", fill="#172333")
    draw.text((18, 590), "Android includes safe-circle padding. iOS/Harmony preserve the full approved master.", fill="#172333")
    path.parent.mkdir(parents=True, exist_ok=True)
    montage.save(path)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--check", action="store_true")
    parser.add_argument("--preview", type=Path)
    args = parser.parse_args()
    if not MASTER.is_file():
        parser.exit(1, "Approved shared master is missing: assets/branding/app-icon-master.png\n")
    with Image.open(MASTER) as master:
        files = generated_files(master)
        stale = []
        for relative, data in files.items():
            path = ROOT / relative
            if args.check:
                current = path.read_bytes() if path.is_file() else None
                # Git may use CRLF on Windows; PNG bytes remain strict.
                if current is not None and path.suffix in (".xml", ".json"):
                    current = current.replace(b"\r\n", b"\n")
                if current != data:
                    stale.append(relative)
            else:
                path.parent.mkdir(parents=True, exist_ok=True)
                path.write_bytes(data)
        if args.preview:
            preview(master, args.preview)
    if stale:
        parser.exit(1, "Missing or stale icon resources:\n" + "\n".join(stale) + "\n")
    print(f"{'Verified' if args.check else 'Generated'} {len(files)} icon resources from approved master.")


if __name__ == "__main__":
    main()
