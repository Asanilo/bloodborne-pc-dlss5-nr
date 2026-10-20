"""Helper build delegation/isolation tests; no Wine, GPU, or real PE build."""
import hashlib
import importlib.util
import json
import os
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest
from unittest.mock import patch

HERE = Path(__file__).resolve().parents[1]
spec = importlib.util.spec_from_file_location('prepare_helper', HERE / 'prepare_helper.py')
entry = importlib.util.module_from_spec(spec)
spec.loader.exec_module(entry)


class HelperEntries(unittest.TestCase):
    def setUp(self):
        tmp = tempfile.TemporaryDirectory()
        self.addCleanup(tmp.cleanup)
        self.root = Path(tmp.name)
        self.helper = self.root / 'helper'
        script = self.helper / 'nrbridge/build.py'
        script.parent.mkdir(parents=True)
        script.write_text('import json,sys\nfrom pathlib import Path\nPath(__file__).with_name("received.json").write_text(json.dumps(sys.argv[1:]))\n')

    def test_normal_and_old_runtime_entry_delegate_without_frozen_inputs(self):
        environment = dict(os.environ, NR_DEV_ROOT=str(self.root), PYTHONDONTWRITEBYTECODE='1')
        self.assertFalse((self.root / 'inputs').exists())
        for script in (HERE / 'prepare_helper.py', HERE / 'helper_runtime/build.py'):
            result = subprocess.run([sys.executable, str(script), '--helper-root', str(self.helper), '--check-only'], env=environment, capture_output=True, text=True)
            self.assertEqual(result.returncode, 0, result.stderr)
            self.assertEqual(json.loads((self.helper / 'nrbridge/received.json').read_text()), ['--check-only'])
        self.assertFalse((self.root / 'inputs').exists())
        self.assertFalse((self.root / 'build').exists())

    def test_helper_environment_selection_and_missing_selection(self):
        environment = dict(os.environ, NR_DEV_ROOT=str(self.root),
                           NR_HELPER_ROOT=str(self.helper), PYTHONDONTWRITEBYTECODE='1')
        script = HERE / 'prepare_helper.py'
        selected = subprocess.run([sys.executable, str(script), '--check-only'],
                                  env=environment, capture_output=True, text=True)
        self.assertEqual(selected.returncode, 0, selected.stderr)
        self.assertEqual(json.loads((self.helper / 'nrbridge/received.json').read_text()), ['--check-only'])
        environment.pop('NR_HELPER_ROOT')
        missing = subprocess.run([sys.executable, str(script)], env=environment,
                                 capture_output=True, text=True)
        self.assertEqual(missing.returncode, 3)
        self.assertIn('--helper-root or NR_HELPER_ROOT', missing.stderr)
        self.assertFalse((self.root / 'build').exists())

    def test_missing_helper_rejected_without_frozen_fallback(self):
        with patch.object(entry, 'ROOT', self.root):
            self.assertEqual(entry.main(['--helper-root', str(self.root / 'missing')]), 3)
        self.assertFalse((self.root / 'inputs').exists())

    def frozen(self):
        source = self.root / 'inputs/helper-f527'
        source.mkdir(parents=True)
        cpp = source / 'frozen.cpp'
        cpp.write_text('frozen input bytes')
        effective = source / 'effective-generated-spec.json'
        effective.write_text('{}')
        dependency = self.root / 'locked-header.h'
        dependency.write_text('locked dependency')
        compiler = Path(sys.executable).resolve()
        old_output = self.root / 'build/helper'
        old_output.mkdir(parents=True)
        (old_output / 'wine_async_presr_play.exe').write_text('old private PE preserved')
        script = self.root / 'fake_compile.py'
        script.write_text('import sys\nfrom pathlib import Path\np=Path(sys.argv[1])\np.write_bytes(b"#!/usr/bin/env python3\\nraise SystemExit(0)\\n" if p.name=="wire_check" else b"fixture PE")\np.chmod(0o755)\n')
        sha = entry.sha
        manifest = {'files': [{'export': str(cpp), 'sha256': sha(cpp)}],
                    'dependency_pins': {str(dependency): sha(dependency)},
                    'effective_spec_sha256': sha(effective),
                    'compiler': {'path': str(compiler), 'sha256': sha(compiler)},
                    'commands': [[str(compiler), str(script), str(old_output / 'wire_check')],
                                 [str(old_output / 'wire_check')],
                                 [str(compiler), str(script), str(old_output / 'wine_async_presr_play.exe')]],
                    'artifact_sha256': '0' * 64}
        (source / 'manifest.json').write_text(json.dumps(manifest))
        return source, old_output

    def test_explicit_legacy_and_old_alias_isolate_and_preserve_originals(self):
        source, old_output = self.frozen()
        originals = {str(p): p.read_bytes() for p in [*source.iterdir(), *old_output.iterdir()] if p.is_file()}
        allowed = self.root / 'build/convergence/legacy-helper-rebuild'
        with patch.object(entry, 'ROOT', self.root):
            for number, alias in enumerate(('--legacy-rebuild', '--rebuild')):
                out = allowed / str(number)
                self.assertEqual(entry.main([alias, '--legacy-output', str(out)]), 0)
                self.assertEqual((out / 'wine_async_presr_play.exe').read_bytes(), b'fixture PE')
                info = json.loads((out / 'build.json').read_text())
                self.assertEqual(info['frozen_expected_artifact_sha256'], '0' * 64)
                self.assertFalse(info['legacy_originals_modified'])
            self.assertEqual(entry.main(['--legacy-rebuild', '--legacy-output', str(allowed / '0')]), 3)
            self.assertEqual(entry.main(['--legacy-rebuild', '--legacy-output', str(old_output)]), 3)
        self.assertEqual(originals, {p: Path(p).read_bytes() for p in originals})

    def test_legacy_drift_refused_before_outputs(self):
        source, _ = self.frozen()
        (source / 'frozen.cpp').write_text('drift')
        with patch.object(entry, 'ROOT', self.root):
            self.assertEqual(entry.main(['--legacy-rebuild']), 3)
        self.assertFalse((self.root / 'build/convergence').exists())

    def test_legacy_retirement_import_and_cli_compatibility(self):
        oldspec = importlib.util.spec_from_file_location('legacy_retirement_compat', HERE / 'retirement.py')
        old = importlib.util.module_from_spec(oldspec)
        oldspec.loader.exec_module(old)
        newspec = importlib.util.spec_from_file_location('peer_retirement', HERE / 'peer_retirement.py')
        peer = importlib.util.module_from_spec(newspec)
        newspec.loader.exec_module(peer)
        for left, right in ((True, 1), ({'model': 1.0}, {'model': 1.0}), ([1, False], [1, 0])):
            self.assertEqual(old.exact_typed(left, right), peer.exact_typed(left, right))
        self.assertEqual(old.rows('PROOF tick=42\n', 'PROOF'), peer.rows('PROOF tick=42\n', 'PROOF'))
        for script in ('retirement.py', 'peer_retirement.py'):
            result = subprocess.run([sys.executable, str(HERE / script), '--help'], capture_output=True, text=True)
            self.assertEqual(result.returncode, 0, result.stderr)
            self.assertIn('--run', result.stdout)


if __name__ == '__main__':
    unittest.main()
