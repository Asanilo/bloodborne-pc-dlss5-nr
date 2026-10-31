"""Exercise the generated entry in real shells without Steam, audio or GPU work."""
from paths import ROOT
import os
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest

sys.path.insert(0, str(ROOT / 'scripts'))
from steam_environment import guard_shell, MARKER


class SteamEnvironmentTests(unittest.TestCase):
    def run_entry(self, shell, preload):
        with tempfile.TemporaryDirectory() as directory:
            script = Path(directory) / 'entry.sh'
            script.write_text(guard_shell(
                '#!/bin/sh\nset -eu\n'
                '/bin/true\n/bin/true\n'
                'printf "%s\\n" "${LD_PRELOAD-unset}" "$#" "$@"\n'))
            env = dict(os.environ)
            if preload is None:
                env.pop('LD_PRELOAD', None)
            else:
                env['LD_PRELOAD'] = preload
            return subprocess.run([shell, str(script), 'with spaces', '*:literal'],
                                  env=env, text=True, capture_output=True, check=True)

    def test_overlay_removed_before_child_exec_and_arguments_preserved(self):
        for shell in ('/bin/sh', '/bin/bash'):
            with self.subTest(shell=shell):
                result = self.run_entry(shell, '/missing/ubuntu12_32/gameoverlayrenderer.so')
                self.assertEqual(result.stdout.splitlines(), ['unset', '2', 'with spaces', '*:literal'])
                # The first interpreter's loader runs before any shell code. Its
                # single warning is unavoidable here; the children must be clean.
                self.assertEqual(result.stderr.count('cannot be preloaded'), 1)

    def test_mixed_separators_keep_unrelated_valid_preload(self):
        result = self.run_entry('/bin/sh', '/missing/gameoverlayrenderer.so: libm.so.6')
        self.assertEqual(result.stdout.splitlines()[0], 'libm.so.6')
        self.assertEqual(result.stderr.count('cannot be preloaded'), 1)

    def test_normal_launch_has_no_warnings(self):
        for preload in (None, '', 'libm.so.6'):
            with self.subTest(preload=preload):
                result = self.run_entry('/bin/sh', preload)
                self.assertEqual(result.stderr, '')

    def test_repeated_upgrade_is_idempotent(self):
        script = guard_shell('#!/bin/sh\nexec "$@"\n')
        self.assertEqual(guard_shell(script), script)
        self.assertEqual(script.count(MARKER), 1)


if __name__ == '__main__':
    unittest.main()
