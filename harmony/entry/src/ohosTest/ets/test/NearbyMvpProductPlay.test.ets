import { describe, it, expect } from '@ohos/hypium';
import { abilityDelegatorRegistry } from '@kit.TestKit';
import { display, window } from '@kit.ArkUI';
import { Driver, ON } from '@ohos.UiTest';
import nativePlay from 'libentry.so';
import { BuiltinGames } from '../../../main/ets/service/BuiltinGames';
import { PlayService, RomLaunchLocator } from '../../../main/ets/service/PlayService';

async function delay(ms: number): Promise<void> {
  await new Promise<void>((resolve) => { setTimeout(resolve, ms); });
}

export default function nearbyMvpProductPlayTest(): void {
  describe('NearbyMvpProductPlay', () => {
    it('guest_reuses_RunGame_controls_renderer_and_audio', 0, async () => {
      const args = abilityDelegatorRegistry.getArguments().parameters;
      const delegator = abilityDelegatorRegistry.getAbilityDelegator();
      await delegator.startAbility({ bundleName: 'com.flynes.emu', abilityName: 'EntryAbility' });
      await delay(800);
      const context = (await delegator.getCurrentTopAbility()).context;
      const win = await window.getLastWindow(context);
      nativePlay.appOpen(context.filesDir, context.cacheDir);
      const locator = new RomLaunchLocator();
      const query = args['-s localGameQuery'];
      if (typeof query === 'string' && query.length > 0) {
        const row = nativePlay.catalogSnapshot().find((row) => row.originalFilename === query ||
          row.sourceRelativePath === query || row.sourceRelativePath.endsWith('/' + query));
        expect(row !== undefined).assertTrue();
        locator.canonicalId = row!.canonicalId;
        locator.sourceUuidHex = row!.sourceUuidHex;
        locator.sourceRelativePath = row!.sourceRelativePath;
        locator.packageFormat = row!.packageFormat;
        locator.zipEntryName = row!.originalFilename;
      } else {
        const game = BuiltinGames.loadSync(context).all().find((game) =>
          game.multiplayerEligibility === 'SUPPORTED' && game.multiplayerMaxPlayers === 2);
        expect(game !== undefined).assertTrue();
        locator.canonicalId = game!.canonicalId;
      }
      const rom = await new PlayService().readRom(context, locator);
      const invite = args['-s crossAppInvite'];
      expect(typeof invite === 'string').assertTrue();
      nativePlay.nearbyMvpCancel();
      expect(nativePlay.nearbyMvpJoin(invite)).assertTrue();
      const deadline = Date.now() + 20000;
      while (nativePlay.nearbyMvpSnapshot().state !== 3 && Date.now() < deadline) await delay(20);
      expect(nativePlay.nearbyMvpSnapshot().state).assertEqual(3);
      expect(nativePlay.nearbyMvpSelectRom(rom)).assertTrue();
      expect(nativePlay.nearbyMvpConfirm()).assertTrue();
      while (nativePlay.nearbyMvpSnapshot().state !== 6 && Date.now() < deadline) await delay(20);
      expect(nativePlay.nearbyMvpSnapshot().state).assertEqual(6);
      await win.getUIContext().getRouter().replaceUrl({
        url: 'pages/RunGame', params: { nearby: true, canonicalId: locator.canonicalId }
      });
      const driver = Driver.create();
      await delay(1200);
      expect(await driver.findComponent(ON.type('Canvas')) !== null).assertTrue();
      const rect = win.getWindowProperties().windowRect;
      const avoid = win.getWindowAvoidArea(window.AvoidAreaType.TYPE_SYSTEM);
      const settings = nativePlay.settingsGet();
      const hit = nativePlay.hitMapFromLayout(rect.width, rect.height,
        display.getDefaultDisplaySync().densityDPI / 160,
        avoid.leftRect.width, avoid.rightRect.width, avoid.topRect.height, avoid.bottomRect.height,
        nativePlay.controlLayoutGet(), Math.max(0, Math.min(2, settings.directionMode - 1)), settings.deadZone);
      const names = ['START', 'SELECT', 'A', 'B'];
      const bits = [8, 4, 1, 2];
      for (let index = 0; index < names.length; index++) {
        const target = hit.controls.find((item) => item.control === names[index]);
        expect(target !== undefined).assertTrue();
        let observed = false;
        const timer = setInterval(() => {
          observed = observed || (nativePlay.playStep().appliedButtons & bits[index]) !== 0;
        }, 10);
        try { await driver.longClick(Math.round(rect.left + target!.centerX), Math.round(rect.top + target!.centerY)); }
        finally { clearInterval(timer); }
        expect(observed).assertTrue();
        await delay(100);
        expect(nativePlay.playStep().appliedButtons).assertEqual(0);
      }
      const status = nativePlay.playRuntimeStatus();
      expect(status.running).assertTrue();
      expect(status.error).assertEqual('');
      expect(status.audioStarted).assertTrue();
      expect(status.audioConsumedSamples > 0).assertTrue();
      console.info(`FlyNesNearby product_audio consumed=${status.audioConsumedSamples} callbacks=${status.audioCallbackCount}`);
      // Exercise P2 movement after the host has passed the game's opening screens.
      await delay(8000);
      const right = hit.controls.find((item) => item.control === 'RIGHT');
      expect(right !== undefined).assertTrue();
      let moved = false;
      const movementTimer = setInterval(() => {
        moved = moved || (nativePlay.playStep().appliedButtons & 128) !== 0;
      }, 10);
      try { await driver.longClick(Math.round(rect.left + right!.centerX), Math.round(rect.top + right!.centerY)); }
      finally { clearInterval(movementTimer); }
      expect(moved).assertTrue();
      const hold = args['-s playHoldMs'];
      await delay(typeof hold === 'string' ? Number.parseInt(hold, 10) : 1000);
      let sawPause = false;
      const lobbyDeadline = Date.now() + 45000;
      while (nativePlay.nearbyMvpSnapshot().state === 6 && Date.now() < lobbyDeadline) {
        sawPause = sawPause || nativePlay.nearbyMvpSnapshot().paused;
        await delay(20);
      }
      expect(sawPause).assertTrue();
      // RunGame itself navigates back; the guest lobby loads the host's new catalog identity.
      const nextDeadline = Date.now() + 30000;
      while (nativePlay.nearbyMvpSnapshot().localConfigured === 0 && Date.now() < nextDeadline)
        await delay(50);
      expect(nativePlay.nearbyMvpSnapshot().localConfigured).assertEqual(1);
      expect(nativePlay.nearbyMvpSnapshot().peerGameKey.length > 0).assertTrue();
      await (await driver.findComponent(ON.id('nearby_lobby_confirm'))).click();
      while (nativePlay.nearbyMvpSnapshot().state !== 6 && Date.now() < nextDeadline) await delay(50);
      expect(nativePlay.nearbyMvpSnapshot().state).assertEqual(6);
      await delay(3000);
      expect(nativePlay.nearbyMvpSnapshot().completedFrames > 30).assertTrue();
      expect(nativePlay.playRuntimeStatus().audioConsumedSamples > 0).assertTrue();
      console.info('FlyNesNearby product_lobby_switch second_game=PASS');
      if (typeof query === 'string' && query.length > 0) {
        const thirdDeadline = Date.now() + 30000;
        while (nativePlay.nearbyMvpSnapshot().state === 6 && Date.now() < thirdDeadline) await delay(20);
        while (nativePlay.nearbyMvpSnapshot().localConfigured === 0 && Date.now() < thirdDeadline)
          await delay(50);
        expect(nativePlay.nearbyMvpSnapshot().localConfigured).assertEqual(1);
        expect(nativePlay.nearbyMvpSnapshot().peerGameKey).assertEqual(locator.canonicalId);
        await (await driver.findComponent(ON.id('nearby_lobby_confirm'))).click();
        while (nativePlay.nearbyMvpSnapshot().state !== 6 && Date.now() < thirdDeadline) await delay(50);
        expect(nativePlay.nearbyMvpSnapshot().state).assertEqual(6);
        await delay(3000);
        expect(nativePlay.nearbyMvpSnapshot().completedFrames > 30).assertTrue();
        console.info('FlyNesNearby product_lobby_switch third_game=PASS');
      }
    });
  });
}
