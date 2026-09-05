import { UIAbility } from '@kit.AbilityKit';
import { window } from '@kit.ArkUI';

export default class EntryAbility extends UIAbility {
  onWindowStageCreate(windowStage: window.WindowStage): void {
    try {
      windowStage.getMainWindowSync()
        .setPreferredOrientation(window.Orientation.AUTO_ROTATION_LANDSCAPE);
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
