import { describe, it, expect } from '@ohos/hypium';
import { GameCenterRow } from 'libentry.so';
import { uniqueGameCenterRows } from '../../../main/ets/service/CatalogProductService';

function row(id: string, builtin: boolean, path: string): GameCenterRow {
  return {
    canonicalId: id,
    titleEn: path,
    titleZhHans: '',
    builtin: builtin,
    favorite: false,
    lastPlayedSequence: 0,
    originalFilename: path,
    sourceUuidHex: builtin ? '' : '00112233445566778899aabbccddeeff',
    sourceRelativePath: path,
    packageFormat: 1
  };
}

export default function catalogProductServiceTest() {
  describe('CatalogProductService', () => {
    it('publishes_one_card_per_canonical_game_and_keeps_builtin_first', 0, () => {
      const rows = uniqueGameCenterRows([
        row('game:a', false, 'a-copy-1.nes'),
        row('game:b', false, 'b.nes'),
        row('game:a', false, 'a-copy-2.nes'),
        row('game:builtin', true, 'from_below.nes')
      ]);

      expect(rows.length).assertEqual(3);
      expect(rows[0].canonicalId).assertEqual('game:builtin');
      expect(rows[1].sourceRelativePath).assertEqual('a-copy-1.nes');
      expect(rows[2].canonicalId).assertEqual('game:b');
    });
  });
}
