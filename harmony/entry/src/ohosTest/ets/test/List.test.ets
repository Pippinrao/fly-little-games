import smokeTest from './Smoke.test';
import { abilityDelegatorRegistry } from '@kit.TestKit';
import controlLayoutWarningsTest from './ControlLayoutWarnings.test';
import licenseModelTest from './LicenseModel.test';
import pauseParamsTest from './PauseParams.test';
import settingsHelpersTest from './SettingsHelpers.test';
import checkpointStoreTest from './CheckpointStore.test';
import catalogProductServiceTest from './CatalogProductService.test';
import nearbyServiceTest from './NearbyService.test';
import nearbyUxRestorationTest from './NearbyUxRestoration.test';
import nearbyMvpScanFlowTest from './NearbyMvpScanFlow.test';
import nearbyMvpCrossAppTest from './NearbyMvpCrossApp.test';
import nearbyMvpProductPlayTest from './NearbyMvpProductPlay.test';
import builtinPlaySmokeTest from './BuiltinPlaySmoke.test';
import builtinGameplayAcceptanceTest from './BuiltinGameplayAcceptance.test';

export default function testsuite() {
  if (abilityDelegatorRegistry.getArguments().parameters['-s nearbyProductPlayOnly'] === 'true') {
    nearbyMvpProductPlayTest();
    return;
  }
  if (abilityDelegatorRegistry.getArguments().parameters['-s nearbyServiceOnly'] === 'true') {
    nearbyServiceTest();
    return;
  }
  if (abilityDelegatorRegistry.getArguments().parameters['-s nearbyMvpOnly'] === 'true') {
    nearbyMvpScanFlowTest();
    return;
  }
  if (abilityDelegatorRegistry.getArguments().parameters['-s nearbyCrossOnly'] === 'true') {
    nearbyMvpCrossAppTest();
    return;
  }
  // Targeted physical-device UX checks must not initialize unrelated ROM fixtures
  // before Hypium applies its class filter.
  if (abilityDelegatorRegistry.getArguments().parameters['-s nearbyUxOnly'] === 'true') {
    nearbyUxRestorationTest();
    return;
  }
  smokeTest();
  controlLayoutWarningsTest();
  licenseModelTest();
  pauseParamsTest();
  settingsHelpersTest();
  checkpointStoreTest();
  catalogProductServiceTest();
  nearbyServiceTest();
  nearbyMvpScanFlowTest();
  nearbyUxRestorationTest();
  builtinPlaySmokeTest();
  builtinGameplayAcceptanceTest();
}
