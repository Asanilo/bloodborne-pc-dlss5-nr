"""Run the real launcher wrapper with stand-ins for GTK discovery and nix-shell."""
from paths import ROOT
import json
import os
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile
import unittest


class LauncherWrapperTests(unittest.TestCase):
    def launch(self, system_gtk):
        with tempfile.TemporaryDirectory(prefix="bb launcher '") as directory:
            root = Path(directory)
            launcher = root / "launcher with spaces"
            launcher.mkdir()
            wrapper = launcher / "bb-launcher.sh"
            shutil.copyfile(ROOT / "launcher/bb-launcher.sh", wrapper)
            tools = root / "bin"
            tools.mkdir()
            python = tools / "python3"
            python.write_text(f"#!{sys.executable}\n"
                              "import json, os, sys\n"
                              "if sys.argv[1] == '-c':\n"
                              "    sys.exit(0 if os.environ['TEST_GTK'] == '1' else 1)\n"
                              "print(json.dumps(sys.argv[1:]))\n")
            nix = tools / "nix-shell"
            nix.write_text(f"#!{sys.executable}\n"
                           "import subprocess, sys\n"
                           "assert sys.argv[2] == '--run'\n"
                           "sys.exit(subprocess.call(['bash', '-c', sys.argv[3]]))\n")
            for tool in (python, nix):
                tool.chmod(0o755)
            args = ["--play", "save folder", "", "$(touch injected)", "*", "quote'and\"double"]
            env = dict(os.environ, PATH=str(tools) + os.pathsep + os.environ["PATH"],
                       TEST_GTK="1" if system_gtk else "0")
            result = subprocess.run(["bash", str(wrapper), *args], cwd=root, env=env,
                                    capture_output=True, text=True, timeout=10, check=True)
            self.assertEqual(json.loads(result.stdout), [str(launcher / "bbport_launcher.py"), *args])
            self.assertFalse((root / "injected").exists())

    def test_system_gtk_preserves_arguments(self):
        self.launch(True)

    def test_nix_fallback_preserves_arguments_without_shell_expansion(self):
        self.launch(False)
