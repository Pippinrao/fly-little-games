import { common } from '@kit.AbilityKit';
import { display, router, window } from '@kit.ArkUI';
import { image } from '@kit.ImageKit';
import { i18n } from '@kit.LocalizationKit';
import { localizedGameTitle } from '../service/CatalogProductService';
import { vibrator } from '@kit.SensorServiceKit';
import { batteryInfo, BusinessError, thermal } from '@kit.BasicServicesKit';
import nativePlay, { PlayRuntimeStatusDto, PlayStepResult, RenderStatusDto, SettingsDto } from 'libentry.so';
import { GamepadOverlay } from '../overlay/GamepadOverlay';
import { PlayService, rememberRomOpenError, RomLaunchLocator } from '../service/PlayService';
import { CoverStore } from '../service/CoverStore';
import { parsePauseTitle } from './PauseParams';
import { computeViewport, ViewportSize } from './ViewportGeometry';
import {
  NearbyActionOption,
  NearbyBanner,
  NearbyInGameRow,
  nearbyAuthorityTimeoutOptions,
  nearbyInGameBanner,
  nearbyInGameRows,
  nearbyString
} from '../service/NearbyService';

const DIRECTION_FIXED_JOYSTICK = 1;
const DEFAULT_DEAD_ZONE = 0.18;
// Frame indices sampled for cover capture (matches Android 2/4/6/8s at 60fps).
const COVER_SAMPLE_OFFSETS: number[] = [120, 240, 360, 480];

@Entry
@Component
struct RunGame {
  private readonly play: PlayService = new PlayService();
  private timerId: number = -1;
  private started: boolean = false;
  private nearby: boolean = false;
  private returningToLobby: boolean = false;
  @State private canonicalId: string = '';
  private readonly locator: RomLaunchLocator = new RomLaunchLocator();
  @State private frame: image.PixelMap | undefined = undefined;
  @State private pixelFallback: boolean = false;
  @State private density: number = 3;
  @State private insetL: number = 0;
  @State private insetR: number = 0;
  @State private insetT: number = 0;
  @State private insetB: number = 0;
  @State private layoutUtf8: string = '';
  @State private pauseIds: string[] = [];
  @State private overlayMounted: boolean = true;
  @State private paused: boolean = false;
  @State private drawerOpen: boolean = false;
  @State private gameTitle: string = 'Game paused';
  private titleEn: string = '';
  private titleZhHans: string = '';
  @State private pressedBits: number = 0;
  @State private checkpointFailed: boolean = false;
  @State private status: string = '';
  @State private directionMode: number = DIRECTION_FIXED_JOYSTICK;
  @State private deadZone: number = DEFAULT_DEAD_ZONE;
  // Settings-driven behavior (Android parity: MainActivity + GamepadView).
  @State private aspectMode: number = 1;
  @State private audioEnabled: boolean = true;
  @State private autosaveEnabled: boolean = true;
  @State private hapticLevel: number = 2;
  @State private distinctAbHaptics: boolean = true;
  @State private frameAspect: number = 256 / 240;
  @State private viewportW: number = 0;
  @State private viewportH: number = 0;
  private hapticLastMs: number = 0;
  private coverStore: CoverStore | undefined = undefined;
  private coverGameId: string = '';
  private coverSample: number = 0;
  private coverBusy: boolean = false;
  private coverStored: boolean = false;
  private renderEvidenceFrames: number = 0;
  private resumeAfterSettings: boolean = false;
  private protectionObservationFailed: boolean = false;

