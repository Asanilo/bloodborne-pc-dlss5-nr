#!/usr/bin/env python3
"""Optional HLSL reproduction with an explicitly supplied native DXC; never installs output."""
import argparse
import hashlib
import json
from pathlib import Path
import subprocess

HERE = Path(__file__).resolve().parent
LOCK = HERE.parent / 'source-provenance.json'


def sha(path):
    return hashlib.sha256(Path(path).read_bytes()).hexdigest()


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--dxc', type=Path, required=True)
    p.add_argument('--dxc-sha256', required=True)
    p.add_argument('--output', type=Path, required=True)
    args = p.parse_args()
    lock = json.loads(LOCK.read_text())['shader']
    source = HERE / 'dlssnr-frozen.hlsl'
    if sha(source) != lock['historical_hlsl_sha256'] or sha(args.dxc) != args.dxc_sha256:
        raise ValueError('historical HLSL or explicit DXC identity mismatch')
    if not args.dxc.is_absolute() or not args.output.is_absolute() or args.output.exists():
        raise ValueError('absolute DXC and new output path required')
    # This command is optional; the regular Helper build uses the already locked
    # production SPIR-V. No Wine executable is launched by this recipe.
    if args.dxc.read_bytes()[:4] != b'\x7fELF':
        raise ValueError('native Linux DXC ELF required; Windows/Wine generation not allowed')
    args.output.parent.mkdir(parents=True, exist_ok=True)
    command = [str(args.dxc), *lock['original_dxc_flags'], str(source), '-Fo', str(args.output)]
    subprocess.run(command, check=True)
    actual = sha(args.output)
    if actual != lock['spv_sha256']:
        raise ValueError('DXC output preserved but not the frozen shader: ' + actual)
    print('EXACT_FROZEN_SPIRV_REPRODUCED', actual)


if __name__ == '__main__':
    main()
