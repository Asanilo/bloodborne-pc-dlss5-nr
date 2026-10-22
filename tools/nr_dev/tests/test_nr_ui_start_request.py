"""Compile and exercise the real UI intent publisher; no game or GPU execution."""
import subprocess
import tempfile
import unittest
from pathlib import Path


class NRUiStartRequest(unittest.TestCase):
    def test_actual_publisher(self):
        root = Path(__file__).resolve().parents[3]
        with tempfile.TemporaryDirectory(prefix="nr-ui-start-") as directory:
            scratch = Path(directory)
            executable = scratch / "cpu-test"
            subprocess.run(
                ["c++", "-std=c++20", "-Wall", "-Wextra", "-Werror",
                 "-Wno-misleading-indentation", "-pthread", "-I", str(root),
                 "-I", str(root / "gpu/third_party/nlohmann/include"),
                 str(root / "tools/nr_dev/tests/test_nr_ui_start_request.cpp"),
                 str(root / "gpu/render/nr_runtime_control.cpp"),
                 "-o", str(executable)], check=True, timeout=60)
            subprocess.run([str(executable), str(scratch)], check=True, timeout=20)


if __name__ == "__main__":
    unittest.main()
