import { AbilityConstant, UIAbility, Want } from '@kit.AbilityKit';
import { hilog } from '@kit.PerformanceAnalysisKit';
import { window } from '@kit.ArkUI';
import { resolveTestFontSizeScale } from '../ui/NearbyTestFontScale';

export default class EntryAbility extends UIAbility {
  private testPage: string = 'pages/GameCenter';
  private stage: window.WindowStage | undefined = undefined;

  onCreate(want: Want, _launchParam: AbilityConstant.LaunchParam): void {
    this.applyDebugWant(want);
  }

  onNewWant(want: Want, _launchParam: AbilityConstant.LaunchParam): void {
    if (this.applyDebugWant(want) && this.stage !== undefined) {
      this.stage.loadContent(this.testPage);
    }
  }

  private applyDebugWant(want: Want): boolean {
    const debug = this.context.applicationInfo.debug;
    const rawScale: string | undefined = want.parameters?.['flynes.test.fontSizeScale'] as
      string | undefined;
    const parsed = resolveTestFontSizeScale(rawScale, debug);
    if (parsed.accepted) {
      this.context.getApplicationContext().setFontSizeScale(parsed.scale);
      console.info(`FlyNES test fontSizeScale applied=${parsed.scale}`);
    } else if (parsed.rejection.length > 0) {
      console.info(parsed.rejection);
    }
    if (!debug) {
      return false;
    }
    const page: string | undefined = want.parameters?.['flynes.test.page'] as string | undefined;
    if (page === 'nearby_friends') {
      this.testPage = 'pages/NearbyFriends';
    } else if (page === 'nearby_pairing') {
      this.testPage = 'pages/NearbyPairing';
    } else if (page === 'nearby_lobby') {
      this.testPage = 'pages/NearbyLobby';
    } else {
      return false;
    }
    return true;
  }

  onWindowStageCreate(windowStage: window.WindowStage): void {
    this.stage = windowStage;
    console.info(`FlyNES UI configuration fontSizeScale=${this.context.config.fontSizeScale ?? -1}`);
    try {
      const win = windowStage.getMainWindowSync();
      win.setPreferredOrientation(window.Orientation.AUTO_ROTATION_LANDSCAPE);
      win.setWindowLayoutFullScreen(true);
      win.setWindowSystemBarEnable([]);
    } catch (error) {
      const detail = error instanceof Error ? error.message : 'orientation failed';
      console.error(`FlyNES landscape preference failed: ${detail}`);
    }
    windowStage.loadContent(this.testPage, (error) => {
      if (error.code !== 0) {
        console.error(`FlyNES content failed to load: ${error.code}`);
      }
    });
  }

  onForeground(): void { hilog.info(0, 'FlyNesNearby', 'event=ability_foreground'); }

  onBackground(): void { hilog.info(0, 'FlyNesNearby', 'event=ability_background'); }

  onWindowStageDestroy(): void {
    this.stage = undefined;
  }
}
