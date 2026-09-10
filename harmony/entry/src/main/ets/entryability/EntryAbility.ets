import { AbilityConstant, UIAbility, Want } from '@kit.AbilityKit';
import { window } from '@kit.ArkUI';

export default class EntryAbility extends UIAbility {
  onCreate(want: Want, _launchParam: AbilityConstant.LaunchParam): void {
    if (!this.context.applicationInfo.debug) {
      return;
    }
    const rawScale: string | undefined = want.parameters?.['flynes.test.fontSizeScale'] as
      string | undefined;
    if (rawScale === undefined) {
      return;
    }
    const scale: number = Number(rawScale);
    if (scale === 1 || scale === 1.5 || scale === 2) {
      this.context.getApplicationContext().setFontSizeScale(scale);
      console.info(`FlyNES test fontSizeScale applied=${scale}`);
    }
  }

  onWindowStageCreate(windowStage: window.WindowStage): void {
    console.info(`FlyNES UI configuration fontSizeScale=${this.context.config.fontSizeScale ?? -1}`);
    try {
      const win = windowStage.getMainWindowSync();
      win.setPreferredOrientation(window.Orientation.AUTO_ROTATION_LANDSCAPE);
      // Android parity: Theme.FlyNES sets windowFullscreen=true, so the app draws
      // edge-to-edge with no system bars. Without this the page is laid out inside
      // the bottom navigation-bar avoid area and a white strip shows in game.
      win.setWindowLayoutFullScreen(true);
      win.setWindowSystemBarEnable([]);
    } catch (error) {
      const detail = error instanceof Error ? error.message : 'orientation failed';
      console.error(`FlyNES landscape preference failed: ${detail}`);
    }
    windowStage.loadContent('pages/GameCenter', (error) => {
      if (error.code !== 0) {
        console.error(`FlyNES Game Center failed to load: ${error.code}`);
      }
    });
  }
}
