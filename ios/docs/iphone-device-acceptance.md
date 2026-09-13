# iPhone 16 Pro Max device acceptance

Everything below is executed on the Mac that can reach the phone. The simulator
suite is a regression gate, not a substitute for this run.

## 0. Before you start

- Phone: iPhone 16 Pro Max, **Developer Mode on** (Settings → Privacy & Security).
- Cable, and trust the computer when prompted.
- Note the phone's iOS version (`Settings → General → About`). The product targets
  iOS 16.4 minimum, so any iOS 16.4+ phone installs. Verify the version before
  choosing an install path — if the phone is newer than the Mac's Xcode can
  handle, use the Sideloadly path (item 2) instead of Xcode.
- Do **not** commit any team id, profile, `.p12`, or Apple ID to the repository.

## 1. Build

```bash
export DEVELOPER_DIR=/Applications/Xcode.app/Contents/Developer
cd ~/Developer/fly-little-games-ios

# arm64 device bundle. Empty FLYNES_IOS_SIGNING_TEAM = unsigned archive.
bash ios/scripts/build_device.sh

# or, for a signed, Xcode-installable bundle:
FLYNES_IOS_SIGNING_TEAM=<YOUR_TEAM_ID> bash ios/scripts/build_device.sh
```

The script fails unless the bundle is arm64 and carries both localizations,
`default.metallib`, and (when signing) a valid signature with a provisioning
profile.

## 2. Install

### Sideloadly (no Apple Developer membership)

```bash
bash ios/scripts/package_device.sh
# -> build/ios-device/evidence/FlyNES-unsigned.ipa (+ .sha256)
```

Copy the IPA to the Windows PC and refresh it with the **same Apple ID and the
same Bundle ID `com.flynes.app`** used previously; see
[ios/docs/sideloadly.md](sideloadly.md) for the full procedure and the 7-day
personal-team limit.

### Xcode / devicectl (signed build)

```bash
xcrun devicectl list devices
xcrun devicectl device install app --device <UDID> \
  build/ios-device/Release-iphoneos/FlyNES.app
```

## 3. Acceptance checklist

Record the observed result for each line; do not mark a line passed without
looking at the phone.

### Layout and hardware adaptation

- [ ] Launches in landscape and stays landscape in the Game Center, run view,
      settings, sources, and layout editor.
- [ ] Nothing is hidden behind the Dynamic Island or the home indicator: the
      status line, category tabs, and pause button keep clear of the cutout.
- [ ] The on-screen D-pad and buttons are inside the safe area on both edges.
- [ ] Rotating the phone (or locking/unlocking) cancels any held direction
      instead of leaving a stuck button.
- [ ] Layout editor drag targets the visual control, not an offset position.
- [ ] Large text (Settings → Accessibility → Larger Text) keeps the header
      compact and the grid usable.

### Game Center and catalog

- [ ] The built-in game shows the trusted titles **From Below / 来自下方**, never
      `from_below.nes`.
- [ ] Switching the app language to 简体中文 promotes 来自下方 and keeps *From
      Below* as the secondary line.
- [ ] Four categories (Recent / Favorites / All / Built-in), search, and the
      selected-card-restored-per-category behaviour match Android.
- [ ] Search finds a game by its filename and by either title, including Chinese.
- [ ] Favorite and Recent markers survive a force-quit and relaunch.
- [ ] Add a source through the real Files picker: a folder, a single `.nes`, and a
      `.zip`. A Chinese-named file and a ZIP with a Chinese entry name both appear.
- [ ] Choose a file, then **cancel** the picker: nothing is added and no error is
      shown.
- [ ] Rescan after adding a file to the folder: the new game appears.
- [ ] Remove a source: its games disappear from the library and your files remain.
- [ ] A launch failure (for example, delete the file, then press Play) reports in
      the Game Center status line and keeps the library on screen.

### Automatic covers

- [ ] Play the built-in game for about 5 seconds, return to the Game Center, and
      the card and left detail show a captured cover instead of the title
      placeholder.
- [ ] A black or blank boot screen never becomes a cover.
- [ ] The cover survives a force-quit and relaunch.
- [ ] No cover file appears in the app container before a game has been played.

### Playback, audio, and input

- [ ] Frames advance with no input at all for at least 30 minutes.
- [ ] Sound is audible and does not compete with other audio; pausing stops it.
- [ ] Using the phone normally (lock, switch apps, take a call) pauses the game and
      restores it, and the pause autosave resumes at the same place.
- [ ] Multi-touch: hold a direction while tapping A/B; rolling from A to B keeps
      both pressed; lifting one finger keeps the other.
- [ ] Each direction press produces the configured haptic, and A/B differ when
      "Distinct A/B haptics" is on.
- [ ] Settings changes apply immediately and survive a relaunch.
- [ ] Display presets: Power saver / Balanced switch spatial filters; Extreme
      stays locked and explains why.
- [ ] Framebuffer size matches the device screen with no letterbox artifacts
      beyond the chosen aspect mode.

### Performance

- [ ] 30 minutes of continuous play: no visible stutter, no thermal warning, and
      the app stays responsive while paused.
- [ ] Watch for dropped frames and memory growth in Instruments if anything looks
      wrong; record the device model, iOS version, and build revision.

## 4. Report back

Record for each checklist line: pass/fail, what you saw, and a screenshot for any
failure. Also record the phone model, iOS version, build path, and the git
revision of `codex/ios-simulator-ready` you installed. Save it under
`ios/docs/evidence/` — do not claim device acceptance from simulator results.
