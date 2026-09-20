import { afterAll, afterEach, beforeEach, describe, expect, it } from '@ohos/hypium';
import { common } from '@kit.AbilityKit';
import { abilityDelegatorRegistry } from '@kit.TestKit';
import { display, window } from '@kit.ArkUI';
import { preferences } from '@kit.ArkData';
import { fileIo } from '@kit.CoreFileKit';
import { Driver, ON } from '@ohos.UiTest';
import nativePlay, { PlayRuntimeStatusDto, SettingsDto } from 'libentry.so';
import { BuiltinGame, BuiltinGames } from '../../../main/ets/service/BuiltinGames';
import { checkpointKey } from '../../../main/ets/service/CheckpointStore';
import { lastRomOpenError, NES_BTN_A, NES_BTN_START } from '../../../main/ets/service/PlayService';

// These are UI/runtime smoke tests, not hardware or full-game certifications.
// Launch the real RunGame route with manifest parameters. Catalog selection is
// covered separately; bypassing it keeps the user's navigation/filter unchanged.
// Normal play history and automatically captured covers may remain after a run.
// No checkpoint save/load is exercised: this product has only the user's slot.
const POLL_MS: number = 25;
const FRAME_WAIT_MS: number = 1600;

function requireTrue(value: boolean, message: string): void {
  if (!value) {
    throw new Error(message);
  }
}

async function pollUntil(predicate: () => boolean, message: string,
  timeoutMs: number = FRAME_WAIT_MS): Promise<void> {
  const deadline = Date.now() + timeoutMs;
  do {
    if (predicate()) {
      return;
    }
    await new Promise<void>((resolve) => { setTimeout(resolve, POLL_MS); });
  } while (Date.now() < deadline);
  throw new Error(message);
}

interface NavigationSnapshot {
  persistedExists: boolean;
  persistedType: string;
  persistedValue: preferences.ValueType;
  memoryExists: boolean;
  memoryType: string;
  memoryValue: boolean | string | number | null;
}

interface RecoveryRecord {
  schemaVersion: number;
  canonicalId: string;
  settings: SettingsDto;
  navigation: NavigationSnapshot;
  note: string;
}

function navigationSnapshot(context: common.UIAbilityContext): NavigationSnapshot {
  const store = preferences.getPreferencesSync(context, { name: 'game_center_ui' });
  const persistedExists = store.hasSync('multiplayerOnly');
  const persistedValue = store.getSync('multiplayerOnly', false);
  const memoryExists = AppStorage.has('GameCenterMultiplayerOnly');
  const memoryValue = AppStorage.get<boolean | string | number>('GameCenterMultiplayerOnly');
  return {
    persistedExists: persistedExists,
    persistedType: persistedExists ? typeof persistedValue : 'absent',
    persistedValue: persistedValue,
    memoryExists: memoryExists,
    memoryType: memoryExists ? typeof memoryValue : 'absent',
    memoryValue: memoryValue === undefined ? null : memoryValue
  };
}

function readExisting(path: string): Uint8Array | undefined {
  if (!fileIo.accessSync(path)) {
    return undefined;
  }
  const file = fileIo.openSync(path, fileIo.OpenMode.READ_ONLY);
  try {
    const bytes = new ArrayBuffer(fileIo.statSync(file.fd).size);
    requireTrue(fileIo.readSync(file.fd, bytes) === bytes.byteLength, 'Checkpoint snapshot was truncated');
    return new Uint8Array(bytes);
  } finally {
    fileIo.closeSync(file.fd);
  }
}

export class BuiltinPlayFixture {
  readonly driver: Driver = Driver.create();
  context: common.UIAbilityContext | undefined = undefined;
  mainWindow: window.Window | undefined = undefined;
  originalSettings: SettingsDto | undefined = undefined;
  originalNavigation: NavigationSnapshot | undefined = undefined;
  checkpoint: Uint8Array | undefined = undefined;
  checkpointPath: string = '';
  evidencePath: string = '';
  settingsChanged: boolean = false;
  ownsRoute: boolean = false;
  primaryFailure: string = '';
  protected cleanupFailure: string = '';

  // Gameplay acceptance overrides this to stop stale async continuations.
  // Ordinary smoke keeps its existing fixture behavior and hook budgets.
  protected checkActive(): void {}

