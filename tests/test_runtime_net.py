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
        for name, flags in [('module', []), ('missing', ['-DNO_INIT'])]:
            subprocess.run([*common, '-fPIC', '-shared', *flags,
                            str(ROOT / 'tests/fixtures/online_module.c'),
                            '-o', str(cls.out / (name + '.so'))], check=True)

    def run_case(self, module, mode):
        result = subprocess.run([str(self.out / 'test'), str(self.out / (module + '.so')), mode],
                                capture_output=True, text=True, timeout=5)
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        self.assertIn('PASS:', result.stdout)

    def test_success_publishes_hooks_once(self): self.run_case('module', 'ok')
    def test_failed_init_never_publishes_hooks(self): self.run_case('module', 'fail')
    def test_missing_exports_never_publish_hooks(self): self.run_case('missing', 'fail')
