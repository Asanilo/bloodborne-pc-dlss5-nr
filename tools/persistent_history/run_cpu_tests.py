#!/usr/bin/env python3
"""Run persistent-history Python and C++ regressions without a Vulkan device.

Usage: python3 tools/persistent_history/run_cpu_tests.py
Requires Python 3 and a C++20 compiler (CXX, or c++). Discovers test_*.py and
test_*.cpp in this directory; all compiled artifacts live in a temporary folder.
These tests establish CPU contracts only, not GPU or NVIDIA visual acceptance.
"""
import os
import pathlib
import shlex
import subprocess
import sys
import tempfile


DIRECTORY = pathlib.Path(__file__).resolve().parent
ROOT = DIRECTORY.parents[1]


def run(command):
    print('+ ' + shlex.join(map(str, command)), flush=True)
    try:
        return subprocess.run(command, cwd=ROOT).returncode == 0
    except OSError as error:
        print(f'Cannot run {command[0]}: {error}', file=sys.stderr)
        return False


def main():
    failed = []
    if not run([sys.executable, '-m', 'unittest', 'discover', '-s',
                str(DIRECTORY), '-p', 'test_*.py', '-v']):
        failed.append('Python regressions')

    compiler = shlex.split(os.environ.get('CXX', 'c++'))
    if not compiler:
        print('CXX must name a C++20 compiler', file=sys.stderr)
        return 1
    sources = sorted(DIRECTORY.glob('test_*.cpp'))
    with tempfile.TemporaryDirectory(prefix='nr-history-cpu-') as temporary:
        for source in sources:
            binary = pathlib.Path(temporary) / source.stem
            if not run([*compiler, '-std=c++20', '-I', str(ROOT / 'gpu/render'),
                        str(source), '-o', str(binary)]):
                failed.append(source.name + ' (compile)')
            elif not run([str(binary)]):
                failed.append(source.name + ' (run)')

    if failed:
        print('CPU regressions failed: ' + ', '.join(failed), file=sys.stderr)
        return 1
    print(f'PASS persistent-history Python suite and {len(sources)} C++ binaries; '
          'CPU only, no GPU acceptance')
    return 0


if __name__ == '__main__':
    sys.exit(main())