  aboutToAppear(): void {
    const params = router.getParams() as Record<string, Object>;
    this.nearby = params !== undefined && params['nearby'] === true;
    this.gameTitle = parsePauseTitle(params, 'Game paused');
    this.titleEn = params !== undefined && typeof params['titleEn'] === 'string' ? params['titleEn'] as string : '';
    this.titleZhHans = params !== undefined && typeof params['titleZhHans'] === 'string' ? params['titleZhHans'] as string : '';
    if (params !== undefined) {
      if (params['canonicalId'] !== undefined) {
        this.canonicalId = params['canonicalId'] as string;
        this.locator.canonicalId = this.canonicalId;
      }
      if (params['sourceUuidHex'] !== undefined) {
        this.locator.sourceUuidHex = params['sourceUuidHex'] as string;
      }
      if (params['sourceRelativePath'] !== undefined) {
        this.locator.sourceRelativePath = params['sourceRelativePath'] as string;
      }
      if (params['packageFormat'] !== undefined) {
        this.locator.packageFormat = Number.parseInt(params['packageFormat'] as string, 10);
      }
      if (params['zipEntryName'] !== undefined) {
        this.locator.zipEntryName = params['zipEntryName'] as string;
      }
    }
    this.pauseIds = nativePlay.pauseCommands().filter((id: string) => !this.nearby || id !== 'settings');
    this.readDisplayMetrics();
    const hostContext = this.getUIContext().getHostContext();
    if (hostContext === undefined) {
      this.status = 'UIAbility context is unavailable';
      return;
    }
    const context = hostContext as common.UIAbilityContext;
    this.ensureApp(context);
    this.loadPersistedLayout();
    this.requestLandscape(context);
    if (!this.started) {
      void this.start(context);
    }
  }

  onPageShow(): void {
    this.loadPersistedLayout();
    this.gameTitle = localizedGameTitle(this.titleEn, this.titleZhHans, this.gameTitle,
      nativePlay.settingsGet().localeTag, i18n.System.getSystemLanguage());
    if (this.resumeAfterSettings) {
      this.resumeAfterSettings = false;
      this.resumePlay();
      return;
    }
    if (this.started && !this.paused) {
      nativePlay.renderSetPaused(false);
      this.startLoop();
    }
  }

  onPageHide(): void {
    if (this.nearby) { this.play.setButtons(0); nativePlay.nearbyMvpSetPaused(true); }
    this.stopLoop();
    nativePlay.renderSetPaused(true);
  }

  aboutToDisappear(): void {
    this.stopLoop();
    this.play.close();
    this.frame?.release();
    this.frame = undefined;
    this.started = false;
  }

  onBackPress(): boolean {
    if (!this.nearby) return false;
    this.openPause();
    return true;
  }

  build() {
    Stack() {
      if (this.pixelFallback && this.frame !== undefined) {
        Image(this.frame)
          .width(this.viewportW > 0 ? this.viewportW : '100%')
          .height(this.viewportH > 0 ? this.viewportH : '100%')
          .objectFit(ImageFit.Fill)
          .backgroundColor('#000000')
          .alt($r('app.string.play_screen_alt'))
      } else {
        XComponent({
          id: 'flynes-render-surface',
          type: XComponentType.SURFACE,
          libraryname: 'entry'
        })
          .id('flynes-render-surface')
          .width(this.viewportW > 0 ? this.viewportW : '100%')
          .height(this.viewportH > 0 ? this.viewportH : '100%')
          .backgroundColor('#000000')
      }

      if (!this.started && this.status.length > 0) {
        Column({ space: 16 }) {
          Text(this.status)
            .fontColor('#A8B0BD')
            .fontSize(18)
          Button($r('app.string.game_center_title'))
            .height(48)
            .fontSize(16)
            .onClick(() => {
              router.back();
            })
        }
        .width('100%')
        .height('100%')
        .justifyContent(FlexAlign.Center)
        .backgroundColor('#000000')
      }

      if (!this.drawerOpen && this.overlayMounted) {
        GamepadOverlay({
          density: this.density,
          insetL: this.insetL,
          insetR: this.insetR,
          insetT: this.insetT,
          insetB: this.insetB,
          layoutUtf8: this.layoutUtf8,
          directionMode: this.directionMode,
          deadZone: this.deadZone,
          pressedBits: this.pressedBits,
          onButtons: (bits: number) => {
            this.handleOverlayButtons(bits);
          }
        })
          .width('100%')
          .height('100%')
      }

      if (!this.drawerOpen) {
        Stack({ alignContent: Alignment.TopEnd }) {
          Button('II')
            .width(48)
            .height(48)
            .fontSize(16)
            .fontColor('#F4EFE6')
            .fontWeight(FontWeight.Bold)
            .backgroundColor('#B81B1D22')
            .border({ width: 2, color: '#FF6B5E' })
            .borderRadius(24)
            .accessibilityText($r('app.string.open_pause'))
            .margin({
              top: 16 + this.getUIContext().px2vp(this.insetT),
              right: 16 + this.getUIContext().px2vp(this.insetR)
            })
            .onClick(() => {
              this.openPause();
            })
        }
        .width('100%')
        .height('100%')
        .hitTestBehavior(HitTestMode.Transparent)
      }

      // D6/D7: only a live session with an action-demanding state pins a banner to
      // the top of the run surface. It is not a modal and cannot be dismissed while
      // the state persists, so a frozen end never has to hunt through the drawer.
      if (nearbyInGameBanner(this.nearbySessionExists(), this.nearbyUiState()) !== undefined) {
        this.nearbyBanner()
      }

      if (this.drawerOpen) {
        this.pauseDrawer()
      }
    }
    .width('100%')
    .height('100%')
    .backgroundColor('#000000')
  }

