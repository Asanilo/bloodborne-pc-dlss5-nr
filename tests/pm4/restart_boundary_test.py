#!/usr/bin/env python3
"""Exercise actual probe restart functions without a game, GPU, or real run.sh."""
from pathlib import Path
import json
import os
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]
VARS = ('BB_NR_NATIVE_ASYNC_SESSION', 'BB_NR_NATIVE_ASYNC_RUN_DIR',
        'BB_NR_NATIVE_ORIGIN_RUN_DIR')


class RestartBoundaryTest(unittest.TestCase):
    def test_actual_restart_boundary_and_exec_controls(self):
        source = (ROOT / 'src/probe.c').read_text()
        actual = source[source.index('const char *runtime_restart_block_reason(void)'):source.index('\nint main(')]
        harness = r'''
#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/syscall.h>
#include <fcntl.h>
''' + actual + r'''
int main(void) {
    setvbuf(stdout, NULL, _IONBF, 0); /* Match probe main. */
    int fd = open("/dev/null", O_RDONLY);
    if (fd < 3) return 91;
    char value[32];
    snprintf(value, sizeof(value), "%d", fd); setenv("TEST_FD", value, 1);
    snprintf(value, sizeof(value), "%d", getpid()); setenv("TEST_PID", value, 1);
    runtime_restart();
    printf("{\"returned\":true,\"flag\":%d,\"fd_open\":%s,\"pid_same\":%s}\n",
           runtime_restarting, fcntl(fd, F_GETFD) >= 0 ? "true" : "false",
           getpid() == atoi(getenv("TEST_PID")) ? "true" : "false");
    return 0;
}
'''
        with tempfile.TemporaryDirectory(prefix='nr-restart-boundary-') as tmp:
            tmp = Path(tmp)
            (tmp / 'harness.c').write_text(harness)
            subprocess.run(['cc', '-Wall', '-Wextra', '-Werror', str(tmp / 'harness.c'),
                            '-o', str(tmp / 'harness')], check=True)
            # Actual execlp resolves this harmless executable, never the game's run.sh.
            (tmp / 'bash').write_text('''#!/usr/bin/python3
import json, os, fcntl
try:
    fcntl.fcntl(int(os.environ['TEST_FD']), fcntl.F_GETFD)
    fd_open = True
except OSError:
    fd_open = False
print(json.dumps({'exec': True, 'argv_ok': __import__('sys').argv[1:] == ['run.sh'],
                  'pid_same': os.getpid() == int(os.environ['TEST_PID']), 'fd_open': fd_open}))
''')
            (tmp / 'bash').chmod(0o755)
            env = {k: v for k, v in os.environ.items() if k not in VARS}
            env['PATH'] = str(tmp)
            for variable in VARS:
                with self.subTest(blocking_variable=variable):
                    result = subprocess.run([str(tmp / 'harness')], env=dict(env, **{variable: 'isolated'}),
                                            capture_output=True, text=True, check=True)
                    self.assertEqual(json.loads(result.stdout),
                                     dict(returned=True, flag=0, fd_open=True, pid_same=True))
                    self.assertIn('restart blocked:', result.stderr)
                    self.assertNotIn('restarting through', result.stdout)
            for values in ({}, dict.fromkeys(VARS, '')):
                with self.subTest(non_nr=values):
                    result = subprocess.run([str(tmp / 'harness')], env=dict(env, **values),
                                            capture_output=True, text=True, check=True)
                    self.assertIn('Runtime: restarting through run.sh', result.stdout)
                    self.assertEqual(json.loads(result.stdout.splitlines()[-1]),
                                     dict(exec=True, argv_ok=True, pid_same=True, fd_open=False))
                    self.assertNotIn('restart blocked:', result.stderr)


if __name__ == '__main__':
    unittest.main()
