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
        variants=[(tree, port, False) for tree in ("bbnet20/bbsc", "bbnet/shadps4")
                  for port in (True, False)]+[("bbnet20/bbsc",True,True)]
        for tree, port, offline in variants:
            binary = tmp / (tree.replace('/', '-') + ('-offline' if offline else '-port' if port else '-upstream'))
            source=ROOT/'gpu/shim/bbport_cinematic_input.cpp' if offline else ROOT/'gpu'/tree/'core/bbsc_cinematic.cpp'
            command = [os.environ.get('CXX', 'c++'), '-std=c++20', '-Wall', '-Wextra', '-Werror',
                       '-pthread', '-I' + str(tmp), '-I' + str(ROOT),
                       '-I' + str(ROOT / 'gpu' / tree),
                       str(ROOT / 'tests/test_cinematic_input.cpp'),
                       str(source),
                       '-o', str(binary), *sdl]
            if port:
                command.append('-DBBNET_PORT_VERSION='+('1' if offline else '20'))
            if offline:
                command.append('-DBBPORT_OFFLINE_CINEMATIC')
            subprocess.run(command, check=True)
            subprocess.run([str(binary)], check=True, env={**os.environ, 'SHADPS4_BBSC_CINEMATIC': '1'})

if __name__ == '__main__':
    test_cinematic_input()
    print('PASS: cinematic ownership, native epoch cancellation, F6 port / F1 upstream')
