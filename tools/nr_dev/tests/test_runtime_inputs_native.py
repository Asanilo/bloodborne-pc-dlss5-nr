"""Compile and execute only the Native path/argv helpers; no GPU/loader calls."""
import subprocess
import tempfile
import unittest
from pathlib import Path


class NativeRuntimePaths(unittest.TestCase):
    def test_real_header_path_validation_and_legacy_argv(self):
        root = Path(__file__).resolve().parents[3]
        with tempfile.TemporaryDirectory(prefix='nr-runtime-paths-') as scratch:
            scratch = Path(scratch)
            executable = scratch / 'test-runtime-paths'
            subprocess.run(['c++', '-std=c++20', '-Wall', '-Wextra', '-Werror', '-I', str(root),
                            str(Path(__file__).with_suffix('.cpp')), '-o', str(executable)], check=True)
            subprocess.run([str(executable), str(scratch / 'run')], check=True)


if __name__ == '__main__':
    unittest.main()
