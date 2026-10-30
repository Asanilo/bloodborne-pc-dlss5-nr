# SPDX-FileCopyrightText: Copyright 2026 bbport contributors
# SPDX-License-Identifier: GPL-2.0-or-later
"""Region-preserving language maps and the real patch compiler entry point."""
from paths import ROOT
import contextlib
import copy
import io
import json
import struct
import tempfile
import unittest
from pathlib import Path
from unittest import mock
import xml.etree.ElementTree as ET

import patches


class GameLanguageTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        # Synthetic image: no copyrighted executable is needed by CI.
        cls.elf = bytearray(0x4000 + max(patches.VOICE_TABLES) + 84)
        struct.pack_into('<Q', cls.elf, 0x20, 0x40)
        struct.pack_into('<HH', cls.elf, 0x36, 56, 1)
        struct.pack_into('<IIQQQQQQ', cls.elf, 0x40, 1, 5, 0x4000, 0, 0,
                         len(cls.elf)-0x4000, 0x6000000, 0x4000)
        site, table = patches.EU_TEXT_TABLE
        struct.pack_into('<21i', cls.elf, 0x4000 + site, *table)

    def maps(self, settings=None, env=None):
        with contextlib.redirect_stdout(io.StringIO()):
            writes = patches.language_writes(settings or {}, self.elf, env or {})
        return {site: list(struct.unpack('<21i', data)) for site, data in writes}

    def test_chinese_selection_overrides_saved_text_in_every_region(self):
        for ps4, game in [('10', 18), ('11', 20)]:
            with self.subTest(language=ps4):
                tables = self.maps(env={'BB_LANGUAGE': ps4})
                for site in patches.TEXT_TABLES:
                    self.assertEqual(tables[site], [game]*21)
                for site in patches.VOICE_TABLES:
                    self.assertEqual(tables[site], list(range(8))+[1]*13)

    def test_other_system_languages_keep_game_selection(self):
        for lang in [str(i) for i in range(21) if i not in (10, 11)]:
            with self.subTest(language=lang):
                tables = self.maps(env={'BB_LANGUAGE': lang})
                for site in patches.TEXT_TABLES:
                    self.assertEqual(tables[site], list(range(21)))

    def test_explicit_text_and_voice_settings_and_auto(self):
        tables = self.maps({'game_text_language': 'frfr', 'game_voice_language': 'jajp'},
                           {'BB_LANGUAGE': '11'})
        for site in patches.TEXT_TABLES:
            self.assertEqual(tables[site], [2]*21)
        for site in patches.VOICE_TABLES:
            self.assertEqual(tables[site], [0]*21)
        self.assertEqual(self.maps({'game_text_language': 'auto'}, {'BB_LANGUAGE': '11'})
                         [patches.TEXT_TABLES[2]], list(range(21)))
        for settings in ({'game_text_language': 'invalid'}, {'game_voice_language': 'zhcn'}):
            self.assertEqual(self.maps(settings), self.maps())

    def test_bad_table_and_truncated_images_are_rejected(self):
        site = 0x4000 + patches.EU_TEXT_TABLE[0]
        with mock.patch.object(patches, 'EU_TEXT_TABLE', (patches.EU_TEXT_TABLE[0], [0]*21)):
            with self.assertRaisesRegex(ValueError, 'do not match'):
                patches.language_writes({}, self.elf, {})
        for elf in (b'', self.elf[:site+84]):  # sentinel exists but later voice tables do not
            with self.assertRaisesRegex(ValueError, 'do not match'):
                patches.language_writes({}, elf, {})

    def run_main(self, out, extra='', external=None, elf=None):
        args = ['patches.py', '--out', str(out), '--fps', '30', '--extra', extra,
                '--settings', str(out/'absent.ini')]
        if external:
            args += ['--patches-dir', str(external), '--patches-config', str(external/'patches.json')]
        stdout, stderr = io.StringIO(), io.StringIO()
        env = {'BB_LANGUAGE': '11', 'BB_INTEL_TONEMAP_FIX': '0',
               'BB_SKIP_NETWORK_CHOICE': '0', 'BB_GX_COMMAND_HEAP': '0'}
        with mock.patch('sys.argv', args), mock.patch.dict(patches.os.environ, env, clear=True), \
                mock.patch.object(Path, 'read_bytes', return_value=self.elf if elf is None else elf), \
                contextlib.redirect_stdout(stdout), contextlib.redirect_stderr(stderr):
            patches.main()
        blob = (out/'patches.bin').read_bytes()
        magic, base, count = struct.unpack_from('<8sQQ', blob)
        self.assertEqual((magic, base), (b'BBPATCH2', 0x400000))
        pos, writes = 24, []
        for _ in range(count):
            offset, size = struct.unpack_from('<QQ', blob, pos)
            pos += 16
            writes.append((offset, blob[pos:pos+size]))
            pos += size
        self.assertEqual(pos, len(blob))
        return writes, stdout.getvalue(), stderr.getvalue()

    def test_main_writes_only_language_tables_and_keeps_other_patches(self):
        with tempfile.TemporaryDirectory() as folder:
            out = Path(folder)
            writes, log, warning = self.run_main(out)
            self.assertEqual(set(dict(writes)), set(patches.TEXT_TABLES+patches.VOICE_TABLES))
            self.assertNotIn('Unlock Game Region', log+warning)
            baseline, _, _ = self.run_main(out, 'Skip Intro')
            migrated, log, warning = self.run_main(out, 'Unlock Game Region;Skip Intro')
            self.assertEqual(migrated, baseline)
            self.assertIn('replaced by language-table mapping', warning)
            self.assertNotIn('Unlock Game Region', log)

    def test_external_legacy_selection_migrates_without_editing_config(self):
        with tempfile.TemporaryDirectory() as folder:
            out = Path(folder)
            root = ET.Element('Patch')
            source = ET.parse(ROOT/'patches/Bloodborne.xml')
            for meta in source.iter('Metadata'):
                if meta.get('Name') in ('Unlock Game Region', 'Skip Intro'):
                    root.append(copy.deepcopy(meta))
            ET.ElementTree(root).write(out/'custom.xml')
            config = json.dumps({'enabled': ['custom.xml/Unlock Game Region', 'custom.xml/Skip Intro']})
            (out/'patches.json').write_text(config)
            baseline, _, _ = self.run_main(out, 'Skip Intro')
            migrated, _, warning = self.run_main(out, external=out)
            self.assertEqual(sorted(migrated), sorted(baseline))
            self.assertIn('custom.xml/Unlock Game Region replaced', warning)
            self.assertEqual((out/'patches.json').read_text(), config)

    def test_main_does_not_publish_patches_on_image_mismatch(self):
        with tempfile.TemporaryDirectory() as folder:
            out = Path(folder)
            sentinel = b'previous artifact'
            (out/'patches.bin').write_bytes(sentinel)
            # Structurally valid ELF header, but missing tables.
            with self.assertRaisesRegex(ValueError, 'do not match'):
                self.run_main(out, elf=self.elf[:256])
            self.assertEqual((out/'patches.bin').read_bytes(), sentinel)


if __name__ == '__main__':
    unittest.main()
