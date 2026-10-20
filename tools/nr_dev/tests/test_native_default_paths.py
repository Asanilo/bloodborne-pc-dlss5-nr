"""Compile real production headers with a link-only passwd fixture; no GPU."""
import subprocess
import tempfile
import unittest
from pathlib import Path


class NativeDefaultPaths(unittest.TestCase):
    def compile_and_run(self, source):
        root = Path(__file__).resolve().parents[3]
        with tempfile.TemporaryDirectory(prefix="nr-native-defaults-") as directory:
            scratch = Path(directory)
            executable = scratch / "cpu-test"
            subprocess.run(
                ["c++", "-std=c++20", "-Wall", "-Wextra", "-Werror", "-pthread",
                 "-I", str(root), str(root / source), "-Wl,--wrap=getpwuid_r",
                 "-o", str(executable)], check=True, timeout=60)
            subprocess.run([str(executable), str(scratch)], check=True, timeout=20)

    def test_relocation_and_confinement(self):
        self.compile_and_run("tools/nr_dev/tests/test_native_default_paths.cpp")

    def test_relocated_legacy_scene_frontend(self):
        self.compile_and_run("tools/nr_probe/test_native_async_scene_first_cpu.cpp")

    def test_relocated_identity_frontend(self):
        self.compile_and_run("tools/nr_probe/test_native_origin_identity_cpu.cpp")


if __name__ == "__main__":
    unittest.main()