  @Builder
  pauseDrawer() {
    Stack({ alignContent: Alignment.End }) {
      Column()
        .width('100%')
        .height('100%')
        .backgroundColor('#8A000000')
        .transition(TransitionEffect.OPACITY.animation({ duration: 180 }))
        .onClick(() => {
          this.resumePlay();
        })
      Column({ space: 12 }) {
        Text($r('app.string.pause_eyebrow'))
          .fontSize(14)
          .fontColor('#FF6B5E')
          .fontWeight(FontWeight.Bold)
          .width('100%')
        Text(this.gameTitle)
          .fontSize(28)
          .fontColor('#F4EFE6')
          .fontWeight(FontWeight.Bold)
          .width('100%')
        if (this.checkpointFailed) {
          Text($r('app.string.pause_checkpoint_failed'))
            .fontSize(14)
            .fontColor('#FF6B5E')
            .width('100%')
        }
        ForEach(this.pauseIds, (id: string) => {
          Button(this.pauseLabel(id))
            .width('100%')
            .height(id === 'resume' ? 56 : 48)
            .fontSize(16)
            .fontColor(id === 'resume' ? '#121316' : '#F4EFE6')
            .backgroundColor(id === 'resume' ? '#FF6B5E' : '#25282F')
            .borderRadius(14)
            .onClick(() => {
              this.handlePauseCommand(id);
            })
        }, (id: string) => id)
        // In-game nearby status block (design §22.4). It lives in the existing
        // pause drawer, not a persistent HUD (D6), and renders nothing at all
        // while there is no session (D7).
        if (this.nearbySessionExists()) {
          this.nearbyStatusBlock()
        }
      }
      .width(320)
      .height('100%')
      .padding({
        left: 24,
        right: 24 + this.getUIContext().px2vp(this.insetR),
        top: 24,
        bottom: 24
      })
      .justifyContent(FlexAlign.Center)
      .backgroundColor('#1B1D22')
      .transition(TransitionEffect.translate({ x: 320 }).animation({ duration: 220, curve: Curve.EaseOut }))
      .onClick(() => {
      })
    }
    .width('100%')
    .height('100%')
  }

  // D6: the non-dismissible banner. 冻结 / 重连倒计时 / authority 超时 are the states
  // that demand user action; nothing dismisses this row while the state persists.
  @Builder
  nearbyBanner() {
    Column({ space: 8 }) {
      Text(nearbyString(this.bannerLabel()))
        .fontSize(18)
        .fontWeight(FontWeight.Bold)
        .fontColor('#F4EFE6')
        .width('100%')
      Text(nearbyString(this.bannerReason()))
        .fontSize(13)
        .fontColor('#FF6B5E')
        .width('100%')
      // 接管 / 继续单人 / 保存结束. The reason is a table string chosen by the
      // session's reason-enum kind, never wire text (D9). This build has no
      // session surface, so the options are present and disabled with a reason.
      if (this.bannerHasActions()) {
        ForEach(nearbyAuthorityTimeoutOptions(), (option: NearbyActionOption) => {
          Column({ space: 4 }) {
            Button(nearbyString(option.label))
              .width('100%')
              .height(48)
              .fontSize(16)
              .enabled(false)
            Text(nearbyString(option.reason))
              .fontSize(12)
              .fontColor('#A8B0BD')
              .width('100%')
          }
          .width('100%')
        }, (option: NearbyActionOption) => option.label)
      }
    }
    .width('100%')
    .padding(12)
    .margin({ top: 8 + this.getUIContext().px2vp(this.insetT), left: 8, right: 8 })
    .backgroundColor('#E61B1D22')
    .border({ width: 1, color: '#FF6B5E' })
    .borderRadius(12)
    .alignItems(HorizontalAlign.Start)
  }

