import { describe, it, expect } from '@ohos/hypium';
import { LicenseModel } from '../../../main/ets/pages/LicenseModel';

export default function licenseModelTest() {
  describe('LicenseModel', () => {
    it('maps_known_license_files', 0, () => {
      const nestopia = LicenseModel.byFileName('nestopiaue-gplv2.txt');
      expect(nestopia !== undefined).assertTrue();
      expect(nestopia!.displayName).assertEqual('Nestopia UE (GPLv2)');
      expect(nestopia!.sourceUrl).assertEqual('https://github.com/0ldsk00l/nestopia');
      expect(nestopia!.hasSourceUrl()).assertTrue();

      const own = LicenseModel.byFileName('own-gplv2.txt');
      expect(own !== undefined).assertTrue();
      expect(own!.hasSourceUrl()).assertFalse();

      const zlib = LicenseModel.byFileName('zlib-license.txt');
      expect(zlib!.sourceUrl).assertEqual('https://zlib.net');

      const fromBelow = LicenseModel.byFileName('LICENSE-from-below.txt');
      expect(fromBelow!.sourceUrl).assertEqual('https://mhughson.itch.io/from-below');
    });

    it('resolveBody_throws_when_asset_missing', 0, () => {
      let threw = false;
      try {
        LicenseModel.resolveBody('missing.txt', undefined);
      } catch (error) {
        threw = true;
      }
      expect(threw).assertTrue();
    });

    it('lists_four_entries', 0, () => {
      expect(LicenseModel.all().length).assertEqual(4);
    });
  });
}
