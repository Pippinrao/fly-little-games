#!/usr/bin/env python3
"""File gates for Harmony Game Center as the default product entry."""

from pathlib import Path

text = Path("harmony/entry/src/main/ets/entryability/EntryAbility.ets").read_text(encoding="utf-8")
assert "pages/GameCenter" in text
assert "pages/Index" not in text or "GameCenter" in text
pages = Path("harmony/entry/src/main/resources/base/profile/main_pages.json").read_text(encoding="utf-8")
assert "pages/GameCenter" in pages
gc = Path("harmony/entry/src/main/ets/pages/GameCenter.ets").read_text(encoding="utf-8")
strings = Path("harmony/entry/src/main/resources/base/element/string.json").read_text(encoding="utf-8")
for key in ("game_center_recent", "game_center_favorites", "game_center_all",
            "game_center_builtin", "game_center_launch", "game_center_sources"):
    assert key in strings
    assert key in gc or f"$r('app.string.{key}')" in gc.replace('"', "'")
assert "gameCenterFilter" in gc
assert "play_save" not in gc
# @State arrays are ArkTS observed collections, not JS Array. Passing them
# into N-API napi_is_array crashes Game Center (jscrash TypeError).
assert "@State private allRows" not in gc
assert "gameCenterFilter(this.allRows" not in gc

run = Path("harmony/entry/src/main/ets/pages/RunGame.ets").read_text(encoding="utf-8")
overlay = Path("harmony/entry/src/main/ets/overlay/GamepadOverlay.ets").read_text(encoding="utf-8")
assert "GamepadOverlay" in run
assert "play_save" not in run.lower()
assert "Save" not in overlay
assert "pauseCommands" in run or "PauseCommand" in run
assert "hitMapFromLayout" in overlay or "HitMap" in overlay
# Canvas buffer is vp-sized while hit-map/draw use px; scale px onto the buffer.
assert "setTransform" in overlay
assert "canvasContext.width" in overlay
# Returning from Settings while paused does not tick frames. Canvas only
# redraws onReady/onAreaChange/onTouch, so layoutUtf8 @Prop must @Watch a
# redraw callback. .key() is test-only on this SDK; remount via overlayMounted.
assert "@Watch('onLayoutUtf8Changed')" in overlay
assert "onLayoutUtf8Changed" in overlay
assert "overlayMounted" in run
assert ".key(" not in run

settings = Path("harmony/entry/src/main/ets/pages/Settings.ets").read_text(encoding="utf-8")
for key in ("section.display", "section.controls", "section.audio", "section.game_language", "section.about"):
    assert key in settings
assert settings.find("section.display") < settings.find("section.about")
layout_editor = Path("harmony/entry/src/main/ets/pages/ControlLayout.ets").read_text(encoding="utf-8")
# Stage Column used to swallow Stack onTouch; controls must be directly pannable.
assert "PanGesture" in layout_editor
assert "beginDrag" in layout_editor
assert "onClick" in layout_editor
assert "toFixed(4)" in layout_editor
src = Path("harmony/entry/src/main/ets/platform/HarmonySourceMap.ets").read_text(encoding="utf-8")
assert "FLYCAT" not in src
assert "uuid" in src.lower() or "UUID" in src
# Dashed RFC-4122 from generateRandomUUID must normalize to 32 hex digits.
assert "32" in src
uuid_norm = (
    "replace('-'" in src or 'replace("-")' in src or
    "replaceAll('-'" in src or 'replaceAll("-")' in src or
    "normalizeUuidHex" in src or
    "/[^0-9a-f]/g" in src or "/[^0-9a-f]/gi" in src
)
assert uuid_norm, "HarmonySourceMap must strip hyphens from generated UUIDs"

catalog = Path("harmony/entry/src/main/ets/service/CatalogProductService.ets").read_text(encoding="utf-8")
dts = Path("harmony/entry/src/main/cpp/types/libentry/Index.d.ts").read_text(encoding="utf-8")
assert "catalogSnapshot" in catalog or "catalogRows" in catalog
snapshot_export = "catalogSnapshot" if "catalogSnapshot" in catalog else "catalogRows"
assert snapshot_export in dts
assert "return [builtin]" not in catalog
assert "From Below" in catalog

print("flynes_harmony_product_contract: PASS")
