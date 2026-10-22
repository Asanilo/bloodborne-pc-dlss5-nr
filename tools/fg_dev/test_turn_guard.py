"""CPU tests of the production optional FG turn fallback; no Vulkan/GPU work."""
from pathlib import Path
import subprocess
import tempfile
import unittest


class TurnGuardTests(unittest.TestCase):
    def test_fast_turn_recovery_wrap_and_invalid_axes(self):
        root = Path(__file__).resolve().parents[2]
        with tempfile.TemporaryDirectory() as directory:
            binary = str(Path(directory) / 'turn-guard')
            subprocess.run(['c++', '-std=c++20', '-O1', '-UNDEBUG',
                            '-Wall', '-Wextra', '-Werror',
                            '-fsanitize=undefined,float-cast-overflow',
                            '-fno-sanitize-recover=all', '-I', str(root),
                            str(root / 'tests/test_frame_generation_turn_guard.cpp'),
                            '-o', binary], check=True)
            subprocess.run([binary], check=True)


if __name__ == '__main__':
    unittest.main()
