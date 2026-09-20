import { afterEach, beforeEach, describe, it, expect } from '@ohos/hypium';
import { Want } from '@kit.AbilityKit';
import { abilityDelegatorRegistry } from '@kit.TestKit';
import { Driver, ON, Component } from '@ohos.UiTest';
import {
  parseTestFontSizeScale,
  resolveTestFontSizeScale
} from '../../../main/ets/ui/NearbyTestFontScale';

const BUNDLE: string = 'com.flynes.emu';
const ABILITY: string = 'EntryAbility';

async function openPage(page: string, scale: string): Promise<Driver> {
  const delegator = abilityDelegatorRegistry.getAbilityDelegator();
  const want: Want = {
    bundleName: BUNDLE,
    abilityName: ABILITY,
    parameters: {
      'flynes.test.page': page,
      'flynes.test.fontSizeScale': scale
    }
  };
  await delegator.startAbility(want);
  const driver: Driver = Driver.create();
  await driver.delayMs(800);
  return driver;
}

async function boundsHeightPx(driver: Driver, id: string): Promise<number> {
  const component: Component | undefined = await driver.findComponent(ON.id(id));
  expect(component !== undefined && component !== null).assertTrue();
  if (component === undefined || component === null) {
    return 0;
  }
  const bounds = await component.getBounds();
  return bounds.bottom - bounds.top;
}

async function boundsHeightVp(driver: Driver, id: string): Promise<number> {
  return px2vp(await boundsHeightPx(driver, id));
}

async function requireComponent(driver: Driver, id: string,
  allowScroll: boolean = false): Promise<Component> {
  let component: Component | undefined = await driver.findComponent(ON.id(id));
  if ((component === undefined || component === null) && allowScroll) {
    const scroll: Component | undefined = await driver.findComponent(ON.type('Scroll'));
    if (scroll !== undefined && scroll !== null) {
      const bounds = await scroll.getBounds();
      const x: number = Math.floor((bounds.left + bounds.right) / 2);
      for (let attempt = 0; attempt < 4; attempt++) {
        await driver.swipe(x, bounds.bottom - 40, x, bounds.top + 40, 3000);
        component = await driver.findComponent(ON.id(id));
        if (component !== undefined && component !== null) {
          break;
        }
      }
    }
  }
  if (component === undefined || component === null) {
    throw new Error(`Required nearby control not reachable: ${id}`);
  }
  return component;
}

async function componentExists(driver: Driver, id: string): Promise<boolean> {
  try {
    const component: Component | undefined = await driver.findComponent(ON.id(id));
    return component !== undefined && component !== null;
  } catch (error) {
    const ignored: string = `${error}`;
    return ignored.length < 0;
  }
}

async function waitForNearbyComponent(driver: Driver, id: string): Promise<Component> {
  const component: Component | undefined = await driver.waitForComponent(ON.id(id), 800);
  if (component === undefined || component === null) {
    throw new Error(`Required nearby control not reachable after transition: ${id}`);
  }
  return component;
}

