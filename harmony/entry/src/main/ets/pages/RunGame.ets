import { common } from '@kit.AbilityKit';
import { display, router, window } from '@kit.ArkUI';
import { image } from '@kit.ImageKit';
import nativePlay from 'libentry.so';
import { GamepadOverlay } from '../overlay/GamepadOverlay';
import { PlayService } from '../service/PlayService';

const DIRECTION_FIXED_JOYSTICK = 1;
const DEFAULT_DEAD_ZONE = 0.18;

@Entry
@Component
struct RunGame {
  private readonly play: PlayService = new PlayService();
  private timerId: number = -1;
  private started: boolean = false;
  @State private canonicalId: string = '';
  @State private frame: image.PixelMap | undefined = undefined;
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
  @State private pressedBits: number = 0;
  @State private checkpointFailed: boolean = false;
  @State private status: string = '';
  @State private directionMode: number = DIRECTION_FIXED_JOYSTICK;
  @State private deadZone: number = DEFAULT_DEAD_ZONE;

  aboutToAppear(): void {
    const params = router.getParams() as Record<string, Object>;
    if (params !== undefined && params['canonicalId'] !== undefined) {
      this.canonicalId = params['canonicalId'] as string;
    }
    this.pauseIds = nativePlay.pauseCommands();
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
    if (this.started && !this.paused) {
      this.startLoop();
    }
  }

  onPageHide(): void {
    this.stopLoop();
  }

  aboutToDisappear(): void {
    this.stopLoop();
    this.play.close();
    this.frame?.release();
    this.frame = undefined;
    this.started = false;
  }

  build() {
    Stack() {
      if (this.frame === undefined) {
        Column() {
          Text(this.status.length > 0 ? this.status : 'Starting')
            .fontColor('#A8B0BD')
        }
        .width('100%')
        .height('100%')
        .justifyContent(FlexAlign.Center)
        .backgroundColor('#000000')
      } else {
        Image(this.frame)
          .width('100%')
          .height('100%')
          .objectFit(ImageFit.Contain)
          .backgroundColor('#000000')
          .alt($r('app.string.play_screen_alt'))
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
        .onClick(() => {
          this.resumePlay();
        })
      Column({ space: 12 }) {
        Text($r('app.string.pause_eyebrow'))
          .fontSize(12)
          .fontColor('#FF6B5E')
          .fontWeight(FontWeight.Bold)
          .width('100%')
        Text($r('app.string.pause_title'))
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
            .height(id === 'resume' ? 52 : 48)
            .fontSize(15)
            .fontColor(id === 'resume' ? '#121316' : '#F4EFE6')
            .backgroundColor(id === 'resume' ? '#FF6B5E' : '#25282F')
            .borderRadius(14)
            .onClick(() => {
              this.handlePauseCommand(id);
            })
        }, (id: string) => id)
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
      .onClick(() => {
      })
    }
    .width('100%')
    .height('100%')
  }

  private async start(context: common.UIAbilityContext): Promise<void> {
    try {
      await this.play.open(context, this.canonicalId);
      this.started = true;
      this.status = '';
      this.startLoop();
    } catch (error) {
      this.status = error instanceof Error ? error.message : 'Failed to start play session';
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
      const stepped = this.play.stepFrame();
      const next = this.play.createFrame(stepped.rgb565);
      const old = this.frame;
      this.frame = next;
      old?.release();
    } catch (error) {
      this.stopLoop();
      this.status = error instanceof Error ? error.message : 'Frame step failed';
    }
  }

  private handleOverlayButtons(bits: number): void {
    this.pressedBits = bits;
    this.play.setButtons(bits);
  }

  private openPause(): void {
    if (this.drawerOpen) {
      return;
    }
    if (!this.paused) {
      this.play.setButtons(0);
      this.pressedBits = 0;
      this.stopLoop();
      this.play.pauseOutput();
      this.checkpointFailed = false;
      try {
        this.play.saveCheckpoint();
      } catch (error) {
        this.checkpointFailed = true;
        this.status = error instanceof Error ? error.message : 'checkpoint failed';
      }
    }
    this.paused = true;
    this.drawerOpen = true;
  }

  private resumePlay(): void {
    this.drawerOpen = false;
    if (!this.paused) {
      return;
    }
    this.paused = false;
    this.checkpointFailed = false;
    this.play.resumeOutput();
    this.startLoop();
  }

  private handlePauseCommand(id: string): void {
    if (id === 'resume') {
      this.resumePlay();
      return;
    }
    if (id === 'game_center') {
      this.drawerOpen = false;
      this.stopLoop();
      this.play.close();
      this.started = false;
      void router.replaceUrl({ url: 'pages/GameCenter' });
      return;
    }
    if (id === 'settings') {
      this.drawerOpen = false;
      this.paused = true;
      void router.pushUrl({ url: 'pages/Settings' });
    }
  }

  private pauseLabel(id: string): Resource {
    if (id === 'resume') {
      return $r('app.string.pause_resume');
    }
    if (id === 'game_center') {
      return $r('app.string.pause_game_center');
    }
    return $r('app.string.pause_settings');
  }

  private ensureApp(context: common.UIAbilityContext): void {
    nativePlay.appOpen(context.filesDir, context.cacheDir);
  }

  private loadPersistedLayout(): void {
    try {
      this.layoutUtf8 = nativePlay.controlLayoutGet();
      const settings = nativePlay.settingsGet();
      this.directionMode = Math.max(0, Math.min(2, settings.directionMode - 1));
      this.deadZone = settings.deadZone;
    } catch (error) {
      this.layoutUtf8 = nativePlay.controlLayoutRecommended();
      const detail = error instanceof Error ? error.message : 'layout get failed';
      console.error(`FlyNES layout load failed: ${detail}`);
    }
    this.remountOverlay();
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
