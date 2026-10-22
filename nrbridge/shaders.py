"""Frozen production Encode/Resolve SPIR-V and reproducible host header."""
import hashlib
from pathlib import Path
import re
import subprocess

ROOT = Path(__file__).resolve().parents[1]
SPV = ROOT / 'nrbridge/shaders/DlssNr_Shader_Vk.spv'
LEGACY_HEADER = SPV.with_suffix('.h')
SPV_SHA = '7b44aeb1876f61c8c102979225edb10287189d1d8eddfa0a65186cf026c80239'


def header(data):
    # Preserve the original production array name; formatting has no bytecode effect.
    rows = [', '.join('0x%02x' % x for x in data[i:i+12]) for i in range(0, len(data), 12)]
    return ('#pragma once\n\ninline static const unsigned char dlssnr_spv[] = {\n    '
            + ',\n    '.join(rows) + '\n};\n').encode()


def validate():
    data = SPV.read_bytes()
    original = bytes(int(x, 16) for x in re.findall(r'0x([0-9a-fA-F]{2})', LEGACY_HEADER.read_text()))
    if hashlib.sha256(data).hexdigest() != SPV_SHA or original != data:
        raise ValueError('production shader bytecode/header drift')
    subprocess.run(['/usr/bin/spirv-val', '--target-env', 'vulkan1.0', str(SPV)], check=True)
    if bytes(int(x, 16) for x in re.findall(rb'0x([0-9a-fA-F]{2})', header(data))) != data:
        raise ValueError('generated header bytes differ')
    return data
