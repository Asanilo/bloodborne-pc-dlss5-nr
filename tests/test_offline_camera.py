#!/usr/bin/env python3
"""Compile and exercise the production offline driver without guest/GPU work."""
import os
from pathlib import Path
import shlex
import subprocess
import tempfile

ROOT=Path(__file__).resolve().parents[1]

def test_offline_camera():
    with tempfile.TemporaryDirectory(prefix='bb-offline-camera-') as temporary:
        tmp=Path(temporary)
        log=tmp/'common/logging/log.h';log.parent.mkdir(parents=True)
        log.write_text('#pragma once\n#define LOG_INFO(...) ((void)0)\n')
        sdl=shlex.split(subprocess.check_output(['pkg-config','--cflags','--libs','sdl3'],text=True))
        binary=tmp/'camera'
        subprocess.run([os.environ.get('CXX','c++'),'-std=c++20','-Wall','-Wextra','-Werror',
            '-pthread','-I'+str(tmp),'-I'+str(ROOT),'-I'+str(ROOT/'gpu'),
            '-I'+str(ROOT/'gpu/shim'),'-I'+str(ROOT/'gpu/bbnet20/bbsc'),
            str(ROOT/'tests/test_offline_camera.cpp'),str(ROOT/'gpu/shim/bbport_cinematic_input.cpp'),
            '-o',str(binary),*sdl],check=True)
        subprocess.run([str(binary)],check=True,env={**os.environ,'SHADPS4_BBSC_CINEMATIC':'1'})

if __name__=='__main__':test_offline_camera()
