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
    compact_bridge = runtime_bridge.replace(" ", "").replace("\n", "")
    require("NSData*)saveCheckpoint" in compact_bridge,
            "saveCheckpoint must return NSData, not discard the checkpoint blob")
    save_at = runtime_bridge.rfind("- (NSData *)saveCheckpoint")
    if save_at < 0:
        save_at = runtime_bridge.find("saveCheckpoint")
    require(save_at >= 0, "runtime bridge must define saveCheckpoint")
    save_body = runtime_bridge[save_at:save_at + 2200]
    require("dataWithBytes" in save_body or "dataWithBytesNoCopy" in save_body,
            "saveCheckpoint must keep the blob as NSData")
    require("loadCheckpoint" in runtime_bridge,
            "runtime bridge must expose loadCheckpoint")
    require("fly_runtime_load_checkpoint" in runtime_bridge,
            "loadCheckpoint must wrap fly_runtime_load_checkpoint")
    load_cp_at = runtime_bridge.rfind("- (BOOL)loadCheckpoint")
    require(load_cp_at >= 0, "runtime bridge must define loadCheckpoint")
    copy_latest_at = runtime_bridge.find("- (NSData *)copyLatestRgb565Frame", load_cp_at)
    load_cp_body = runtime_bridge[load_cp_at:copy_latest_at if copy_latest_at > load_cp_at else load_cp_at + 2200]
    require("fly_runtime_copy_latest_frame" in load_cp_body or "copyLatestRgb565Frame" in load_cp_body
            or "copyLatest" in load_cp_body,
            "loadCheckpoint must copy latest frame metadata after fly_runtime_load_checkpoint")
    require("frame_index_" in load_cp_body,
            "loadCheckpoint must resync frame_index_ after restore")
    compact_load_cp = load_cp_body.replace(" ", "").replace("\n", "")
    require("frame_index+1" in compact_load_cp,
            "loadCheckpoint must set the bridge next frame index to last+1")
    persist_src = runtime_bridge + "\n" + run
    require("autosave.nst" in persist_src,
            "pause autosave must persist as per-ROM autosave.nst")
    require("saves/" in persist_src or "saves" in persist_src,
            "autosave.nst must live in a per-ROM saves directory")
    require("canonicalId" in persist_src,
            "per-ROM autosave directory must use Run's canonicalId")
    require("NSDataWritingAtomic" in persist_src or "atomically:YES" in persist_src,
            "autosave.nst must be written atomically")
    open_pause_at = run.find("- (void)openPauseDrawer")
    require(open_pause_at >= 0, "run surface must implement openPauseDrawer")
    open_pause_body = run[open_pause_at:open_pause_at + 1800]
    require("saveAutosaveIfEnabled" in open_pause_body,
            "openPauseDrawer must use the autosave setting before checkpointing")
    save_at = run.find("- (void)saveAutosaveIfEnabled")
    require(save_at >= 0, "run surface must implement the autosave helper")
    save_end = run.find("\n- (", save_at + 1)
    save_body = run[save_at:save_end if save_end > save_at else len(run)]
    require("autosave_enabled" in save_body and "return" in save_body,
            "autosave helper must skip disabled autosave")
    require("saveCheckpoint" in save_body and "persistAutosave" in save_body,
            "enabled autosave must persist the checkpoint blob")
    view_load_at = run.find("- (void)viewDidLoad")
    require(view_load_at >= 0, "run surface must implement viewDidLoad")
    view_will_at = run.find("- (void)viewWillAppear")
    view_load_body = run[view_load_at:view_will_at if view_will_at > view_load_at else view_load_at + 1600]
    require("loadCheckpoint" in view_load_body or "restoreAutosave" in view_load_body,
            "next run must loadCheckpoint from autosave.nst before stepping")
    load_rom_at = view_load_body.find("loadRom")
    restore_at = view_load_body.find("restoreAutosave")
    checkpoint_at = view_load_body.find("loadCheckpoint")
    require(load_rom_at >= 0,
            "run start must call loadRom before restoreAutosave / loadCheckpoint")
    first_restore = restore_at if restore_at >= 0 else checkpoint_at
    if restore_at >= 0 and checkpoint_at >= 0:
        first_restore = min(restore_at, checkpoint_at)
    require(first_restore >= 0, "run start must restore autosave after loadRom")
    require(load_rom_at < first_restore,
            "loadRom must precede restoreAutosave / loadCheckpoint")
    load_rom_end = view_load_body.find(";", load_rom_at)
    load_rom_call = view_load_body[load_rom_at:load_rom_end + 1 if load_rom_end >= 0 else load_rom_at + 80]
    require("error:nil" not in load_rom_call.replace(" ", ""),
            "loadRom must not ignore NSError with error:nil")
    between_load_and_restore = view_load_body[load_rom_at:first_restore]
    require("if" in between_load_and_restore,
            "restoreAutosave must be gated on successful loadRom")
    restore_fn_at = run.find("- (void)restoreAutosave")
    require(restore_fn_at >= 0, "run surface must implement restoreAutosave")
    apply_at = run.find("- (void)applyOverlayButtons")
    restore_fn = run[restore_fn_at:apply_at if apply_at > restore_fn_at else restore_fn_at + 900]
    require("romReady_" in restore_fn or "romLoaded_" in restore_fn,
            "restoreAutosave must refuse unless loadRom succeeded")
    require("rom_open_failed" in run,
            "ROM open failure must surface a visible localized message")
    require("library.rom_open_failed" in run or "run.rom_open_failed" in run,
            "ROM open failure must use a localized string, not a debug HUD")
    require("debugHud" not in run and "debug_hud" not in run.lower()
            and "Debug HUD" not in run,
            "ROM open failure must not dump into a debug HUD")
    require("bundledRomForCanonicalId" in run,
            "run start must resolve a bundled ROM from the shared manifest")
    require("BuiltinGames" in run,
            "run start must consult the bundled-game manifest")
    require("byCanonicalId" in run,
            "run start must resolve the bundled game by canonical id")
    first_bundled = view_load_body.find("bundledRomForCanonicalId")
    require(first_bundled >= 0, "the builtin path must resolve the bundled ROM once")
    second_bundled = view_load_body.find("bundledRomForCanonicalId", first_bundled + 1)
    require(second_bundled < 0,
            "a non-builtin id must not fall back to a bundled ROM")
    require("canonicalIdMapsToBuiltinFromBelow" not in run,
            "the retired single-game canonical-id mapping must be gone")
    cmake = read("ios/app/CMakeLists.txt")
    require("content/assets" in cmake and "builtin-games.json" in cmake,
            "product CMake must package the shared bundled content")
    require("FLYNES_IOS_BUNDLED_ROMS" in cmake,
            "product CMake must package every bundled ROM")
    require("MACOSX_PACKAGE_LOCATION" in cmake and "Resources" in cmake,
            "bundled content must be packaged as bundle resources")
    require((ROOT / "content/assets/roms/thwaite.nes").is_file(),
            "Harmony and iOS hosts must share the manifest's ROM fixture")
    strings = read("ios/app/en.lproj/Localizable.strings")
    require("pause.checkpoint_failed" in strings,
            "pause checkpoint failure must be localized")
    require("pause_checkpoint_failed" in run or "pause.checkpoint_failed" in run,
            "openPauseDrawer must surface pause checkpoint failure")
    require("library.rom_open_failed" in strings or "run.rom_open_failed" in strings,
            "ROM open failure must be localized (spec §6)")
    require("play_save" not in run.lower() and "Save/Load" not in run,
            "run HUD must not include Save/Load")

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

    editor = (read("ios/app/ControlLayoutEditorView.swift")
              + "\n" + read("ios/app/platform/ControlLayoutDraft.swift"))
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
    # Android GameCenterItem carries title_en / title_zh_hans / original_filename and
    # GameCenterState::filtered matches all three. The iOS bridge must populate the
    # corresponding row keys from the presentation helper rather than passing a raw
    # filename as a title.
    item_body = bridge[bridge.find("game_center_item_from_row"):]
    require("title_en" in item_body or "titleEn" in item_body,
            "GameCenterItem must receive title_en")
    require("title_zh_hans" in item_body or "titleZhHans" in item_body,
            "GameCenterItem must receive title_zh_hans")
    require("searchAliases" in item_body or "original_filename" in item_body
            or "originalFilename" in item_body,
            "GameCenterItem must receive filename aliases")
    require("CatalogPresentation.h" in bridge,
            "catalog rows must derive titles from FlyNesCatalogPresentation")
    require("fieldsForFilename" in bridge and "trustedBuiltin" in bridge,
            "catalog titles must come from the trusted-builtin aware helper")
    require("titleZhHans" in bridge and "titleEn" in bridge and "searchAliases" in bridge,
            "catalog rows must publish titleEn / titleZhHans / searchAliases")
    gc_at = bridge.rfind("gameCenterFilteredGamesForCategory")
    require(gc_at >= 0, "app bridge must implement gameCenterFilteredGamesForCategory")
    scan_at = bridge.find("scanBorrowedFd", gc_at)
    gc_body = bridge[gc_at:scan_at if scan_at > gc_at else gc_at + 3500]
    source_service = read("ios/app/platform/CatalogSourceService.mm")
    source_model = read("ios/app/CatalogSourceManagementView.swift")
    require("prepareBuiltin" in source_model and "prepareBuiltin" in source_service,
            "Game Center must prepare the real bundled catalog source")
    require("scanFileRecords:" in source_service,
            "builtin must use the same validated scanner as external sources")
    require("BuiltinGames" in source_service,
            "the builtin source must be prepared from the shared manifest")
    require("library.source.builtin_missing" in source_service,
            "missing builtin must report failure, not inject a fake playable row")
    require("Thwaite" not in gc_body and "thwaite.nes" not in gc_body,
            "filtered snapshots must not fabricate builtin playable entries")
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
    require("library.source.game_unsupported" in detail and "library.source.game_stale" in detail,
            "selected detail must explain compatibility and freshness launch refusal")
    require("compatibilityState != 1" in detail and "freshness != 1" in detail and ".disabled" in detail,
            "Play must require a fresh compatible variant for builtins and imports")
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
    # Android resolves and commits the selection before leaving the Game Center, so a
    # launch failure stays in the library. Play therefore hands the resolved ROM to the
    # library, which owns the path value, instead of a self-contained NavigationLink.
    review_require("LibraryRoute" in library and "navigationDestination" in library,
                   "Play must push run as a path value owned by the Game Center")
    review_require("onLaunch" in detail,
                   "selected detail must hand Play to the Game Center launch resolver")
    review_require("romData" in library and "romData" in run_swift,
                   "the resolved ROM must travel with the run route")
    review_require("library.launch_failed" in source_model or "launch_failed" in source_model,
                   "a failed launch must report in the Game Center status line")
    review_require("NavigationLink" not in detail,
                   "Play must not navigate before the game is known to be openable")

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
