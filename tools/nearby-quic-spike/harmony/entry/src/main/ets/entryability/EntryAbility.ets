import { UIAbility, Want } from '@kit.AbilityKit';
import { window } from '@kit.ArkUI';
import { hilog } from '@kit.PerformanceAnalysisKit';
import { probeLaunchGate } from '../ProbeLaunchGate';

export default class EntryAbility extends UIAbility {
  private acceptWant(want: Want): void {
    const parameters = want.parameters;
    if (!parameters || parameters.runProbe !== true) { return; }
    const valid = typeof parameters.bind === 'string' && typeof parameters.peer === 'string' &&
      typeof parameters.pin === 'string';
    const decision = probeLaunchGate.request(parameters.runProbe === true, valid);
    if (decision.error.length > 0) {
      AppStorage.setOrCreate('probeRequestError', decision.error);
      hilog.error(0x0000, 'NearbyQuicProbe', '%{public}s', decision.error);
      return;
    }
    if (decision.id > 0) {
      AppStorage.setOrCreate('probeRequestError', '');
      AppStorage.setOrCreate('probeBind', parameters.bind);
      AppStorage.setOrCreate('probePeer', parameters.peer);
      AppStorage.setOrCreate('probePin', parameters.pin);
      AppStorage.setOrCreate('probeRequest', decision.id);
    }
  }

  private beginLaunch(want: Want): void {
    probeLaunchGate.beginLaunch();
    AppStorage.setOrCreate('probeRequest', 0);
    AppStorage.setOrCreate('probeRequestError', '');
    AppStorage.setOrCreate('probeBusy', probeLaunchGate.isRunning());
    this.acceptWant(want);
  }
  onCreate(want: Want): void { this.beginLaunch(want); }
  onNewWant(want: Want): void { this.beginLaunch(want); }
  onWindowStageCreate(stage: window.WindowStage): void {
    try {
      stage.loadContent('pages/Index', (error) => {
        if (error.code !== 0) { hilog.error(0x0000, 'NearbyQuicProbe', 'ERROR page load %{public}d', error.code); }
      });
    } catch (error) {
      hilog.error(0x0000, 'NearbyQuicProbe', 'ERROR cannot load probe page');
    }
  }
}
