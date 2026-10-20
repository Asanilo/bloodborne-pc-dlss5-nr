#!/usr/bin/env python3
"""Check/recreate the embedded leaf stub from its assembly source; CPU build only."""
import argparse
import hashlib
import os
from pathlib import Path
import re
import subprocess
import tempfile


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--write', action='store_true', help='update the embedded array and slot offsets')
    args = parser.parse_args()
    root = Path(__file__).resolve().parents[1]
    source = root / 'gpu/shim/native_mouse_stub_template.S'
    target = root / 'gpu/shim/bbport_mouse_look.cpp'
    with tempfile.TemporaryDirectory(prefix='bb-native-mouse-stub-') as temporary:
        obj = Path(temporary) / 'stub.o'
        binary = Path(temporary) / 'stub.bin'
        subprocess.run(['clang', '-c', str(source), '-o', str(obj)], check=True)
        relocations = subprocess.check_output(['readelf', '--relocs', str(obj)], text=True,
                                               env=dict(os.environ, LC_ALL='C'))
        if 'There are no relocations' not in relocations:
            raise SystemExit('copied stub must have no unresolved relocations')
        subprocess.run(['objcopy', '-O', 'binary', '--only-section=.text', str(obj), str(binary)], check=True)
        generated = binary.read_bytes()
    text = target.read_text()
    array = re.search(r'constexpr unsigned char kStubTemplate\[\] = \{(.*?)\n\};', text, re.S)
    if not array:
        raise SystemExit('missing embedded stub array')
    embedded = bytes(int(value, 16) for value in re.findall(r'0x([0-9a-fA-F]{2})\b', array[1]))
    slots = {'kStubMailboxSlot': 0x1122334455667788,
             'kStubContinuationSlot': 0x8877665544332211}
    offsets = {}
    for name, marker in slots.items():
        needle = marker.to_bytes(8, 'little')
        if generated.count(needle) != 1:
            raise SystemExit('slot marker missing or ambiguous: ' + name)
        offsets[name] = generated.index(needle)
    if args.write:
        rows = ['    ' + ', '.join(f'0x{value:02x}' for value in generated[start:start + 16]) + ','
                for start in range(0, len(generated), 16)]
        text = text[:array.start(1)] + '\n' + '\n'.join(rows) + text[array.end(1):]
        for name, offset in offsets.items():
            text, count = re.subn(r'(constexpr std::size_t ' + name + r' = )\d+(;)',
                                  lambda match: match[1] + str(offset) + match[2], text)
            if count != 1:
                raise SystemExit('slot constant missing: ' + name)
        target.write_text(text)
    else:
        if embedded != generated:
            raise SystemExit('embedded stub differs from assembly; regenerate and rerun state tests')
        for name, offset in offsets.items():
            match = re.search(r'constexpr std::size_t ' + name + r' = (\d+);', text)
            if not match or int(match[1]) != offset:
                raise SystemExit('embedded slot offset differs: ' + name)
    print('PASS: assembly == embedded stub; bytes=' + str(len(generated)) +
          ' sha256=' + hashlib.sha256(generated).hexdigest())


if __name__ == '__main__':
    main()
