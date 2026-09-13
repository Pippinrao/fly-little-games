import { describe, it, expect } from '@ohos/hypium';
import { GameCenterRow } from 'libentry.so';
import { localizedGameTitle, uniqueGameCenterRows } from '../../../main/ets/service/CatalogProductService';

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
    it('projects_cached_titles_using_simulator_setting_before_system', 0, () => {
      expect(localizedGameTitle('Contra', '魂斗罗', 'renamed.nes', 'en', 'zh-CN')).assertEqual('Contra');
      expect(localizedGameTitle('Contra', '魂斗罗', 'renamed.nes', 'zh-Hans', 'en')).assertEqual('魂斗罗');
      expect(localizedGameTitle('Contra', '魂斗罗', 'renamed.nes', 'system', 'zh-CN')).assertEqual('魂斗罗');
      expect(localizedGameTitle('Contra', '魂斗罗', 'renamed.nes', 'fr', 'zh-CN')).assertEqual('Contra');
      expect(localizedGameTitle('', '', 'unknown.nes', 'zh-Hans', 'en')).assertEqual('unknown.nes');
    });
    it('keeps_maximum_popularity_across_exact_content_copies', 0, () => {
      const first = row('game:a', false, 'collection.zip');
      first.popularityScore = 0;
      const copy = row('game:a', false, 'Contra.zip');
      copy.popularityScore = 100;
      const translated = row('game:b', false, 'Contra translated.zip');
      translated.popularityScore = 100;
      const rows = uniqueGameCenterRows([first, copy, translated]);
      expect(rows.length).assertEqual(2);
      expect(rows[0].popularityScore).assertEqual(100);
      expect(rows[0].sourceRelativePath).assertEqual('collection.zip');
      expect(rows[1].canonicalId).assertEqual('game:b');
    });
    it('publishes_one_card_per_canonical_game_and_keeps_builtin_first', 0, () => {
      const rows = uniqueGameCenterRows([
        row('game:a', false, 'a-copy-1.nes'),
        row('game:b', false, 'b.nes'),
        row('game:a', false, 'a-copy-2.nes'),
        row('builtin:thwaite', true, 'thwaite.nes')
      ]);

      expect(rows.length).assertEqual(3);
      expect(rows[0].canonicalId).assertEqual('builtin:thwaite');
      expect(rows[1].sourceRelativePath).assertEqual('a-copy-1.nes');
      expect(rows[2].canonicalId).assertEqual('game:b');
    });

    it('keeps_every_bundled_game_before_user_games', 0, () => {
      const rows = uniqueGameCenterRows([
        row('game:a', false, 'a.nes'),
        row('builtin:dabg', true, 'dabg.nes'),
        row('builtin:rhde', true, 'rhde.nes'),
        row('builtin:thwaite', true, 'thwaite.nes')
      ]);

      expect(rows.length).assertEqual(4);
      expect(rows[0].canonicalId).assertEqual('builtin:dabg');
      expect(rows[1].canonicalId).assertEqual('builtin:rhde');
      expect(rows[2].canonicalId).assertEqual('builtin:thwaite');
      expect(rows[3].canonicalId).assertEqual('game:a');
    });
  });
}