  protected async cleanupAfterFailure(): Promise<void> { await this.cleanup(); }

  private async waitUntil(predicate: () => boolean, message: string,
    timeoutMs: number = FRAME_WAIT_MS): Promise<void> {
    await pollUntil(() => { this.checkActive(); return predicate(); }, message, timeoutMs);
    this.checkActive();
  }

  async prepare(game: BuiltinGame): Promise<void> {
    this.checkActive();
    requireTrue(!this.settingsChanged && !this.ownsRoute && this.cleanupFailure.length === 0,
      `Previous cleanup is incomplete; recover settings from ${this.evidencePath}`);
    this.primaryFailure = '';
    this.cleanupFailure = '';
    // Do not interrupt a user's live game or an earlier failed fixture.
    let alreadyRunning = false;
    try {
      alreadyRunning = nativePlay.playRuntimeStatus().running;
    } catch (_) {
      // The public getter throws when no runtime is open.
    }
    requireTrue(!alreadyRunning, 'An existing play session must finish before builtin smoke');
    const delegator = abilityDelegatorRegistry.getAbilityDelegator();
    await delegator.startAbility({ bundleName: 'com.flynes.emu', abilityName: 'EntryAbility' });
    this.checkActive();
    const foregroundDeadline = Date.now() + FRAME_WAIT_MS;
    this.context = undefined;
    do {
      this.checkActive();
      try {
        const ability = await delegator.getCurrentTopAbility();
        this.checkActive();
        if (ability.context.abilityInfo.name === 'EntryAbility') {
          this.context = ability.context;
          break;
        }
      } catch (_) {
        // No top Ability is available while the foreground transition is pending.
      }
      this.checkActive();
      await new Promise<void>((resolve) => { setTimeout(resolve, POLL_MS); });
    } while (Date.now() < foregroundDeadline);
    if (this.context === undefined) {
      throw new Error('Product EntryAbility did not reach foreground');
    }
    this.mainWindow = await window.getLastWindow(this.context);
    this.checkActive();
    // EntryAbility's asynchronous system-bar changes must settle before RunGame
    // captures its avoid area for the Canvas hit map.
    let fullScreenSince = 0;
    await this.waitUntil(() => {
      const properties = this.mainWindow!.getWindowProperties();
      const avoid = this.mainWindow!.getWindowAvoidArea(window.AvoidAreaType.TYPE_SYSTEM);
      const ready = properties.isLayoutFullScreen && properties.windowRect.width > properties.windowRect.height
        && avoid.leftRect.width === 0 && avoid.rightRect.width === 0
        && avoid.topRect.height === 0 && avoid.bottomRect.height === 0;
      if (!ready) {
        fullScreenSince = 0;
        return false;
      }
      if (fullScreenSince === 0) {
        fullScreenSince = Date.now();
      }
      return Date.now() - fullScreenSince >= 100;
    }, 'Product landscape window did not settle before play launch');
    nativePlay.appOpen(this.context.filesDir, this.context.cacheDir);
    this.originalSettings = nativePlay.settingsGet();
    this.originalNavigation = navigationSnapshot(this.context);
    this.checkpointPath = `${this.context.filesDir}/saves/${checkpointKey(game.canonicalId)}/autosave.nst`;
    this.checkpoint = readExisting(this.checkpointPath);
    const evidenceRoot = `${this.context.filesDir}/builtin-play-smoke-evidence`;
    if (!fileIo.accessSync(evidenceRoot)) {
      fileIo.mkdirSync(evidenceRoot, true);
    }
    this.evidencePath = `${evidenceRoot}/${Date.now()}-${checkpointKey(game.canonicalId)}.json`;
    requireTrue(!fileIo.accessSync(this.evidencePath), 'Refusing to overwrite recovery evidence');
    const record: RecoveryRecord = {
      schemaVersion: 1,
      canonicalId: game.canonicalId,
      settings: this.originalSettings,
      navigation: this.originalNavigation,
      note: 'Original settings before smoke. If interrupted, restore settings with settingsApply; navigation was not modified.'
    };
    const json = JSON.stringify(record);
    const file = fileIo.openSync(this.evidencePath, fileIo.OpenMode.CREATE | fileIo.OpenMode.READ_WRITE);
    try {
      fileIo.writeSync(file.fd, json);
      fileIo.fsyncSync(file.fd);
    } finally {
      fileIo.closeSync(file.fd);
    }
    requireTrue(fileIo.readTextSync(this.evidencePath) === json, 'Recovery evidence failed durable readback');
    console.info(`FlyNES builtin smoke recovery=${this.evidencePath}`);
  }

