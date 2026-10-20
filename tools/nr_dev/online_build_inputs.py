"""Validate an optional prepared online sysroot; never installs packages or starts networking."""
import hashlib
import json
import os
from pathlib import Path


def verify(directory):
    root = Path(directory)
    if not root.is_absolute() or root.resolve() != root or not root.is_dir():
        raise ValueError('canonical online dependency root required')
    manifest = root / 'bbport-online-inputs.json'
    if manifest.is_symlink():
        raise ValueError('regular online dependency manifest required')
    record = json.loads(manifest.read_text())
    actual = {}
    for path in root.rglob('*'):
        if path == manifest:
            continue
        relative = str(path.relative_to(root))
        if path.is_symlink():
            if not path.resolve(strict=True).is_relative_to(root):
                raise ValueError('online dependency symlink escape: ' + relative)
            actual[relative] = {'link': os.readlink(path)}
        elif path.is_file():
            actual[relative] = {'sha256': hashlib.sha256(path.read_bytes()).hexdigest()}
    if not actual or actual != record['files']:
        raise ValueError('online dependency inventory/content drift')
    return {'root': str(root), 'manifest_sha256': hashlib.sha256(manifest.read_bytes()).hexdigest(),
            'files': actual, 'packages': record.get('packages', [])}


if __name__ == '__main__':
    import sys
    print(json.dumps(verify(sys.argv[1]), sort_keys=True))
