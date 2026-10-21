#!/usr/bin/env python3
"""Build/replay actual cinematic.cpp using SDL3; no window, GPU or game is created."""
import os
from pathlib import Path
import shlex
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]

def test_cinematic_input():
    with tempfile.TemporaryDirectory(prefix='bb-cinematic-') as temporary:
        tmp = Path(temporary)
        log = tmp / 'common/logging/log.h'
        log.parent.mkdir(parents=True)
        log.write_text('#pragma once\n#define LOG_INFO(...) ((void)0)\n')
        sdl = shlex.split(subprocess.check_output(['pkg-config', '--cflags', '--libs', 'sdl3'], text=True))
        for tree, port in ((tree, port) for tree in ("bbnet20/bbsc", "bbnet/shadps4")
                           for port in (True, False)):
            binary = tmp / (tree.replace('/', '-') + ('-port' if port else '-upstream'))
            command = [os.environ.get('CXX', 'c++'), '-std=c++20', '-Wall', '-Wextra', '-Werror',
                       '-pthread', '-I' + str(tmp), '-I' + str(ROOT),
                       '-I' + str(ROOT / 'gpu' / tree),
                       str(ROOT / 'tests/test_cinematic_input.cpp'),
                       str(ROOT / 'gpu' / tree / 'core/bbsc_cinematic.cpp'),
                       '-o', str(binary), *sdl]
            if port:
                command.append('-DBBNET_PORT_VERSION=20')
            subprocess.run(command, check=True)
            subprocess.run([str(binary)], check=True, env={**os.environ, 'SHADPS4_BBSC_CINEMATIC': '1'})

if __name__ == '__main__':
    test_cinematic_input()
    print('PASS: cinematic ownership, native epoch cancellation, F6 port / F1 upstream')
