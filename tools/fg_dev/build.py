#!/usr/bin/env python3
"""Incrementally build this worktree and record Runner provenance without copying artifacts."""
import hashlib
from pathlib import Path
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[2]


def source():
    head = subprocess.check_output(['git', 'rev-parse', 'HEAD'], cwd=ROOT)
    diff = subprocess.check_output(['git', 'diff', '--binary', '--ignore-submodules=dirty', 'HEAD'], cwd=ROOT)
    return head, diff


def main():
    before = source()
    subprocess.run(['bash', 'scripts/dev-build.sh', *sys.argv[1:]], cwd=ROOT, check=True)
    if source() != before:
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


if __name__ == '__main__':
    main()