  async launch(game: BuiltinGame): Promise<void> {
    this.checkActive();
    requireTrue(this.originalSettings !== undefined, 'Original settings were not captured');
    try {
      const temporary = JSON.parse(JSON.stringify(this.originalSettings)) as SettingsDto;
      temporary.autosaveEnabled = 0;
      // Set the flag before the call so a partial native failure is recoverable.
      this.settingsChanged = true;
      requireTrue(nativePlay.settingsApply(temporary).autosaveEnabled === 0, 'Could not disable checkpoint restore/save');
      this.ownsRoute = true;
      await this.mainWindow!.getUIContext().getRouter().replaceUrl({
        url: 'pages/RunGame',
        params: {
          canonicalId: game.canonicalId,
          title: game.titleEn,
          titleEn: game.titleEn,
          titleZhHans: game.titleZhHans
        }
      });
      this.checkActive();
      await this.waitUntil(() => {
        try {
          const status = nativePlay.playRuntimeStatus();
          return status.running && !status.paused && status.sourceFrames > 1 && status.error.length === 0;
        } catch (_) {
          return false;
        }
      }, `No real source frames after UI launch: ${game.canonicalId}`);
    } catch (error) {
      console.error(`FlyNES builtin smoke launch diagnostic route=${JSON.stringify(this.mainWindow!.getUIContext().getRouter().getState())}`
        + ` params=${JSON.stringify(this.mainWindow!.getUIContext().getRouter().getParams())} romError=${lastRomOpenError}`);
      this.primaryFailure = `${error}`;
      await this.cleanupAfterFailure();
      throw error as Error;
    }
  }

  async clickText(text: string): Promise<void> {
    this.checkActive();
    const button = await this.driver.waitForComponent(ON.text(text).type('Button'), 700);
    this.checkActive();
    requireTrue(button !== undefined && button !== null, `Required game control is missing: ${text}`);
    await button!.click();
    this.checkActive();
  }

  async pause(): Promise<void> {
    await this.clickText('II');
    await this.waitUntil(() => nativePlay.playRuntimeStatus().paused, 'Pause did not reach the runtime', 500);
  }

  async assertFramesAdvance(): Promise<void> {
    this.checkActive();
    const before = nativePlay.playRuntimeStatus().sourceFrames;
    await this.waitUntil(() => nativePlay.playRuntimeStatus().sourceFrames >= before + 3,
      'Source frames did not advance');
    const frame = nativePlay.playStep(); // Copies the latest frame; never steps the core.
    expect(frame.width).assertEqual(256);
    expect(frame.height).assertEqual(240);
    expect(frame.bytesWritten).assertEqual(256 * 240 * 2);
    expect(frame.rgb565.byteLength).assertEqual(256 * 240 * 2);
    expect(frame.frameIndex > 0).assertTrue();
    expect(nativePlay.playRuntimeStatus().error).assertEqual('');
    console.info(`FlyNES builtin smoke frames before=${before} after=${nativePlay.playRuntimeStatus().sourceFrames}`
      + ` frameIndex=${frame.frameIndex} rgb565Bytes=${frame.rgb565.byteLength}`);
  }