  // Every §22.4 field is a row label plus the reason its value cannot be read yet.
  // D10: 明确不会自动合并 is an always-known invariant, so it renders with no blocked
  // key and the two-branch visualisation stays deferred.
  @Builder
  nearbyStatusBlock() {
    Column({ space: 8 }) {
      ForEach(nearbyInGameRows(), (row: NearbyInGameRow) => {
        Column({ space: 2 }) {
          Text(nearbyString(row.label))
            .fontSize(16)
            .fontColor('#F4EFE6')
            .width('100%')
          if (row.blocked.length > 0) {
            Text(nearbyString(row.blocked))
              .fontSize(13)
              .fontColor('#A8B0BD')
              .width('100%')
          }
        }
        .width('100%')
      }, (row: NearbyInGameRow) => row.label)
    }
    .width('100%')
    .padding({ top: 8 })
    .alignItems(HorizontalAlign.Start)
  }

  private bannerState(): NearbyBanner | undefined {
    return nearbyInGameBanner(this.nearbySessionExists(), this.nearbyUiState());
  }

  private bannerLabel(): string {
    const banner = this.bannerState();
    return banner === undefined ? '' : banner.label;
  }

  private bannerReason(): string {
    const banner = this.bannerState();
    return banner === undefined ? '' : banner.reason;
  }

  private bannerHasActions(): boolean {
    const banner = this.bannerState();
    return banner === undefined ? false : banner.actions;
  }

  /**
   * D7: with no session there is no nearby block and no banner. The session surface
   * is not wired into the Harmony binding yet (§3), so this is false today and the
   * local single-player surface stays exactly as it was.
   */
  private nearbySessionExists(): boolean {
    return false;
  }

  /**
   * `fly_session_snapshot.ui_state`. The only defined value is
   * `FLY_SESSION_UI_UNSPECIFIED` (0), so no action-demanding state is known and no
   * banner is pinned until the session surface lands.
   */
  private nearbyUiState(): number {
    return 0;
  }

  private async start(context: common.UIAbilityContext): Promise<void> {
    try {
      if (this.nearby) this.play.openNearby();
      else await this.play.open(context, this.locator, this.autosaveEnabled);
      this.started = true;
      // onPageShow can run before the asynchronous ROM open completes. Clear
      // any pause retained by an earlier surface only after this session owns
      // a running core, so its first source frame can reach the GPU mailbox.
      nativePlay.renderSetPaused(false);
      this.status = '';
      this.markPlayedOnce();
      this.startLoop();
    } catch (error) {
      this.status = error instanceof Error ? error.message : 'Failed to start play session';
      rememberRomOpenError(this.status);
      router.back();
    }
  }

  /** Records the play for Game Center Recent ordering (Android parity). */
  private markPlayedOnce(): void {
    if (this.coverGameId.length > 0) {
      return;
    }
    this.coverGameId = this.locator.canonicalId;
    if (this.coverGameId.length === 0) {
      return;
    }
    try {
      nativePlay.catalogMarkPlayed(this.coverGameId);
    } catch (error) {
      const detail = error instanceof Error ? error.message : 'markPlayed failed';
      console.error(`FlyNES catalogMarkPlayed failed: ${detail}`);
    }
  }

  private startLoop(): void {
    if (this.timerId !== -1 || this.paused) {
      return;
    }
    this.timerId = setInterval(() => {
      this.tick();
    }, 16);
  }

  private stopLoop(): void {
    if (this.timerId !== -1) {
      clearInterval(this.timerId);
      this.timerId = -1;
    }
  }

