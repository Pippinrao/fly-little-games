import copy
import csv
import hashlib
import importlib.util
import json
from pathlib import Path
import tempfile
import unittest
import zipfile
import contextlib
import io

SCRIPT = Path(__file__).with_name('game_titles.py')


class IndexTests(unittest.TestCase):
    def setUp(self):
        self.assertTrue(SCRIPT.exists(), 'The deterministic index generator is missing')
        spec = importlib.util.spec_from_file_location('game_titles', SCRIPT)
        self.module = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(self.module)
        self.entry = dict(sha256='a' * 64, id='nes-' + 'a' * 64,
                          en='A "Game"', zh='游戏', aliases=['Other\\Name'],
                          english_source='curated')
        self.data = dict(schema_version=1, entries=[self.entry])

    def test_emits_cpp_safe_unicode_quotes_backslashes_and_aliases(self):
        self.entry['aliases'].append('Another Name')
        result = self.module.render(self.data)
        self.assertIn('"A \\"Game\\"", "游戏", "Other\\\\Name\\nAnother Name"', result)

    def test_rejects_duplicate_hashes(self):
        self.data['entries'].append(copy.deepcopy(self.entry))
        with self.assertRaisesRegex(ValueError, 'duplicate'):
            self.module.render(self.data)

    def test_rejects_missing_bilingual_title(self):
        self.entry['en'] = ''
        with self.assertRaisesRegex(ValueError, 'title'):
            self.module.render(self.data)

    def test_rejects_machine_placeholder(self):
        self.entry['en'] = 'Unknown game aaaaaa'
        with self.assertRaisesRegex(ValueError, 'placeholder'):
            self.module.render(self.data)

    def test_rejects_local_path_metadata(self):
        self.entry['aliases'] = ['D:/private/game.nes']
        with self.assertRaisesRegex(ValueError, 'path'):
            self.module.render(self.data)

    def test_rejects_unstable_id(self):
        self.entry['id'] = 'game-1'
        with self.assertRaisesRegex(ValueError, 'id'):
            self.module.render(self.data)

    def test_order_is_deterministic(self):
        second = dict(self.entry, sha256='b' * 64, id='nes-' + 'b' * 64)
        self.data['entries'].append(second)
        first = self.module.render(self.data)
        self.data['entries'].reverse()
        self.assertEqual(first, self.module.render(self.data))

    def test_rejects_incorrect_declared_unique_count(self):
        self.data['corpus'] = {'unique_roms': 2000, 'manifest_rows': 2000}
        with self.assertRaisesRegex(ValueError, 'count'):
            self.module.render(self.data)

    def test_check_fails_on_stale_generated_file(self):
        with tempfile.TemporaryDirectory() as folder:
            source, output = Path(folder) / 'source.json', Path(folder) / 'output.inc'
            source.write_text(json.dumps(self.data), encoding='utf-8')
            output.write_text('stale', encoding='utf-8')
            with contextlib.redirect_stderr(io.StringIO()) as errors:
                result = self.module.main(['check', '--source', str(source), '--output', str(output)])
            self.assertEqual(1, result)
            self.assertIn('stale', errors.getvalue())

    def fixture(self, folder, content=b'NES\x1a' + b'\x00' * 12 + b'game bytes'):
        root = Path(folder)
        with zipfile.ZipFile(root / '游戏.zip', 'w') as archive:
            archive.writestr('game.nes', content)
        sha = hashlib.sha256(content).hexdigest()
        self.data['entries'] = [dict(self.entry, sha256=sha, id='nes-' + sha)]
        manifest = root / 'manifest.csv'
        with manifest.open('w', encoding='utf-8', newline='') as stream:
            writer = csv.DictWriter(stream, fieldnames=['output', 'title', 'source', 'source_member', 'sha256', 'size'])
            writer.writeheader()
            writer.writerow(dict(output='游戏.zip', title='游戏', source='old.zip', source_member='old/game.nes', sha256=sha.upper(), size=len(content)))
        return root, manifest

    def test_verifies_real_zip_content_not_only_manifest_counts(self):
        with tempfile.TemporaryDirectory() as folder:
            root, manifest = self.fixture(folder)
            report = self.module.verify_corpus(root, manifest, self.data)
            self.assertEqual(1, report['verified_archives'])
            self.assertEqual(1, report['verified_unique_hashes'])
            self.assertEqual([], report['errors'])

    def test_detects_changed_rom_despite_same_manifest(self):
        with tempfile.TemporaryDirectory() as folder:
            root, manifest = self.fixture(folder)
            with zipfile.ZipFile(root / '游戏.zip', 'w') as archive:
                archive.writestr('game.nes', b'changed')
            report = self.module.verify_corpus(root, manifest, self.data)
            self.assertTrue(any('hash mismatch' in e for e in report['errors']))

    def test_detects_unindexed_and_unmanifested_archive(self):
        with tempfile.TemporaryDirectory() as folder:
            root, manifest = self.fixture(folder)
            self.data['entries'] = []
            with zipfile.ZipFile(root / 'extra.zip', 'w') as archive:
                archive.writestr('game.nes', b'new')
            report = self.module.verify_corpus(root, manifest, self.data)
            self.assertTrue(any('unindexed' in e for e in report['errors']))
            self.assertTrue(any('unmanifested' in e for e in report['errors']))


class CorpusTitleReviewTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        data = json.loads((SCRIPT.parents[2] / 'shared/data/game_titles.json').read_text(encoding='utf-8'))
        cls.entries = {e['sha256']: e for e in data['entries']}

    def test_adventure_island_fan_translation_is_not_a_us_release(self):
        entry = self.entries['9de123f61454cc7a9ce133d68c85047b717dc864c7eb954a714a7dd8effea6f2']
        self.assertIn('English translation', entry['en'])
        self.assertIn('英译', entry['zh'])
        self.assertNotIn('美版', entry['zh'])
        self.assertIn('高桥名人的冒险岛4(美版)', entry['aliases'])

    def test_chinese_dragon_quest_retitles_keep_underlying_sequel_numbers(self):
        cases = [('f13bc164e07f3bc9f6791f559c8ed8daea8d19d76028c310f86c7b1c289f7e5a', 'II', '2', 'V'),
                 ('c2efeb3595194b81840d36d44ef6cbee08f644f440f8abce60e8ac75d48d8a6c', 'III', '3', 'VI'),
                 ('e72ef2c1ac1302c626e620512cb05555e97c4fbfb769c4d895159e0063620ec7', 'IV', '4', 'VII')]
        for sha, roman, chinese_number, retitle in cases:
            entry = self.entries[sha]
            self.assertTrue(entry['en'].startswith('Dragon Quest ' + roman + ' ('))
            self.assertTrue(entry['zh'].startswith('勇者斗恶龙' + chinese_number))
            self.assertIn('Yong Zhe Dou E Long ' + retitle, entry['en'])

    def test_duplicate_archive_label_does_not_invent_a_sequel(self):
        entry = self.entries['af990738a3feac1614f1da54df1f06bfe3b3bab8e9d02fcf82d9a5ca99c30fe2']
        self.assertEqual('本将棋内藤九段将棋秘传', entry['zh'])
        self.assertIn('本将棋内藤九段将棋秘传2', entry['aliases'])

    def test_explicit_english_translation_never_displayed_as_us_release(self):
        for entry in self.entries.values():
            if 'English translation' in entry['en'] or 'English v' in entry['en']:
                self.assertNotIn('美版', entry['zh'], entry['sha256'])

    def test_copier_footer_does_not_promote_conflicting_short_filename(self):
        cases = [('af499ff7afd42ce24e5affad73ef1a1e4e90db562d22010b148cc95b662370f6', 'Contra Force', '魂斗罗外传'),
                 ('609400c9db17', 'Blades of Steel', '钢铁冰球'),
                 ('f9e1c791c690', 'Bomberman Collection', '炸弹人合集'),
                 ('29e4b8174f12', 'Cobra Command', '眼镜蛇指挥官')]
        for prefix, en, zh in cases:
            entry = next(e for sha, e in self.entries.items() if sha.startswith(prefix))
            self.assertTrue(entry['en'].startswith(en), entry['en'])
            self.assertTrue(entry['zh'].startswith(zh), entry['zh'])


if __name__ == '__main__':
    unittest.main()
