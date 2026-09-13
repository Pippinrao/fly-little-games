import smokeTest from './Smoke.test';
import controlLayoutWarningsTest from './ControlLayoutWarnings.test';
import licenseModelTest from './LicenseModel.test';
import pauseParamsTest from './PauseParams.test';
import settingsHelpersTest from './SettingsHelpers.test';
import checkpointStoreTest from './CheckpointStore.test';
import catalogProductServiceTest from './CatalogProductService.test';
import nearbyServiceTest from './NearbyService.test';

export default function testsuite() {
  smokeTest();
  controlLayoutWarningsTest();
  licenseModelTest();
  pauseParamsTest();
  settingsHelpersTest();
  checkpointStoreTest();
  catalogProductServiceTest();
  nearbyServiceTest();
}