  private tick(): void {
    try {
      if (this.nearby) {
        const state = nativePlay.nearbyMvpSnapshot();
        if (state.state !== 6) {
          if (!this.returningToLobby) {
            this.returningToLobby = true;
            this.stopLoop();
            void router.replaceUrl({ url: 'pages/NearbyLobby' });
          }
          return;
        }
      }
      if (this.renderEvidenceFrames % 120 === 0) {
        this.updatePlatformProtection();
      }
      const renderStatus: RenderStatusDto = nativePlay.renderStatus();
      const playStatus: PlayRuntimeStatusDto = nativePlay.playRuntimeStatus();
      this.renderEvidenceFrames += 1;
      if (this.renderEvidenceFrames % 120 === 0) {
        console.info(`FlyNES render evidence ${JSON.stringify(renderStatus)}`);
        console.info(`FlyNES runtime evidence ${JSON.stringify(playStatus)}`);
      }
      const needsPixelFallback: boolean = renderStatus.fallbackActive && !renderStatus.nativeReady;
      this.pixelFallback = needsPixelFallback;
      const coverDue: boolean = this.coverSample < COVER_SAMPLE_OFFSETS.length &&
        playStatus.sourceFrames >= COVER_SAMPLE_OFFSETS[this.coverSample];
      if (needsPixelFallback || coverDue) {
        const stepped: PlayStepResult = this.play.stepFrame();
        this.considerCover(stepped.frameIndex, stepped.rgb565);
        if (!needsPixelFallback) {
          return;
        }
        const next = this.play.createFrame(stepped.rgb565);
        const old = this.frame;
        this.frame = next;
        old?.release();
      } else if (this.frame !== undefined) {
        this.frame.release();
        this.frame = undefined;
      }
    } catch (error) {
      this.stopLoop();
      this.status = error instanceof Error ? error.message : 'Frame step failed';
    }
  }
  /** Mirrors Android CoverCaptureCoordinator: samples frames at 2/4/6/8s. */
  private considerCover(frameIndex: number, rgb565: ArrayBuffer): void {
    if (this.coverSample >= COVER_SAMPLE_OFFSETS.length) {
      return;
    }
    if (frameIndex < COVER_SAMPLE_OFFSETS[this.coverSample]) {
      return;
    }
    this.coverSample += 1;
    if (this.coverGameId.length === 0 || this.coverBusy) {
      return;
    }
    this.coverBusy = true;
    const snapshot = rgb565.slice(0);
    this.ensureCoverStore();
    const store = this.coverStore;
    if (store === undefined) {
      this.coverBusy = false;
      return;
    }
    store.storeFromRgb565(this.coverGameId, snapshot).then((stored: boolean) => {
      this.coverBusy = false;
      if (stored) {
        this.coverStored = true;
        console.info(`FlyNES cover stored for ${this.coverGameId}`);
      }
    }).catch((error: Object) => {
      this.coverBusy = false;
      const detail = error instanceof Error ? error.message : 'cover capture failed';
      console.error(`FlyNES cover capture failed: ${detail}`);
    });
  }

  private ensureCoverStore(): void {
    if (this.coverStore !== undefined) {
      return;
    }
    const host = this.getUIContext().getHostContext();
    if (host === undefined) {
      return;
    }
    this.coverStore = new CoverStore((host as common.UIAbilityContext).filesDir);
  }

  private handleOverlayButtons(bits: number): void {
    const rising = bits & ~this.pressedBits;
    this.pressedBits = bits;
    this.play.setButtons(bits);
    if (this.nearby) console.info(`FlyNesNearby event=touch buttons=${bits}`);
    if (rising !== 0) {
      this.fireHaptic(rising);
    }
  }

  private openPause(): void {
    if (this.drawerOpen) {
      return;
    }
    if (this.nearby) {
      this.play.setButtons(0);
      this.pressedBits = 0;
      nativePlay.nearbyMvpSetPaused(true);
      this.drawerOpen = true;
      return;
    }
    if (!this.paused) {
      this.play.setButtons(0);
      this.pressedBits = 0;
      this.stopLoop();
      this.play.pauseOutput();
      nativePlay.renderSetPaused(true);
      console.info(`FlyNES pause evidence ${JSON.stringify(nativePlay.playRuntimeStatus())}`);
      this.checkpointFailed = false;
      // Settings parity: general.autosave=0 skips writing the resume checkpoint.
      if (this.autosaveEnabled) {
        try {
          this.play.saveCheckpoint();
        } catch (error) {
          this.checkpointFailed = true;
          this.status = error instanceof Error ? error.message : 'checkpoint failed';
        }
      }
    }
    this.paused = true;
    this.drawerOpen = true;
  }

