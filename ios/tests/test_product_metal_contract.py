#!/usr/bin/env python3
"""Host checks for iOS Metal quality shaders and fallback policy."""

from pathlib import Path
import hashlib
import re
import sys


ROOT = Path(__file__).resolve().parents[2]

SHADERS = {
    "nearest": "ios/app/metal/shaders/Nearest.metal",
    "sharp": "ios/app/metal/shaders/SharpBilinear.metal",
    "crt": "ios/app/metal/shaders/Crt.metal",
    "mmpx": "ios/app/metal/shaders/Mmpx2x.metal",
    "scalefx0": "ios/app/metal/shaders/ScaleFxPass0.metal",
    "scalefx1": "ios/app/metal/shaders/ScaleFxPass1.metal",
    "scalefx2": "ios/app/metal/shaders/ScaleFxPass2.metal",
    "scalefx3": "ios/app/metal/shaders/ScaleFxPass3.metal",
    "scalefx4": "ios/app/metal/shaders/ScaleFxPass4.metal",
}

REQUIRED = tuple(SHADERS.values()) + (
    "ios/app/metal/FALLBACK.md",
    "ios/app/metal/FlyNesMetalRenderer.h",
    "ios/app/metal/FlyNesMetalRenderer.mm",
    "ios/app/metal/FlyNesDisplayLinkPacer.h",
    "ios/app/metal/FlyNesDisplayLinkPacer.mm",
    "ios/app/diagnostics/DiagnosticExport.swift",
)

STAGE1 = (
    "ios/CMakeLists.txt",
    "ios/src/main.mm",
    "ios/src/portability_smoke.cpp",
    "ios/src/portability_smoke.hpp",
)

STAGE1_FORBIDDEN = re.compile(
    r"MetalKit|AVAudio|CoreBluetooth|flynes_runtime|fly_runtime_|ios/app/metal",
    flags=re.IGNORECASE,
)


def require(condition: bool, message: str) -> None:
    if not condition:
        raise AssertionError(message)


def read(relative: str) -> str:
    path = ROOT / relative
    require(path.is_file(), f"missing required Metal product file: {relative}")
    return path.read_text(encoding="utf-8")


def sha256_file(relative: str) -> str:
    return hashlib.sha256((ROOT / relative).read_bytes()).hexdigest()


