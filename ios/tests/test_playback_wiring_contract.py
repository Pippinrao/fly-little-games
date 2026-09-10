"""Supplemental source wiring guard; does not claim Apple runtime validation."""
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
bridge = (ROOT / "ios/app/bridge/FlyNesRuntimeBridge.mm").read_text(encoding="utf-8")
surface = (ROOT / "ios/app/run/RunSurfaceViewController.mm").read_text(encoding="utf-8")
for token in ("fly_runtime_pull_pcm", "fly_runtime_clear_input_ports", "recursive_mutex", "meta.timeline_epoch"):
    assert token in bridge, f"runtime bridge missing {token}"
for token in ("FlyNesDisplayLinkPacer", "FlyNesMetalRenderer", "FlyNesAudioPlayer", "releaseAllButtons",
              "AVAudioSessionInterruptionNotification", "UIApplicationWillResignActiveNotification", "didProduceSamples"):
    assert token in surface, f"run surface missing {token}"
callback = surface.split("- (void)applyOverlayButtons:", 1)[1].split("- (void)openPauseDrawer", 1)[0]
assert "stepFrame" not in callback, "input callback must not advance the runtime"
print("iOS playback wiring contract: PASS (source only)")
