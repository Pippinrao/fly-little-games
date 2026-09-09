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
play = Path("harmony/entry/src/main/ets/service/PlayService.ets").read_text(encoding="utf-8")
overlay = Path("harmony/entry/src/main/ets/overlay/GamepadOverlay.ets").read_text(encoding="utf-8")
assert "GamepadOverlay" in run
assert "play_save" not in run.lower()
assert "Save" not in overlay
assert "pauseCommands" in run or "PauseCommand" in run
# Spec §5 launch-by-canonical-id; §6 ROM-open failure stays in Game Center.
# Builtin From Below loads the bundled rawfile. Scanned titles open via source
# map + borrowed file bytes (ZIP decoded to NES payload), not From Below.
assert "game_center_rom_open_failed" in strings
assert "game_center_rom_open_failed" in gc
launch_at = gc.find("private launchSelected")
assert launch_at >= 0, "Game Center must implement launchSelected"
launch_end = gc.find("private async scanManagedRoms", launch_at)
if launch_end < 0:
    launch_end = gc.find("private selectedRow", launch_at)
launch_body = gc[launch_at:launch_end if launch_end > launch_at else launch_at + 1200]
assert "pushUrl" in launch_body
assert "canOpenSelected" in launch_body
assert "mapsToBuiltinFromBelow" not in launch_body, (
    "Launch must push RunGame for scanned catalog rows, not only builtin From Below")
assert "from_below.nes" in play
open_at = play.find("async open")
open_end = play.find("setButtons", open_at)
play_open = play[open_at:open_end if open_end > open_at else open_at + 2200]
rom_at = play_open.find("from_below.nes")
assert rom_at >= 0, "PlayService must still load bundled From Below for builtin"
before_rom = play_open[:rom_at]
assert "canonicalId" in play_open and (
    "canonicalId" in before_rom or "builtin" in before_rom or "from-below" in before_rom
), "PlayService must not open from_below.nes unless canonicalId maps to builtin"
assert "playDecodePackage" in play_open or "playDecodePackage" in play
assert "sourceRelativePath" in play or "sourceUuidHex" in play
assert "filesDir" in gc or "roms" in gc
assert "MANAGED_LIBRARY" in gc or "sourceScope" in gc or "filesDir" in gc
assert "fontSize(22)" in gc
assert ".height(64)" in gc
assert ".height(48)" in gc
start_at = run.find("private async start")
start_end = run.find("private startLoop", start_at)
start_body = run[start_at:start_end if start_end > start_at else start_at + 700]
assert "this.locator" in start_body or "canonicalId" in start_body, (
    "RunGame.start must pass or branch on canonicalId")
assert "play.open(context,this.locator)" in start_body.replace(" ", "").replace("\n", ""), (
    "RunGame must pass the selected launch locator into PlayService.open")
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
assert "Scroll()" in layout_editor, "layout parameters must scroll at small available heights"
for control_id in ("control-layout-actions", "control-layout-save", "control-layout-discard"):
    assert f".id('{control_id}')" in layout_editor, f"missing stable layout editor id: {control_id}"
scroll_at = layout_editor.find("Scroll()")
actions_at = layout_editor.find(".id('control-layout-actions')")
assert 0 <= scroll_at < actions_at, "save and discard footer must remain outside the scrolling region"
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
assert "sourceUuidHex" in dts
assert "sourceRelativePath" in dts
assert "packageFormat" in dts
assert "playDecodePackage" in dts

scan = Path("harmony/entry/src/main/ets/platform/HarmonyRomScan.ets").read_text(encoding="utf-8")
napi = Path("harmony/entry/src/main/cpp/napi_init.cpp").read_text(encoding="utf-8")
for export in ("scanJobStart", "scanJobStatus", "scanJobCancel"):
    assert export in dts, f"{export} must be declared for ArkTS"
    assert f'"{export}"' in napi, f"{export} must be registered by N-API"
    assert export in scan, f"HarmonyRomScan must use {export}"
assert "fileIo.listFile(" in scan, "directory enumeration must use the asynchronous API"
assert "fileIo.open(" in scan, "file opening must use the asynchronous API"
assert "fileIo.stat(" in scan, "file stat must use the asynchronous API"
assert "nativeApp.scanAddFile" not in scan, "ArkUI must not synchronously hash ROM files"
appear_at = gc.find("aboutToAppear")
appear_end = gc.find("private ensureCoverStore", appear_at)
startup_body = gc[appear_at:appear_end]
assert "scanManagedRoms" not in startup_body, "normal page entry must only load the persisted index"
assert "/data/local/tmp/fcgames" not in gc, "debug sideload must not run from the product home page"
assert "fetchHttpSideload" not in gc, "HTTP sideload must be an explicit debug action"
assert ".objectFit(ImageFit.Fill)" in run, "the computed viewport must not be letterboxed a second time"
assert "computeViewport" in run, "RunGame must use the tested physical viewport calculation"
sources_page = Path("harmony/entry/src/main/ets/pages/Sources.ets").read_text(encoding="utf-8")
for token in ("catalogSnapshot", "gameCount", "sources_rescan", "sources_cancel_scan",
              "cancelActiveScan", "processedFiles"):
    assert token in sources_page, f"source management is missing {token}"
assert "libraryStatusText" in gc, "Game Center source hint must derive from the catalog"
for field, commit in (
    ("customRefreshPolicy", "commitCustomRefresh"),
    ("customTemporalMode", "commitCustomTemporal"),
    ("customSpatialMode", "commitCustomSpatial"),
    ("customPostEffect", "commitCustomPost"),
):
    assert settings.count(field) >= 3, f"custom display axis {field} must be rendered and persisted"
    assert commit in settings, f"custom display axis {field} needs a dedicated commit path"
for key in (
    "video_custom_refresh", "video_custom_temporal", "video_custom_spatial",
    "video_custom_post", "video_refresh_follow", "video_refresh_60", "video_refresh_90",
    "video_refresh_120", "video_temporal_native", "video_temporal_motion",
    "video_spatial_nearest", "video_spatial_sharp", "video_spatial_mmpx",
    "video_spatial_scalefx", "video_post_none", "video_post_crt",
):
    assert key in strings, f"missing localized custom display label: {key}"
    assert f"app.string.{key}" in settings, f"custom display UI must render {key}"

print("flynes_harmony_product_contract: PASS")