def main() -> int:
    texts = {relative: read(relative) for relative in REQUIRED}

    nearest = texts[SHADERS["nearest"]]
    require("texture2d" in nearest.lower() or "texture2d<" in nearest.lower(),
            "Nearest shader must sample a 2D texture")
    require("nearest" in nearest.lower() or "mag_filter::nearest" in nearest.lower()
            or "Nearest" in nearest,
            "Nearest shader must document nearest filtering")

    sharp = texts[SHADERS["sharp"]]
    require("fract" in sharp.lower() or "fract(" in sharp,
            "Sharp Bilinear must keep the texel-aware fraction remap")
    require("uTextureSize" in sharp or "textureSize" in sharp or "texture_size" in sharp,
            "Sharp Bilinear must take a texture size uniform")

    crt = texts[SHADERS["crt"]]
    require("scanline" in crt.lower(), "CRT shader must apply scanlines")
    require("vignette" in crt.lower() or "mask" in crt.lower(),
            "CRT shader must include mask or vignette")

    mmpx = texts[SHADERS["mmpx"]]
    require("luma(" in mmpx or "luma " in mmpx, "MMPX port must keep luma comparisons")
    require("allEq4" in mmpx or "all_eq4" in mmpx or "allEq3" in mmpx,
            "MMPX port must keep neighborhood equality helpers")
    require("Morgan McGuire" in mmpx or "MIT" in mmpx,
            "MMPX Metal port must retain upstream attribution")

    scalefx0 = texts[SHADERS["scalefx0"]]
    require("Sp00kyFox" in scalefx0 and "MIT" in scalefx0,
            "ScaleFX Metal ports must retain the upstream MIT notice")
    require("dist(" in scalefx0, "ScaleFX pass 0 must compute the color-distance metric")
    for name in ("scalefx1", "scalefx2", "scalefx3", "scalefx4"):
        body = texts[SHADERS[name]]
        require("Sp00kyFox" in body, f"{name} must retain ScaleFX authorship")
        require("kernel" in body or "fragment" in body,
                f"{name} must be a Metal shader entry")

    notes = texts["ios/app/metal/FALLBACK.md"]
    require("atomic" in notes.lower() and "half-switched" in notes.lower(),
            "fallback notes must forbid a half-switched pipeline")
    require("Sharp" in notes and "Nearest" in notes,
            "fallback notes must name Sharp then Nearest")
    require("maximumFramesPerSecond" in notes,
            "notes must reject maximumFramesPerSecond as 120 Hz evidence")
    require("display-link" in notes.lower() or "CADisplayLink" in notes,
            "notes must require display-link timestamps for motion")
    require("present" in notes.lower() and "stats" in notes.lower(),
            "notes must require present stats for 60-to-120 motion")
    for label, relative in SHADERS.items():
        digest = sha256_file(relative)
        require(digest in notes, f"FALLBACK.md must pin SHA-256 for {label}")

    renderer = texts["ios/app/metal/FlyNesMetalRenderer.mm"]
    require("RGB565" in renderer or "rgb565" in renderer.lower() or "B5G6R5" in renderer,
            "renderer must upload RGB565 frames")
    require("FLY_ASPECT_FOUR_BY_THREE" in renderer or "4:3" in renderer
            or "four_by_three" in renderer.lower() or "FourByThree" in renderer,
            "renderer must support 4:3")
    require("square" in renderer.lower() or "FLY_ASPECT_SQUARE" in renderer,
            "renderer must support square pixels")
    require("integer" in renderer.lower() or "FLY_ASPECT_INTEGER" in renderer,
            "renderer must support integer scale")
    require("fallback" in renderer.lower() and ("Sharp" in renderer or "Nearest" in renderer),
            "renderer must atomically fall back to Sharp or Nearest")
    require("half-switched" in renderer.lower() or "activePipeline" in renderer
            or "commitPipeline" in renderer,
            "renderer must switch pipelines atomically")

    pacer = texts["ios/app/metal/FlyNesDisplayLinkPacer.mm"]
    require("CADisplayLink" in pacer, "motion pacer must use CADisplayLink")
    require("timestamp" in pacer.lower(), "pacer must record display-link timestamps")
    require("maximumFramesPerSecond" not in pacer.split("120")[0] or
            "not" in pacer.lower() or "never" in pacer.lower() or "reject" in pacer.lower(),
            "pacer must not treat maximumFramesPerSecond as cadence evidence")
    require("never" in pacer.lower() and "maximumFramesPerSecond" in pacer,
            "pacer must explicitly refuse maximumFramesPerSecond as 120 Hz evidence")
    require("presented" in pacer.lower() or "present stats" in pacer.lower()
            or "presentedTime" in pacer,
            "pacer must consult present stats")

    diagnostics = texts["ios/app/diagnostics/DiagnosticExport.swift"]
    for token in ("gpuFamily", "displayCadence", "gpuTime", "audioUnderrun",
                  "frameGap", "thermal", "algorithmHash", "buildHash"):
        require(token.lower() in diagnostics.lower() or token in diagnostics,
                f"diagnostic export missing {token}")
    require("locked" in diagnostics.lower() or "qualification" in diagnostics.lower(),
            "advanced modes must stay locked until a matching device profile exists")

    android_mmpx = ROOT / "app/src/main/assets/shaders/mmpx/mmpx_2x.frag"
    require(android_mmpx.is_file(), "Android MMPX source must remain the algorithm origin")
    golden = ROOT / "app/src/test/resources/spatial-golden"
    if golden.exists():
        require("spatial-golden" in notes, "fallback notes must cite Android golden fixtures")

    cmake = read("ios/app/CMakeLists.txt")
    for relative in SHADERS.values():
        require(Path(relative).name in cmake, f"product CMake must list {relative}")
    require("FlyNesMetalRenderer.mm" in cmake, "product CMake must compile the Metal renderer")

    for relative in STAGE1:
        require(not STAGE1_FORBIDDEN.search(read(relative)),
                f"Stage-1 file {relative} must stay free of product Metal/runtime")

    print("flynes_ios_product_metal_contract: PASS")
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except AssertionError as error:
        print(f"flynes_ios_product_metal_contract: FAIL: {error}", file=sys.stderr)
        raise SystemExit(1)
