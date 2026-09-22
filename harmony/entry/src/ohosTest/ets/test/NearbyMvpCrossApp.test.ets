import { describe, it, expect } from '@ohos/hypium';
import { abilityDelegatorRegistry } from '@kit.TestKit';
import { fileIo } from '@kit.CoreFileKit';
import nativeSession, { NearbyMvpStepResult } from 'libentry.so';
import { BuiltinGame, BuiltinGames } from '../../../main/ets/service/BuiltinGames';

function delay(ms: number): Promise<void> {
  return new Promise<void>((resolve: () => void): void => {
    setTimeout(resolve, ms);
  });
}

export default function nearbyMvpCrossAppTest(): void {
  describe('NearbyMvpCrossApp', () => {
    it('real_napi_guest_joins_android_product_host', 0, async () => {
      const parameters = abilityDelegatorRegistry.getArguments().parameters;
      const invite = parameters['-s crossAppInvite'];
      expect(typeof invite === 'string' && invite.length > 0).assertTrue();
      nativeSession.nearbyMvpCancel();
      expect(nativeSession.nearbyMvpJoin(invite)).assertTrue();

      const deadline = Date.now() + 20_000;
      let snapshot = nativeSession.nearbyMvpSnapshot();
      while (snapshot.state !== 3 && snapshot.state !== 4 && Date.now() < deadline) {
        await delay(50);
        snapshot = nativeSession.nearbyMvpSnapshot();
      }
      expect(snapshot.state).assertEqual(3);
      expect(snapshot.reason).assertEqual(0);
      expect(snapshot.transportResult).assertEqual(0);
      expect(snapshot.sessionId.length).assertEqual(32);

      const context = abilityDelegatorRegistry.getAbilityDelegator().getAppContext()
        .createModuleContext('entry');
      const games = BuiltinGames.loadSync(context).all();
      let selected: BuiltinGame | undefined = undefined;
      for (let index = 0; index < games.length; index++) {
        if (games[index].multiplayerEligibility === 'SUPPORTED' &&
          games[index].multiplayerMaxPlayers === 2) {
          selected = games[index];
          break;
        }
      }
      expect(selected !== undefined).assertTrue();
      expect(nativeSession.nearbyMvpSelectRom(
        context.resourceManager.getRawFileContentSync((selected as BuiltinGame).assetPath())))
        .assertTrue();
      expect(nativeSession.nearbyMvpConfirm()).assertTrue();

      const endEpochText = parameters['-s crossAppEndEpochMs'];
      const requestedEndEpoch = typeof endEpochText === 'string' && endEpochText.length > 0
        ? Number.parseInt(endEpochText, 10) : 0;
      const endEpoch = requestedEndEpoch > Date.now() ? requestedEndEpoch : Date.now();
      const runDeadline = Date.now() + Math.max(30_000, endEpoch - Date.now() + 60_000);
      snapshot = nativeSession.nearbyMvpSnapshot();
      while (snapshot.state !== 6 && snapshot.state !== 4 && Date.now() < runDeadline) {
        await delay(10);
        snapshot = nativeSession.nearbyMvpSnapshot();
      }
      expect(snapshot.state).assertEqual(6);
      const frameText = parameters['-s crossAppFrames'];
      const targetFrames = typeof frameText === 'string' && frameText.length > 0
        ? Number.parseInt(frameText, 10) : 600;
      let pcmSamples = 0;
      let step: NearbyMvpStepResult = nativeSession.nearbyMvpStep(0x02);
      let capturedFrame: Uint8Array | undefined = undefined;
      let capturedFrameIndex = -1;
      while ((step.completedFrames < targetFrames || Date.now() < endEpoch) && Date.now() < runDeadline) {
        const buttons = Math.floor(step.completedFrames / 30) % 2 === 0 ? 0x02 : 0;
        step = nativeSession.nearbyMvpStep(buttons);
        pcmSamples += step.pcmSampleCount;
        if (capturedFrame === undefined && step.completedFrames >= targetFrames) {
          capturedFrame = new Uint8Array(step.rgb565);
          capturedFrameIndex = step.frameIndex;
        }
        await delay(step.submitted ? 8 : 1);
      }
      expect(step.completedFrames >= targetFrames).assertTrue();
      expect(Date.now() >= endEpoch).assertTrue();
      expect(capturedFrame !== undefined).assertTrue();
      expect(capturedFrameIndex).assertEqual(targetFrames - 1);
      expect(step.width).assertEqual(256);
      expect(step.height).assertEqual(240);
      expect((capturedFrame as Uint8Array).byteLength).assertEqual(256 * 240 * 2);
      expect(pcmSamples > 0).assertTrue();

      const directory = abilityDelegatorRegistry.getAbilityDelegator().getAppContext().filesDir;
      const path = `${directory}/nearby-cross-guest-session.txt`;
      if (fileIo.accessSync(path)) fileIo.unlinkSync(path);
      const file = fileIo.openSync(path, fileIo.OpenMode.CREATE | fileIo.OpenMode.READ_WRITE);
      try {
        fileIo.writeSync(file.fd, snapshot.sessionId);
        fileIo.fsyncSync(file.fd);
      } finally {
        fileIo.closeSync(file.fd);
      }
      const framePath = `${directory}/nearby-cross-guest-frame.rgb565`;
      if (fileIo.accessSync(framePath)) fileIo.unlinkSync(framePath);
      const frameFile = fileIo.openSync(framePath, fileIo.OpenMode.CREATE | fileIo.OpenMode.READ_WRITE);
      try {
        fileIo.writeSync(frameFile.fd, (capturedFrame as Uint8Array).buffer as ArrayBuffer);
        fileIo.fsyncSync(frameFile.fd);
      } finally {
        fileIo.closeSync(frameFile.fd);
      }
      const playPath = `${directory}/nearby-cross-guest-play.txt`;
      if (fileIo.accessSync(playPath)) fileIo.unlinkSync(playPath);
      const playFile = fileIo.openSync(playPath, fileIo.OpenMode.CREATE | fileIo.OpenMode.READ_WRITE);
      try {
        fileIo.writeSync(playFile.fd, `${step.completedFrames},${capturedFrameIndex},${pcmSamples}`);
        fileIo.fsyncSync(playFile.fd);
      } finally {
        fileIo.closeSync(playFile.fd);
      }
      await delay(3_000);
    });
  });
}
