"""CPU checks for protocol export and locked input refusal; no Wine or GPU."""
import importlib.util
import json
from pathlib import Path
import sys
import tempfile
import unittest
from unittest.mock import patch

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT))
import build
import shaders


class BuildInputs(unittest.TestCase):
    def test_export_reproducible_and_preserves_existing(self):
        with tempfile.TemporaryDirectory() as tmp:
            out = Path(tmp) / 'protocol'
            first = build.protocol.export(out)
            self.assertEqual(first, build.protocol.export(out))
            self.assertEqual(first['exports']['pre_sr_spec.h'], '1e57fbf32d827bf0cb60876e23fbe9ecb52ac0b6160887778c04e13073eaab09')
            self.assertEqual(first['exports']['native_runtime_model.h'], '92c16609685a651a405842126b6160fd02376c2be034d2142f365bda119a0205')
            changed = out / 'native_runtime_model.h'
            changed.write_bytes(b'preserved edited source')
            with self.assertRaises(ValueError):
                build.protocol.export(out)
            self.assertEqual(changed.read_bytes(), b'preserved edited source')

    def test_relative_export_refused(self):
        with self.assertRaises(ValueError):
            build.protocol.export(Path('relative'))

    def test_required_source_drift_before_compiler(self):
        real_sha = build.sha
        selected = ROOT / 'runtime/wine_runtime_model.cpp'
        with patch.object(build, 'sha', side_effect=lambda p: '0' * 64 if Path(p) == selected else real_sha(p)), \
             patch.object(build, 'compiler_inputs', side_effect=AssertionError('compiler must not run')):
            with self.assertRaisesRegex(ValueError, 'migrated source drift'):
                build.prepare(build.parser().parse_args([]))

    def test_sdk_drift_before_generation_or_compiler(self):
        real_sha = build.sha
        with patch.object(build, 'sha', side_effect=lambda p: '0' * 64 if Path(p).name == 'json.hpp' else real_sha(p)), \
             patch.object(build.protocol, 'export', side_effect=AssertionError('export must not run')):
            with self.assertRaisesRegex(ValueError, 'SDK/json dependency drift'):
                build.prepare(build.parser().parse_args([]))

    def test_shader_exact_bytes_and_invalid_rejected(self):
        data = shaders.validate()
        self.assertEqual(len(data), 24628)
        with tempfile.TemporaryDirectory() as tmp:
            altered = Path(tmp) / 'shader.spv'
            altered.write_bytes(data[:-4] + b'FAIL')
            with patch.object(shaders, 'SPV', altered):
                with self.assertRaisesRegex(ValueError, 'shader bytecode/header drift'):
                    shaders.validate()

    def test_no_historical_source_in_compile_includes(self):
        sources = list((ROOT / 'runtime').glob('*.h')) + list((ROOT / 'runtime').glob('*.cpp'))
        for source in sources:
            for line in source.read_text().splitlines():
                if line.startswith('#include'):
                    self.assertNotIn('/nrbridge-design/', line)
                    self.assertNotIn('/inputs/helper-f527/', line)
                    self.assertNotIn('/poc/', line)


if __name__ == '__main__':
    unittest.main()
