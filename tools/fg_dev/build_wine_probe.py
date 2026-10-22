#!/usr/bin/env python3
"""Build the finite Wine DLSSG probe with existing headers/compiler, no SDK copy."""
import argparse
import fcntl
import hashlib
import json
import os
from pathlib import Path
import subprocess

ROOT = Path(__file__).resolve().parents[2]
INPUTS = [
    'tools/fg_dev/wine_dlssg_probe.cpp',
    'tools/fg_dev/dlssg_snippet_bridge.cpp',
    'tools/fg_dev/dlssg_fixture.h',
    'tools/fg_dev/dlssg_evaluate_fixture.h',
    'tools/fg_dev/dlssg_stream.h',
    'tools/fg_dev/dlssg_wine_wake.h',
    'tools/fg_dev/dlssg_gpu_timing.h',
    'tools/fg_dev/dlssg_mailbox_export.h',
    'gpu/upscale/dlssg_gpu_contract.h',
    'nrbridge/runtime/transport.h',
    'gpu/upscale/dlssg_cpu_slot.h',
    'nrbridge/runtime/fixture_util.h',
    'gpu/third_party/nlohmann/include/nlohmann/json.hpp',
]


def identity(path):
    path = Path(path)
    with path.open('rb') as stream:
        digest = hashlib.file_digest(stream, 'sha256').hexdigest()
    return {'path': str(path.resolve()), 'bytes': path.stat().st_size, 'sha256': digest}


def source_identity():
    paths = [ROOT / name for name in INPUTS]
    paths += sorted((ROOT / 'out/deps/dlss-sdk/include').glob('*.h'))
    return {str(p.relative_to(ROOT)): identity(p) for p in paths}


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--compiler', default=os.environ.get('CXX_WINDOWS'), required=not os.environ.get('CXX_WINDOWS'))
    a = p.parse_args()
    # llvm-mingw chooses its target from the invoked symlink basename.
    compiler = Path(a.compiler).absolute()
    if not compiler.is_file():
        raise ValueError(f'compiler required: {compiler}')
    includes = [ROOT / 'out/deps/root/usr/include', ROOT / 'out/deps/dlss-sdk/include', ROOT / 'nrbridge/runtime', ROOT / 'gpu/third_party/nlohmann/include']
    for path in includes:
        if not path.is_dir():
            raise ValueError(f'existing dependency required: {path}')
    # Brief lock protects the mutable executable from overwrite during a GPU run.
    # Compilation itself performs no GPU work and creates no session receipt.
    with open('/tmp/nrbridge-poc-gpu.lock', 'a') as lock:
        fcntl.flock(lock, fcntl.LOCK_EX | fcntl.LOCK_NB)
        before = source_identity()
        flags = [str(compiler), '-std=c++17', '-static', '-O1', '-Wall', '-Wextra',
                 '-Wno-missing-field-initializers', '-Wno-unused-function']
        flags += ['-I' + str(path.resolve()) for path in includes]
        commands = []
        for name, source, extra in [
            ('wine-dlssg-probe.exe', INPUTS[0], []),
            ('nvngx.dll_dlssg.dll', INPUTS[1], ['-shared']),
        ]:
            target = ROOT / 'out' / name
            temp = target.with_name(name + '.building')
            command = flags + extra + [source, '-o', str(temp)]
            try:
                subprocess.run(command, cwd=ROOT, check=True)
                if source_identity() != before:
                    raise RuntimeError('source changed during build; rerun')
                temp.replace(target)
            finally:
                temp.unlink(missing_ok=True)
            commands.append(command)
        manifest = {'schema': 'dlssg-wine-probe-build-v1',
                    'head': subprocess.check_output(['git', 'rev-parse', 'HEAD'], cwd=ROOT, text=True).strip(),
                    'compiler': identity(compiler), 'inputs': before, 'commands': commands,
                    'artifacts': {n: identity(ROOT / 'out' / n) for n in ['wine-dlssg-probe.exe', 'nvngx.dll_dlssg.dll']}}
        (ROOT / 'out/m2-wine-probe-build.json').write_text(json.dumps(manifest, indent=2) + '\n')
        print('Built finite DLSSG probe and caller adapter; no generated-frame success claim.')


if __name__ == '__main__':
    main()
