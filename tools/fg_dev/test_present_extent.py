"""Exercise the production present-size fit without a Vulkan instance or device."""
from pathlib import Path
import subprocess
import tempfile
import unittest


class PresentExtentTests(unittest.TestCase):
    def test_thin_zero_and_restored_surfaces(self):
        root = Path(__file__).resolve().parents[2]
        with tempfile.TemporaryDirectory() as directory:
            executable = str(Path(directory) / 'present-extent')
            subprocess.run(['c++', '-std=c++20', '-O2', '-UNDEBUG', '-Wall', '-Wextra',
                            '-Werror', '-I', str(root), str(root/'tests/test_present_extent.cpp'),
                            '-o', executable], check=True)
            subprocess.run([executable], check=True)


if __name__ == '__main__':
    unittest.main()
