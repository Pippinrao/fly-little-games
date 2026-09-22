import { describe, it, expect } from '@ohos/hypium';
import { Want } from '@kit.AbilityKit';
import { abilityDelegatorRegistry } from '@kit.TestKit';
import { Driver, ON, Component } from '@ohos.UiTest';
import { parseTestFontSizeScale, resolveTestFontSizeScale } from '../../../main/ets/ui/NearbyTestFontScale';

async function openPage(page: string, scale: string): Promise<Driver> {
  const want: Want = {
    bundleName: 'com.flynes.emu',
    abilityName: 'EntryAbility',
    parameters: { 'flynes.test.page': page, 'flynes.test.fontSizeScale': scale }
  };
  await abilityDelegatorRegistry.getAbilityDelegator().startAbility(want);
  const driver: Driver = Driver.create();
  await driver.waitForIdle(300, 4000);
  await driver.delayMs(800);
  return driver;
}

async function control(driver: Driver, id: string): Promise<Component> {
  const component: Component = await driver.waitForComponent(ON.id(id), 5000);
  if (!component) throw new Error(`Missing visible control: ${id}`);
  return component;
}

async function assertFits(driver: Driver, id: string, tap: boolean = true): Promise<void> {
  const page = await (await control(driver, 'nearby_page')).getBounds();
  const item = await (await control(driver, id)).getBounds();
  expect(item.left >= page.left && item.right <= page.right).assertTrue();
  expect(item.top >= page.top && item.bottom <= page.bottom).assertTrue();
  if (tap) expect(px2vp(item.bottom - item.top) >= 47.5).assertTrue();
}

function localized(name: string): string {
  return abilityDelegatorRegistry.getAbilityDelegator().getAppContext()
    .createModuleContext('entry').resourceManager.getStringByNameSync(name);
}

async function capture(driver: Driver, label: string): Promise<void> {
  expect(await driver.waitForIdle(200, 1500)).assertTrue();
  // Route transitions render asynchronously after the UI thread becomes idle.
  await driver.delayMs(350);
  const directory = abilityDelegatorRegistry.getAbilityDelegator().getAppContext().filesDir;
  expect(await driver.screenCap(`${directory}/flynes-ux-${label}.png`)).assertTrue();
}

export default function nearbyUxRestorationTest() {
  describe('NearbyUxRestoration', () => {
    for (const scale of ['1', '1.3', '2']) {
      it(`single_screen_landscape_${scale}`, 0, async () => {
        const driver: Driver = await openPage('nearby_friends', scale);
        for (const id of ['nearby_action_create', 'nearby_action_enterCode',
          'nearby_action_scanQr', 'nearby_secondary_action']) {
          await assertFits(driver, id);
        }
        await capture(driver, `entry-${scale}`);
        // Unsupported entry stays on the same page; it does not start the legacy code workflow.
        await (await control(driver, 'nearby_action_enterCode')).click();
        await assertFits(driver, 'nearby_action_scanQr');
        await (await control(driver, 'nearby_action_create')).click();
        await assertFits(driver, 'nearby_action_scanQr');
        await (await control(driver, 'nearby_action_scanQr')).click();
        await assertFits(driver, 'nearby_action_cancel');
        await assertFits(driver, 'nearby_mvp_scan_action');
        await assertFits(driver, 'nearby_pairing_status', false);
        await capture(driver, `scan-${scale}`);
        // Do not launch the camera or change backend state in this layout test.
        await (await control(driver, 'nearby_action_cancel')).click();
        await (await control(driver, 'nearby_tab_friends')).click();
        expect(await driver.waitForIdle(100, 1500)).assertTrue();
        await driver.delayMs(150);
        expect(await (await control(driver, 'nearby_secondary_action')).getText())
          .assertEqual(localized('nearby_friends_manage'));
        await (await control(driver, 'nearby_secondary_action')).click();
        for (const id of ['nearby_friends_rename', 'nearby_friends_delete',
          'nearby_friends_block', 'nearby_friends_identity_reset']) {
          await assertFits(driver, id);
        }
        await capture(driver, `manage-${scale}`);
        await (await control(driver, 'nearby_friends_rename')).click();
        await assertFits(driver, 'nearby_friends_identity_reset');
      });
    }

    for (const scale of ['1', '1.3', '2']) {
    it(`lobby_has_fixed_summary_and_paged_details_${scale}`, 0, async () => {
      const driver: Driver = await openPage('nearby_lobby', scale);
      for (const id of ['nearby_lobby_rom_identity', 'nearby_lobby_network_owner', 'nearby_lobby_seat']) {
        await assertFits(driver, id, false);
      }
      await assertFits(driver, 'nearby_lobby_confirm');
      await assertFits(driver, 'nearby_lobby_details');
      await capture(driver, `lobby-${scale}`);
      await (await control(driver, 'nearby_lobby_details')).click();
      expect(await driver.waitForIdle(100, 1500)).assertTrue();
      await driver.delayMs(150);
      expect(await (await control(driver, 'nearby_lobby_details')).getText())
        .assertEqual(localized('nearby_action_cancel'));
      await assertFits(driver, 'nearby_lobby_friend_name', false);
      await (await control(driver, 'nearby_details_next')).click();
      await assertFits(driver, 'nearby_lobby_identity_fingerprint', false);
      await assertFits(driver, 'nearby_lobby_details');
      await assertFits(driver, 'nearby_details_previous');
      await assertFits(driver, 'nearby_details_next');
      await assertFits(driver, 'nearby_lobby_confirm');
      await capture(driver, `details-${scale}`);
      await (await control(driver, 'nearby_details_previous')).click();
      await assertFits(driver, 'nearby_lobby_friend_name', false);
    });
    }
  });

  describe('NearbyWantParser', () => {
    it('debug_want_accepts_supported_scales_only', 0, () => {
      expect(parseTestFontSizeScale('1.3').accepted).assertTrue();
      expect(parseTestFontSizeScale('1.3').scale).assertEqual(1.3);
      expect(parseTestFontSizeScale('1.2').accepted).assertFalse();
      expect(resolveTestFontSizeScale('1.3', false).accepted).assertFalse();
      expect(resolveTestFontSizeScale('1.3', true).accepted).assertTrue();
    });
  });
}