  async assertTouchApplied(control: string, bit: number): Promise<void> {
    const canvas = await this.driver.waitForComponent(ON.type('Canvas'), 700);
    requireTrue(canvas !== undefined && canvas !== null, 'Gamepad Canvas is not visible');
    const canvasBounds = await canvas!.getBounds();
    const win = this.mainWindow!;
    const rect = win.getWindowProperties().windowRect;
    const avoid = win.getWindowAvoidArea(window.AvoidAreaType.TYPE_SYSTEM);
    const settings = nativePlay.settingsGet();
    const hit = nativePlay.hitMapFromLayout(rect.width, rect.height,
      display.getDefaultDisplaySync().densityDPI / 160,
      avoid.leftRect.width, avoid.rightRect.width, avoid.topRect.height, avoid.bottomRect.height,
      nativePlay.controlLayoutGet(), Math.max(0, Math.min(2, settings.directionMode - 1)), settings.deadZone);
    const target = hit.controls.find((candidate) => candidate.control === control);
    requireTrue(target !== undefined, `No overlay hit target for ${control}`);
    console.info(`FlyNES builtin smoke touch geometry control=${control} rect=${JSON.stringify(rect)}`
      + ` canvas=${JSON.stringify(canvasBounds)} target=${JSON.stringify(target)} avoid=${JSON.stringify(avoid)}`);
    let applied = false;
    let samples = 0;
    let observedMask = 0;
    let observerFailure = '';
    const before = nativePlay.playRuntimeStatus().sourceFrames;
    const observer = setInterval(() => {
      try {
        const frame = nativePlay.playStep();
        samples++;
        observedMask |= frame.appliedButtons;
        applied = applied || ((frame.appliedButtons & bit) !== 0);
      } catch (error) {
        observerFailure = `${error}`;
      }
    }, POLL_MS);
    try {
      // UiTest sends the actual Canvas touch. The native getter only observes it.
      await this.driver.longClick(Math.round(rect.left + target!.centerX),
        Math.round(rect.top + target!.centerY));
    } finally {
      clearInterval(observer);
    }
    requireTrue(observerFailure.length === 0, `Input observer failed: ${observerFailure}`);
    if (!applied) {
      await this.driver.screenCap(`${this.evidencePath}.png`);
    }
    requireTrue(applied, `UI ${control} touch never reached an emulated frame; samples=${samples}`
      + ` mask=${observedMask} frames=${before}->${nativePlay.playRuntimeStatus().sourceFrames}`);
    requireTrue(nativePlay.playRuntimeStatus().sourceFrames > before, 'No source frames during UI input');
    await pollUntil(() => nativePlay.playStep().appliedButtons === 0,
      `UI ${control} release did not clear the input mask`, 500);
    console.info(`FlyNES builtin smoke input control=${control} appliedMask=${bit} releasedMask=0`);
  }

  async assertPauseResume(): Promise<void> {
    await this.pause();
    this.checkActive();
    let stopped = nativePlay.playRuntimeStatus().sourceFrames;
    let stableSince = Date.now();
    // A frame already inside the simulation worker may complete as pause lands.
    // Observe a stable window, bounded by the polling deadline, rather than sleep.
    await this.waitUntil(() => {
      const status: PlayRuntimeStatusDto = nativePlay.playRuntimeStatus();
      requireTrue(status.paused, 'Runtime left pause without a resume action');
      if (status.sourceFrames !== stopped) {
        stopped = status.sourceFrames;
        stableSince = Date.now();
      }
      return Date.now() - stableSince >= 200;
    }, 'Source frames did not stop while paused', 600);
    console.info(`FlyNES builtin smoke pausedFrame=${stopped} stableMs=${Date.now() - stableSince}`);
    await this.clickText(this.context!.resourceManager.getStringByNameSync('pause_resume'));
    await this.waitUntil(() => !nativePlay.playRuntimeStatus().paused, 'Resume did not reach the runtime', 500);
    await this.assertFramesAdvance();
  }