  private resumePlay(): void {
    this.drawerOpen = false;
    if (this.nearby) { nativePlay.nearbyMvpSetPaused(false); return; }
    if (!this.paused) {
      return;
    }
    this.paused = false;
    this.checkpointFailed = false;
    this.play.resumeOutput();
    nativePlay.renderSetPaused(false);
    console.info(`FlyNES resume evidence ${JSON.stringify(nativePlay.playRuntimeStatus())}`);
    this.startLoop();
  }

  private handlePauseCommand(id: string): void {
    if (id === 'resume') {
      this.resumePlay();
      return;
    }
    if (id === 'game_center') {
      if (this.nearby) {
        nativePlay.nearbyMvpReturnLobby();
        this.returningToLobby = true;
        this.stopLoop();
        void router.replaceUrl({ url: 'pages/NearbyLobby' });
        return;
      }
      this.drawerOpen = false;
      this.stopLoop();
      this.play.close();
      nativePlay.renderSetPaused(true);
      this.started = false;
      void router.replaceUrl({ url: 'pages/GameCenter' });
      return;
    }
    if (id === 'settings') {
      if (this.nearby) return;
      this.drawerOpen = false;
      this.paused = true;
      this.resumeAfterSettings = true;
      void router.pushUrl({ url: 'pages/Settings' });
    }
  }

  private pauseLabel(id: string): Resource {
    if (id === 'resume') {
      return $r('app.string.pause_resume');
    }
    if (id === 'game_center') {
      return this.nearby ? $r('app.string.nearby_lobby_title') : $r('app.string.pause_game_center');
    }
    return $r('app.string.pause_settings');
  }

  private ensureApp(context: common.UIAbilityContext): void {
    nativePlay.appOpen(context.filesDir, context.cacheDir);
  }

  private loadPersistedLayout(): void {
    try {
      this.layoutUtf8 = nativePlay.controlLayoutGet();
      const settings: SettingsDto = nativePlay.settingsGet();
      this.directionMode = Math.max(0, Math.min(2, settings.directionMode - 1));
      this.deadZone = settings.deadZone;
      this.aspectMode = settings.aspectMode;
      this.audioEnabled = settings.audioEnabled === 1;
      this.autosaveEnabled = settings.autosaveEnabled === 1;
      this.hapticLevel = Math.max(1, Math.min(4, settings.hapticLevel));
      this.distinctAbHaptics = settings.distinctAbHaptics === 1;
      const refresh: number = settings.videoQualityPreset === 4 ? settings.customRefreshPolicy : 3;
      const temporal: number = settings.videoQualityPreset === 4 ? settings.customTemporalMode : 1;
      const spatial: number = settings.videoQualityPreset === 1 ? 1 :
        settings.videoQualityPreset === 2 || settings.videoQualityPreset === 3 ? 2 :
          settings.customSpatialMode;
      const post: number = settings.videoQualityPreset === 4 ? settings.customPostEffect : 1;
      nativePlay.renderConfigure(refresh, temporal, spatial, post,
        settings.adaptiveProtection === 1);
      this.updatePlatformProtection();
      console.info(`FlyNES configured evidence ${JSON.stringify({
        refresh: refresh,
        temporal: temporal,
        spatial: spatial,
        post: post,
        adaptiveProtection: settings.adaptiveProtection === 1
      })}`);
      this.play.setAudioMuted(!this.audioEnabled);
    } catch (error) {
      this.layoutUtf8 = nativePlay.controlLayoutRecommended();
      const detail = error instanceof Error ? error.message : 'layout get failed';
      console.error(`FlyNES layout load failed: ${detail}`);
    }
    this.updateViewport();
    this.remountOverlay();
  }

  private updatePlatformProtection(): void {
    try {
      const thermalLimited: boolean = thermal.getLevel() >= thermal.ThermalLevel.HOT;
      const lowBattery: boolean = batteryInfo.batterySOC >= 0 && batteryInfo.batterySOC <= 15;
      nativePlay.renderSetProtection(thermalLimited, lowBattery, true);
      this.protectionObservationFailed = false;
    } catch (error) {
      nativePlay.renderSetProtection(false, false, false);
      if (!this.protectionObservationFailed) {
        const detail = error instanceof Error ? error.message : 'platform protection query failed';
        console.error(`FlyNES protection observation failed: ${detail}`);
        this.protectionObservationFailed = true;
      }
    }
  }

