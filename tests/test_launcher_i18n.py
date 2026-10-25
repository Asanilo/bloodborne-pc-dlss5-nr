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

    def test_current_launcher_labels_have_chinese_and_format_slots(self):
        import ast
        from string import Formatter
        tree = ast.parse((ROOT / 'launcher/bbport_launcher.py').read_text())
        labels = set(i18n.EN)
        for node in ast.walk(tree):
            if (isinstance(node, ast.Call) and isinstance(node.func, ast.Name)
                    and node.func.id == 'tr' and node.args
                    and isinstance(node.args[0], ast.Constant)
                    and isinstance(node.args[0].value, str)):
                labels.add(node.args[0].value)
        # Table-driven labels do not occur as tr("literal") calls.
        for node in tree.body:
            if isinstance(node, ast.Assign) and any(
                    isinstance(target, ast.Name) and target.id in (
                        'CONTROLS', 'EFFECTS', 'MEMORY_MODELS', 'DRAW_PIPE',
                        'READBACKS', 'PREUPLOAD', 'PRESENT_MODES', 'LANGUAGES')
                    for target in node.targets):
                choices=[node.value.body,node.value.orelse] if isinstance(node.value,ast.IfExp) else [node.value]
                for choice in choices:
                    for entry in ast.literal_eval(choice):
                        labels.add(entry[1] if node.targets[0].id in ('CONTROLS', 'EFFECTS') else entry[0])
        # Names/abbreviations remain recognizable in every language.
        names = {'Steam / Lutris', "The Hunter's Dream", 'L3', 'SSAO', 'MangoHud',
                 'DLSS (NVIDIA RTX)', 'FSR 4', 'FSR 4.1.1', 'FSR 3', 'Vulkan', 'NRBridge'}
        previous = i18n.language()
        try:
            i18n.set_language('zh_CN')
            for label in sorted(labels):
                translated = i18n.tr(label)
                self.assertFalse(any('\u0400' <= c <= '\u04ff' for c in translated), label)
                if any(c.isalpha() for c in label) and label not in names:
                    self.assertTrue(any('\u4e00' <= c <= '\u9fff' for c in translated), label)
                slots = lambda text: [field for _, field, _, _ in Formatter().parse(text) if field is not None]
                self.assertEqual(slots(label), slots(translated), label)
        finally:
            i18n.set_language(previous)
