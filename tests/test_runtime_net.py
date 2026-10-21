"""CPU fixture for the actual runtime module publication path; no network or game assets."""
from paths import ROOT
from pathlib import Path
import os
import subprocess
import tempfile
import unittest

class ModulePublication(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.tmp = tempfile.TemporaryDirectory(prefix='bbport-net-cpu-')
        cls.addClassCleanup(cls.tmp.cleanup)
        cls.out = Path(cls.tmp.name)
        cc = os.environ.get('CC', 'cc')
        common = [cc, '-std=c11', '-g', '-O1', '-UNDEBUG', '-Wall', '-Wextra', '-Werror',
                  '-I' + str(ROOT), '-I' + str(ROOT / 'src')]
        subprocess.run([*common, str(ROOT / 'tests/test_runtime_net.c'), '-ldl',
                        '-o', str(cls.out / 'test')], check=True)
        for name, flags in [('module', []), ('missing', ['-DNO_INIT']),
                            ('unknown', ['-DNO_MOUSE_CAPABILITY']),
                            ('invalid', ['-DINVALID_MOUSE_CAPABILITY']),
                            ('legacy', ['-DLEGACY_BBSC_REFUSAL'])]:
            subprocess.run([*common, '-fPIC', '-shared', *flags,
                            str(ROOT / 'tests/fixtures/online_module.c'),
                            '-o', str(cls.out / (name + '.so'))], check=True)

    def run_case(self, module, mode, compatible=0):
        result = subprocess.run([str(self.out / 'test'), str(self.out / (module + '.so')),
                                 mode, str(compatible)],
                                capture_output=True, text=True, timeout=5)
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        self.assertIn('PASS:', result.stdout)

    def test_success_publishes_hooks_once(self): self.run_case('module', 'ok', 1)
    def test_failed_init_never_publishes_hooks(self): self.run_case('module', 'fail')
    def test_missing_exports_never_publish_hooks(self): self.run_case('missing', 'fail')
    def test_unknown_capability_refuses_mouse(self): self.run_case('unknown', 'ok')
    def test_noncanonical_capability_refuses_mouse(self): self.run_case('invalid', 'ok')
    def test_bbsc_capability_admits_reviewed_mouse_path(self): self.run_case('module', 'bbsc', 1)
    def test_legacy_module_bbsc_refusal_is_still_respected(self): self.run_case('legacy', 'bbsc')