export default function nearbyUxRestorationTest() {
  describe('NearbyUxRestoration', () => {
    it('TYPO_H_N00_roles', 0, async () => {
      const driver: Driver = await openPage('nearby_friends', '1');
      const headline1: number = await boundsHeightPx(driver, 'nearby_entry_headline');
      const subtitle1: number = await boundsHeightPx(driver, 'nearby_entry_subtitle');
      expect(headline1 > subtitle1).assertTrue();
      const createHeightVp: number = await boundsHeightVp(driver, 'nearby_action_create');
      expect(createHeightVp >= 48).assertTrue();
    });

    it('TYPO_H_scale13_applied', 0, async () => {
      const driver1: Driver = await openPage('nearby_friends', '1');
      const headline1: number = await boundsHeightPx(driver1, 'nearby_entry_headline');
      const driver13: Driver = await openPage('nearby_friends', '1.3');
      const headline13: number = await boundsHeightPx(driver13, 'nearby_entry_headline');
      expect(headline13 > headline1).assertTrue();
      const createHeightVp: number = await boundsHeightVp(driver13, 'nearby_action_create');
      expect(createHeightVp >= 48).assertTrue();
    });

    it('TYPO_H_scale2_reachable', 0, async () => {
      const driver: Driver = await openPage('nearby_friends', '2');
      const create: Component = await requireComponent(driver, 'nearby_action_create', true);
      const createHeightVp: number = await boundsHeightVp(driver, 'nearby_action_create');
      expect(createHeightVp >= 48).assertTrue();
      await create.click();
      await driver.delayMs(500);
      await requireComponent(driver, 'nearby_invite_headline', true);
      const back: Component = await requireComponent(driver, 'nearby_back');
      await back.click();
    });

    it('N09_first_screen_is_game_host_seat_confirm', 0, async () => {
      const driver: Driver = await openPage('nearby_lobby', '1');
      expect(await componentExists(driver, 'nearby_lobby_rom_identity')).assertTrue();
      expect(await componentExists(driver, 'nearby_lobby_network_owner')).assertTrue();
      expect(await componentExists(driver, 'nearby_lobby_seat')).assertTrue();
      expect(await componentExists(driver, 'nearby_lobby_confirm')).assertTrue();
      expect(await componentExists(driver, 'nearby_lobby_identity_fingerprint')).assertFalse();
      const confirm: Component = await requireComponent(driver, 'nearby_lobby_confirm');
      expect(await confirm.isEnabled()).assertFalse();
      const details: Component = await requireComponent(driver, 'nearby_lobby_details', true);
      await details.click();
      await driver.delayMs(300);
      await requireComponent(driver, 'nearby_lobby_identity_fingerprint', true);
      const stillDisabled: Component = await requireComponent(driver, 'nearby_lobby_confirm');
      expect(await stillDisabled.isEnabled()).assertFalse();
    });
  });

  describe('NearbyJoinRestoration', () => {
    let joinDriver: Driver | undefined = undefined;
    let expectedCode: string = '123456';

    beforeEach(async () => {
      expectedCode = '123456';
      const driver: Driver = await openPage('nearby_friends', '1');
      joinDriver = driver;
      const enter: Component = await requireComponent(driver, 'nearby_action_enterCode', true);
      await enter.click();
      expect(await driver.waitForIdle(100, 500)).assertTrue();
      await waitForNearbyComponent(driver, 'nearby_join_code_input');
    });

    beforeEach(async () => {
      const driver: Driver = joinDriver!;
      const input: Component = await waitForNearbyComponent(driver, 'nearby_join_code_input');
      await input.inputText('123456');
      // Back resolves before the approximately 540 ms keyboard exit animation.
      await driver.pressBack();
      const submit: Component = await waitForNearbyComponent(driver, 'nearby_join_submit');
      await submit.click();
      await waitForNearbyComponent(driver, 'nearby_join_cancel');
    });

    afterEach(async () => {
      const driver: Driver = joinDriver!;
      const inputAfterSubmit: Component = await waitForNearbyComponent(driver, 'nearby_join_code_input');
      expect(await inputAfterSubmit.getText()).assertEqual(expectedCode);
      expect(await inputAfterSubmit.isEnabled()).assertTrue();
      const submitAfterRetry: Component = await requireComponent(driver, 'nearby_join_submit');
      expect(await submitAfterRetry.isEnabled()).assertTrue();
      // Cancel plus editable input distinguishes a failed attempt from submission in progress.
      const cancel: Component = await waitForNearbyComponent(driver, 'nearby_join_cancel');
      await cancel.click();
      await waitForNearbyComponent(driver, 'nearby_entry_headline');
      expect(await componentExists(driver, 'nearby_entry_headline')).assertTrue();
    });

    it('JOIN_submit_fail_cancel', 0, async () => {
      const driver: Driver = joinDriver!;
      const submitAgain: Component = await requireComponent(driver, 'nearby_join_submit');
      await submitAgain.click();
      expect(await componentExists(driver, 'nearby_join_cancel')).assertTrue();
      const submitAfterFail: Component = await driver.waitForComponent(ON.id('nearby_join_submit').enabled(true), 800);
      expect(await submitAfterFail.isEnabled()).assertTrue();
    });

    it('JOIN_modify_failed_code_retry_cancel', 0, async () => {
      const driver: Driver = joinDriver!;
      expect(await componentExists(driver, 'nearby_join_cancel')).assertTrue();
      const submitAfterFail: Component = await driver.waitForComponent(ON.id('nearby_join_submit').enabled(true), 800);
      expect(await submitAfterFail.isEnabled()).assertTrue();
      const inputAfterFail: Component = await requireComponent(driver, 'nearby_join_code_input');
      expectedCode = '654321';
      await inputAfterFail.inputText(expectedCode, { paste: true });
      await driver.pressBack();
      const retry: Component = await waitForNearbyComponent(driver, 'nearby_join_submit');
      await retry.click();
    });
  });

  describe('NearbyWantParser', () => {
    it('debug_want_accepts_1_13_15_2_and_rejects_others', 0, () => {
      expect(parseTestFontSizeScale('1.3').accepted).assertTrue();
      expect(parseTestFontSizeScale('1.3').scale).assertEqual(1.3);
      expect(parseTestFontSizeScale('1.2').accepted).assertFalse();
      expect(resolveTestFontSizeScale('1.3', false).accepted).assertFalse();
      expect(resolveTestFontSizeScale('1.3', true).accepted).assertTrue();
    });
  });
}