  /**
   * Android GamepadView parity: control presses fire HapticController.feedback.
   * Level OFF(1) suppresses; B gets a distinct double-pulse cadence when enabled.
   * Level maps to preset intensity like HapticPattern (LIGHT 72/STANDARD 140/STRONG 220).
   */
  private fireHaptic(bits: number): void {
    if (this.hapticLevel === 1) {
      return;
    }
    const intensity: number = [0, 0, 40, 70, 100][this.hapticLevel];
    const isB = (bits & 0x02) !== 0;
    // Throttle: direction pads stream repeated bits every frame.
    const now = Date.now();
    if (now - this.hapticLastMs < 40) {
      return;
    }
    this.hapticLastMs = now;
    const count = isB && this.distinctAbHaptics ? 2 : 1;
    const effect: vibrator.VibratePreset = {
      type: 'preset',
      effectId: 'haptic.clock.timer',
      count: count,
      intensity: intensity
    };
    try {
      vibrator.startVibration(effect, { usage: 'touch' }, (error: BusinessError) => {
        if (error && error.code !== 0) {
          console.error(`FlyNES haptic failed: ${error.code}`);
        }
      });
    } catch (error) {
      const detail = error instanceof Error ? error.message : 'haptic failed';
      console.error(`FlyNES haptic failed: ${detail}`);
    }
  }

  private remountOverlay(): void {
    this.overlayMounted = false;
    setTimeout(() => {
      this.overlayMounted = true;
    }, 0);
  }

  private readDisplayMetrics(): void {
    try {
      const info = display.getDefaultDisplaySync();
      this.density = info.densityDPI / 160;
    } catch (error) {
      this.density = 3;
      const detail = error instanceof Error ? error.message : 'display metrics failed';
      console.error(`FlyNES density failed: ${detail}`);
    }
  }

  /**
   * Android ViewportLayout.compute parity: 4:3 letterboxed to safe area,
   * square-pixels uses native 256:240, integer scale snaps to whole multiples.
   */
  private updateViewport(): void {
    try {
      const info = display.getDefaultDisplaySync();
      const safeWidthPx = Math.max(1, info.width - Math.max(0, this.insetL)
        - Math.max(0, this.insetR));
      const safeHeightPx = Math.max(1, info.height - Math.max(0, this.insetT)
        - Math.max(0, this.insetB));
      const viewport: ViewportSize = computeViewport(safeWidthPx, safeHeightPx,
        this.aspectMode, 256, 240);
      this.viewportW = this.getUIContext().px2vp(viewport.width);
      this.viewportH = this.getUIContext().px2vp(viewport.height);
    } catch (error) {
      this.viewportW = 0;
      this.viewportH = 0;
      const detail = error instanceof Error ? error.message : 'viewport failed';
      console.error(`FlyNES viewport failed: ${detail}`);
    }
  }

  private requestLandscape(context: common.UIAbilityContext): void {
    window.getLastWindow(context).then((win: window.Window) => {
      try {
        win.setPreferredOrientation(window.Orientation.AUTO_ROTATION_LANDSCAPE);
      } catch (error) {
        const detail = error instanceof Error ? error.message : 'orientation failed';
        console.error(`FlyNES landscape preference failed: ${detail}`);
      }
      try {
        const avoid = win.getWindowAvoidArea(window.AvoidAreaType.TYPE_SYSTEM);
        this.insetL = avoid.leftRect.width;
        this.insetR = avoid.rightRect.width;
        this.insetT = avoid.topRect.height;
        this.insetB = avoid.bottomRect.height;
        this.updateViewport();
      } catch (error) {
        const detail = error instanceof Error ? error.message : 'avoid area failed';
        console.error(`FlyNES insets failed: ${detail}`);
      }
    }).catch((error: Object) => {
      const detail = error instanceof Error ? error.message : 'window failed';
      console.error(`FlyNES window failed: ${detail}`);
    });
  }
}
