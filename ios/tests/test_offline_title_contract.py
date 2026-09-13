"""Portable bridge/wiring checks; Objective-C behavior runs on macOS."""
from pathlib import Path
import unittest

ROOT = Path(__file__).resolve().parents[2]


class OfflineTitleContract(unittest.TestCase):
    def test_ios_copies_snapshot_metadata_without_changing_identity(self):
        bridge = (ROOT / 'ios/app/bridge/FlyNesAppBridge.mm').read_text(encoding='utf-8')
        self.assertIn('fly_catalog_snapshot_get_title(snapshot, index, &title)', bridge)
        self.assertIn('indexedTitleEn:', bridge)
        self.assertIn('@"canonicalId" : @(canonical.c_str())', bridge)

    def test_ios_locale_change_only_reprojects_cached_fields(self):
        view = (ROOT / 'ios/app/CatalogLibraryView.swift').read_text(encoding='utf-8')
        self.assertIn('.onChange(of: locale) { _ in reprojectTitles() }', view)

    def test_run_surface_receives_localized_cached_title(self):
        run = (ROOT / 'ios/app/RunGameView.swift').read_text(encoding='utf-8')
        self.assertTrue('uiViewController.gameTitle = gameTitle' in run, 'run surface must receive current language title')

    def test_harmony_snapshot_and_alias_bridge(self):
        bridge = (ROOT / 'harmony/entry/src/main/cpp/napi_init.cpp').read_text(encoding='utf-8')
        self.assertIn('fly_catalog_snapshot_get_title(snapshot.get(), index, &title)', bridge)
        self.assertIn('"searchAliases"', bridge)

    def test_harmony_effective_language_and_cached_run_titles(self):
        service = (ROOT / 'harmony/entry/src/main/ets/service/CatalogProductService.ets').read_text(encoding='utf-8')
        self.assertIn('export function localizedGameTitle(', service)
        run = (ROOT / 'harmony/entry/src/main/ets/pages/RunGame.ets').read_text(encoding='utf-8')
        self.assertIn('localizedGameTitle(', run)
        self.assertIn("params['titleEn']", run)


if __name__ == '__main__':
    unittest.main()
