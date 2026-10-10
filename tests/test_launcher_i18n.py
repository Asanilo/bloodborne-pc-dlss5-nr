from paths import ROOT
import sys
import unittest
sys.path.insert(0, str(ROOT / "launcher"))
import bbport_i18n as i18n

class LauncherTranslations(unittest.TestCase):
    def test_local_controls_and_upstream_translations_coexist(self):
        previous = i18n.language()
        try:
            i18n.set_language("zh_CN")
            self.assertEqual(i18n.tr("Light attack (R1)"), "轻攻击（R1）")
            # New upstream strings must fall through to the upstream Chinese table.
            for key, translated in i18n.ZH_CN.items():
                english = i18n.EN.get(key, key)
                if english not in i18n.ZH:
                    self.assertEqual(i18n.tr(key), translated, key)
            i18n.set_language("en")
            self.assertEqual(i18n.tr("Light attack (R1)"), "Light attack (R1)")
        finally:
            i18n.set_language(previous)

    def test_chinese_uses_existing_region_patch(self):
        sys.path.insert(0, str(ROOT / "scripts"))
        from patches import language_patches
        for language in ('10', '11'):
            self.assertEqual(language_patches({'BB_LANGUAGE': language}), ['Unlock Game Region'])
        self.assertEqual(language_patches({'BB_LANGUAGE': '1'}), [])
        self.assertEqual(language_patches({}), [])