  async cleanup(): Promise<void> {
    this.checkActive();
    const failures: string[] = [];
    if (this.ownsRoute) {
      let routeExited = false;
      let runtimeClosed = false;
      try {
        const current = this.mainWindow!.getUIContext().getRouter().getState();
        routeExited = current.name === 'GameCenter' || current.path.indexOf('GameCenter') >= 0;
        if (!routeExited) {
          if (!nativePlay.playRuntimeStatus().paused) {
            await this.pause();
          }
          this.checkActive();
          await this.clickText(this.context!.resourceManager.getStringByNameSync('pause_game_center'));
          this.checkActive();
          await this.waitUntil(() => {
            const state = this.mainWindow!.getUIContext().getRouter().getState();
            return state.name === 'GameCenter' || state.path.indexOf('GameCenter') >= 0;
          }, 'Return to game library did not complete', 600);
          routeExited = true;
          console.info('FlyNES builtin smoke returnedToGameCenter=true');
        }
      } catch (error) {
        this.checkActive();
        failures.push(`return to library: ${error}`);
      }
      // Each fallback/restoration runs independently, including after failed UI actions.
      try {
        this.checkActive();
        nativePlay.playClose();
        runtimeClosed = true;
      } catch (error) {
        failures.push(`close runtime: ${error}`);
      }
      if (!routeExited) {
        try {
          await this.mainWindow!.getUIContext().getRouter().replaceUrl({ url: 'pages/GameCenter' });
          this.checkActive();
          await this.waitUntil(() => {
            const state = this.mainWindow!.getUIContext().getRouter().getState();
            return state.name === 'GameCenter' || state.path.indexOf('GameCenter') >= 0;
          }, 'Fallback return to game library did not complete', 600);
          routeExited = true;
        } catch (error) {
          this.checkActive();
          failures.push(`reset route: ${error}`);
        }
      }
      // Failed navigation or runtime close must remain retryable in afterAll.
      this.ownsRoute = !(routeExited && runtimeClosed);
    }
    if (this.settingsChanged) {
      this.checkActive();
      try {
        const restored = nativePlay.settingsApply(this.originalSettings!);
        requireTrue(JSON.stringify(restored) === JSON.stringify(this.originalSettings), 'Settings did not restore exactly');
        this.settingsChanged = false;
      } catch (error) {
        failures.push(`restore settings (recovery ${this.evidencePath}): ${error}`);
      }
      try {
        requireTrue(JSON.stringify(navigationSnapshot(this.context!)) === JSON.stringify(this.originalNavigation),
          'Navigation preference changed during smoke');
      } catch (error) {
        failures.push(`navigation preservation: ${error}`);
      }
      try {
        const after = readExisting(this.checkpointPath);
        requireTrue((after === undefined) === (this.checkpoint === undefined), 'Checkpoint existence changed');
        if (after !== undefined && this.checkpoint !== undefined) {
          requireTrue(after.length === this.checkpoint.length, 'Checkpoint size changed');
          for (let index = 0; index < after.length; index++) {
            requireTrue(after[index] === this.checkpoint[index], 'Checkpoint bytes changed');
          }
        }
      } catch (error) {
        failures.push(`checkpoint preservation: ${error}`);
      }
    }
    if (failures.length > 0) {
      const message = failures.join('; ');
      // Preserve the cleanup outcome even when Hypium must retain a prior error.
      if (this.cleanupFailure.length === 0) { this.cleanupFailure = message; }
      console.error(`FlyNES builtin smoke cleanup: ${message}`);
      // Hypium replaces a spec's error if an afterEach throws; keep the first failure.
      if (this.primaryFailure.length === 0) {
        throw new Error(message);
      }
    }
  }

  async checked(body: () => Promise<void>): Promise<void> {
    try {
      await body();
    } catch (error) {
      this.primaryFailure = `${error}`;
      try {
        // Hypium skips afterEach when the spec throws; finish this fixture now
        // so an input failure does not prevent the independent remaining cases.
        await this.cleanup();
      } finally {
        // A cleanup failure must never replace the original assertion/error.
        throw error as Error;
      }
    }
  }
}

export default function builtinPlaySmokeTest(): void {
  const context = abilityDelegatorRegistry.getAbilityDelegator().getAppContext() as common.UIAbilityContext;
  const games = BuiltinGames.loadSync(context).all();
  for (let index = 0; index < games.length; index++) {
    const game = games[index];
    describe(`BuiltinPlaySmoke_${index + 1}`, () => {
      const fixture = new BuiltinPlayFixture();
      // Real setup/cleanup hooks each retain Hypium's default five-second budget.
      beforeEach(async () => { await fixture.prepare(game); });
      beforeEach(async () => { await fixture.launch(game); });
      afterEach(async () => { await fixture.cleanup(); });
      // Also restores after setup failure; Hypium skips afterEach in that case.
      afterAll(async () => { await fixture.cleanup(); });
      it('real_frames_and_START_touch', 0, async () => {
        await fixture.checked(async () => {
          await fixture.assertFramesAdvance();
          await fixture.assertTouchApplied('START', NES_BTN_START);
        });
      });
      it('A_touch_reaches_emulated_frame', 0, async () => {
        await fixture.checked(async () => { await fixture.assertTouchApplied('A', NES_BTN_A); });
      });
      it('pause_stops_frames_and_resume_advances', 0, async () => {
        await fixture.checked(async () => { await fixture.assertPauseResume(); });
      });
    });
  }
}
