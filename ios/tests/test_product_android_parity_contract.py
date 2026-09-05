#!/usr/bin/env python3
"""Host checks that the iOS product shell matches the Android/Harmony contract.

Runnable on Windows. Does not claim simulator or device parity.
"""

from pathlib import Path
import sys


ROOT = Path(__file__).resolve().parents[2]

SECTION_KEYS = (
    "section.display",
    "section.controls",
    "section.audio",
    "section.game_language",
    "section.about",
)

PAUSE_IDS = ("resume", "game_center", "settings")


def require(condition: bool, message: str) -> None:
    if not condition:
        raise AssertionError(message)


def read(relative: str) -> str:
    path = ROOT / relative
    require(path.is_file(), f"missing required product file: {relative}")
    return path.read_text(encoding="utf-8")


def main() -> int:
    overlay = read("ios/app/run/GamepadOverlayView.mm")
    require("GamepadHitMap" in overlay or "flynes::product" in overlay,
            "overlay must place controls via GamepadHitMap / flynes::product")
    require("ControlLayoutV2" in overlay or "from_layout" in overlay,
            "overlay must consume ControlLayoutV2")
    require("from_layout" in overlay, "overlay must build the hit map from ControlLayoutV2")
    require("Save" not in overlay and "Load" not in overlay,
            "overlay HUD must not include Save/Load")
    require("Control::Start" in overlay or "NES_START" in overlay,
            "overlay START must remain a NES START control")
    require("OPEN_PAUSE" not in overlay,
            "overlay START must not open the pause drawer")

    run = read("ios/app/run/RunSurfaceViewController.mm")
    require("OPEN_PAUSE" in run or "Pause" in run,
            "run surface must expose an independent pause entry")
    require(run.count("Game Center") + run.count("game_center") >= 1,
            "pause drawer must include Game Center")
    require("kPauseDrawerCommands" in run,
            "pause drawer commands must come from kPauseDrawerCommands")
    for pause_id in PAUSE_IDS:
        require(pause_id in run, f"pause drawer must include {pause_id}")
    require("play_save" not in run.lower() and "Save/Load" not in run,
            "run HUD must not include Save/Load")
    require("UIInterfaceOrientationMaskLandscape" in run,
            "run surface must stay landscape")

    runtime_bridge = (
        read("ios/app/bridge/FlyNesRuntimeBridge.h")
        + "\n"
        + read("ios/app/bridge/FlyNesRuntimeBridge.mm")
    )
    require("saveCheckpoint" in runtime_bridge,
            "runtime bridge must expose saveCheckpoint")
    require("fly_runtime_save_checkpoint" in runtime_bridge,
            "runtime bridge must call fly_runtime_save_checkpoint")
    open_pause_at = run.find("- (void)openPauseDrawer")
    require(open_pause_at >= 0, "run surface must implement openPauseDrawer")
    open_pause_body = run[open_pause_at:open_pause_at + 1400]
    require("saveCheckpoint" in open_pause_body,
            "openPauseDrawer must auto-checkpoint via saveCheckpoint")
    strings = read("ios/app/en.lproj/Localizable.strings")
    require("pause.checkpoint_failed" in strings,
            "pause checkpoint failure must be localized")
    require("pause_checkpoint_failed" in run or "pause.checkpoint_failed" in run,
            "openPauseDrawer must surface pause checkpoint failure")

    settings = read("ios/app/SettingsView.swift")
    for key in SECTION_KEYS:
        require(key in settings, f"settings missing {key}")
    positions = [settings.find(key) for key in SECTION_KEYS]
    require(all(pos >= 0 for pos in positions), "settings section keys must all be present")
    require(positions == sorted(positions),
            "settings sections must follow Android order: display, controls, audio, game_language, about")
    require("ControlLayout" in settings or "controlLayout" in settings,
            "controls section must open the layout editor")
    require("FlyNesAppBridge" in settings,
            "settings must bind through FlyNesAppBridge")

    editor = read("ios/app/ControlLayoutEditorView.swift")
    require("FlyNesAppBridge" in editor,
            "layout editor must write shared persist via FlyNesAppBridge")
    require("controlLayoutApply" in editor or "fly_control_layout_apply" in editor,
            "layout editor must apply ControlLayoutV2 through fly_control_layout_*")
    require("D_PAD" in editor and "SELECT" in editor and "START" in editor,
            "layout editor must expose ControlLayoutV2 elements")

    bridge = read("ios/app/bridge/FlyNesAppBridge.h") + "\n" + read("ios/app/bridge/FlyNesAppBridge.mm")
    require("fly_control_layout_get" in bridge and "fly_control_layout_apply" in bridge,
            "app bridge must call fly_control_layout_get/apply")
    require("game_center_state.hpp" in bridge or "GameCenterState" in bridge,
            "iOS must filter Game Center through flynes::product::GameCenterState, not a Swift fork")
    require("GameCenterItem" in bridge,
            "catalog rows must map into flynes::product::GameCenterItem")
    require("filtered(" in bridge,
            "iOS must call GameCenterState::filtered")
    require("items_for" in bridge or "filtered(" in bridge,
            "iOS must call GameCenterState::filtered / items_for")
    require("title_en" in bridge and "title_zh_hans" in bridge
            and "original_filename" in bridge,
            "Game Center search must use title_en / title_zh_hans / original_filename")

    cmake = read("ios/app/CMakeLists.txt")
    require("flynes_product" in cmake, "product CMake must link flynes_product")
    require("ControlLayoutEditorView.swift" in cmake,
            "product CMake must compile the layout editor")

    library = read("ios/app/CatalogLibraryView.swift")
    require("LibraryFilter" in library or "Category" in library,
            "Game Center must keep four category filters")
    for token in ("recent", "favorites", "all", "builtin"):
        require(token in library.lower(), f"Game Center must expose {token}")
    require("game_center" in library.lower() or "Game Center" in library,
            "library IA must align with Game Center labels")
    require("tabItem" not in library and "TabView" not in library,
            "Game Center must not invent extra tabs")
    require("gameCenterFilter" in library or "gameCenterFiltered" in library,
            "Game Center must call an ObjC++ helper that uses GameCenterState::filtered")
    require("localizedCaseInsensitiveContains" not in library,
            "Game Center must not search displayName/canonicalId in Swift")
    require("RECENT" in library and "FAVORITES" in library and "ALL" in library
            and "BUILTIN" in library,
            "Game Center categories must be the shared RECENT/FAVORITES/ALL/BUILTIN names")

    app = read("ios/app/FlyNESApp.swift")
    require("CatalogLibraryView" in app, "cold start must open Game Center")
    require("tabItem" not in app and ".tabItem" not in app,
            "product must not add a bottom Settings tab")
    require("SettingsView" in library or "SettingsView" in app,
            "Settings must remain reachable from Game Center")

    bookmark_h = read("ios/app/platform/FlyNesBookmarkStore.h")
    bookmark_mm = read("ios/app/platform/FlyNesBookmarkStore.mm")
    bookmark = bookmark_h + "\n" + bookmark_mm
    require("UUID" in bookmark or "uuid" in bookmark.lower(),
            "bookmark store must remain a UUID→bookmark map")
    require("FLYCAT01" not in bookmark,
            "bookmark store must not store locators in FLYCAT01")
    require("bookmark" in bookmark.lower(),
            "bookmark store must keep security-scoped bookmarks platform-only")

    run_swift = read("ios/app/RunGameView.swift")
    detail = read("ios/app/CatalogGameDetailView.swift")
    review_errors = []

    def review_require(condition: bool, message: str) -> None:
        if not condition:
            review_errors.append(message)

    review_require("navigationDestination" in library,
                   "Game Center must own navigationDestination so pause can pop Library → Detail → Run")
    compact_run_swift = run_swift.replace(" ", "")
    pops_to_root = (
        "path=NavigationPath()" in compact_run_swift
        or "removeLast(path.count)" in compact_run_swift
        or "popToRoot" in run_swift
    )
    review_require(pops_to_root,
                   "pause Game Center must pop to the library root, not dismiss() one NavigationLink")
    gc_at = run_swift.find("game_center")
    review_require(gc_at >= 0, "pause must handle game_center")
    if gc_at >= 0:
        review_require("dismiss()" not in run_swift[gc_at:gc_at + 180],
                       "pause Game Center must not dismiss() a single NavigationLink")
    review_require("LibraryRoute" in detail or "navigationDestination" in detail,
                   "Play must push run as a path value, not a nested NavigationLink destination")

    uses_named_space = "coordinateSpace" in editor and ".named" in editor
    uses_translation = "translation" in editor
    review_require(uses_named_space or uses_translation,
                   "layout drag must use named coordinateSpace on the stage or translation from the start normalized point")
    if "value.location" in editor and not uses_named_space:
        review_require(False,
                       "DragGesture.location without a named stage space is the control's local point")

    has_sibling_scrim = (
        "UIView *scrim" in run
        or "scrimView" in run
        or "pauseScrim" in run
        or "scrim_" in run
    )
    has_touch_filter = "shouldReceiveTouch" in run
    review_require(has_sibling_scrim or has_touch_filter,
                   "pause tap must use a sibling scrim beside the drawer, or shouldReceiveTouch only when touch.view is the scrim")
    review_require("[pauseLayer_ addGestureRecognizer" not in run,
                   "pause tap must not be installed on pauseLayer_; that layer includes the drawer")

    run_header = read("ios/app/run/RunSurfaceViewController.h")
    settings_at = run.find("PauseCommand::Settings")
    review_require(settings_at >= 0, "pause must handle Settings")
    settings_return = run.find("return;", settings_at) if settings_at >= 0 else -1
    settings_branch = run[settings_at:settings_return + len("return;")] if settings_return >= 0 else ""
    keeps_drawer = "dismissPauseLayerKeepingPaused" not in settings_branch
    reopens_drawer = (
        "onDismiss" in run_swift
        and ("openPauseDrawer" in run_swift or "reopenPause" in run_swift
             or "openPauseDrawer" in run_header)
    )
    review_require(keeps_drawer or reopens_drawer,
                   "pause Settings keeps drawer or reopens it; overlay reloads layout on settings close")
    review_require("dismissPauseLayerKeepingPaused:NO" not in settings_branch,
                   "pause Settings must leave the session paused")
    review_require("reloadProductSettings" in run_header,
                   "run surface must expose reloadProductSettings because the settings sheet does not call viewWillAppear")
    review_require("reloadProductSettings" in run_swift,
                   "settings sheet close must reload overlay layout")

    require(not review_errors, "; ".join(review_errors))

    print("flynes_ios_product_android_parity_contract: PASS")
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except AssertionError as error:
        print(f"flynes_ios_product_android_parity_contract: FAIL: {error}",
              file=sys.stderr)
        raise SystemExit(1)
