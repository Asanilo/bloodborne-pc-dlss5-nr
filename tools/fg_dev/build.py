#!/usr/bin/env python3
"""Incrementally build this worktree and record Runner provenance without copying artifacts."""
import fcntl
import hashlib
import importlib.util
import json
from pathlib import Path
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[2]


def source():
    head = subprocess.check_output(['git', 'rev-parse', 'HEAD'], cwd=ROOT)
    diff = subprocess.check_output(['git', 'diff', '--binary', '--ignore-submodules=dirty', 'HEAD'], cwd=ROOT)
    return head, diff


def main():
    # Protect mutable out/ from a live managed GPU session while replacing artifacts.
    # CPU builds remain allowed during a boot HOLD; this does not admit GPU work.
    with open('/tmp/nrbridge-poc-gpu.lock','a') as lock:
        fcntl.flock(lock,fcntl.LOCK_EX | fcntl.LOCK_NB)
        build()


def build():
    before = source()
    spec=importlib.util.spec_from_file_location('fg_native_protocol',ROOT/'tools/nr_dev/check_protocol_export.py')
    protocol=importlib.util.module_from_spec(spec);spec.loader.exec_module(protocol)
    proof=protocol.checked(ROOT)
    subprocess.run(['bash', 'scripts/dev-build.sh', *sys.argv[1:]], cwd=ROOT, check=True)
    if source() != before or protocol.checked(ROOT)!=proof:
        raise RuntimeError('source changed during build; rerun before recording provenance')
    build = ROOT / 'out'
    libraries = list(build.glob('libnvidia-ngx-dlss.so.*'))
    if len(libraries) != 1:
        raise RuntimeError('one Native NGX library required')
    names = ['bb-probe', 'gpu/libbbgpu.so', 'libbbport_dlss.so', libraries[0].name]
    rows = []
    for name in names:
        with (build / name).open('rb') as stream:
            digest = hashlib.file_digest(stream, 'sha256').hexdigest()
        rows.append(f'{digest}  {name}\n')
    (build / 'source-head.txt').write_bytes(before[0])
    (build / 'source-diff.patch').write_bytes(before[1])
    (build / 'artifacts.sha256').write_text(''.join(rows))
    cache=(build/'gpu/CMakeCache.txt').read_text()
    options={line.split('=',1)[0]:line.split('=',1)[1] for line in cache.splitlines()
             if line.startswith(('CMAKE_BUILD_TYPE:','BB_LTO:','BB_PGO:','BB_ONLINE_DEPS_ROOT:'))}
    (build/'build-inputs.json').write_text(json.dumps(dict(
        schema='bbport-fg-native-build-inputs-v1',protocol=proof,cmake_options=options,
        cmake_cache_sha256=hashlib.sha256(cache.encode()).hexdigest()),indent=2)+'\n')


if __name__ == '__main__':
    main()
