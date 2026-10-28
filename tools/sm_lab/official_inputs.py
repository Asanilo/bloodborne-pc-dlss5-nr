#!/usr/bin/env python3
"""Prepare a private NRBRIDGE_HOME for the official daily runner.

Reuses the existing shared inputs by absolute reference (game, seed, GE, sysroot,
NGX, model, forwarder). The only copied pieces are the Helper build directory and
its manifest, whose recorded artifact path is rewritten to the private home.

usage: official_inputs.py [--home <dir>] [--name sm-B-v1]
"""
import argparse
import hashlib
import json
import shutil
import sys
from pathlib import Path

WT = Path('/home/arinp/Code/bloodborne_pc/.worktree/sm')
SHARED = Path('/home/arinp/Code/bloodborne_pc/.worktree/nrbridge/out')
ASSETS = Path('/home/arinp/bloodborne/CUSA03173')
SEED = Path('/home/arinp/Code/nr-home/seed/private-NRTest-seed')
GE = Path('/home/arinp/.local/share/Steam/compatibilitytools.d/GE-Proton11-7-x86_64/files')
SYSROOT = Path('/home/arinp/Code/bloodborne_pc/out/deps/root/usr/lib')
NGX_DIR = Path('/usr/lib/nvidia/wine')
SYSTEM_NGX = Path('/usr/lib/libnvidia-ngx.so.615.78.08')
MODEL = SHARED / 'nr-models/310.9.16-SF/nvngx_dlssnr.dll'
FORWARDER = SHARED / 'nr-dev/build/convergence/forwarder/78d663ed3116c1f5/nvngx.dll_dlssnr.dll'
HELPER_SRC = SHARED / 'nr-dev/build/helper-runtime-model/16bdc730da6029bd'


def sha(path):
    digest = hashlib.sha256()
    with Path(path).open('rb') as stream:
        for block in iter(lambda: stream.read(4 * 1024 * 1024), b''):
            digest.update(block)
    return digest.hexdigest()


def tree_files(root):
    """Mirror runtime_inputs.files_under: follow directory links, list regular files."""
    out = []

    def visit(path, ancestors):
        real = path.resolve(strict=True)
        if path.is_dir():
            if real in ancestors:
                raise SystemExit('runtime directory symlink loop: ' + str(path))
            for child in sorted(path.iterdir()):
                visit(child, ancestors | {real})
        else:
            if not path.is_file():
                raise SystemExit('runtime special file refused: ' + str(path))
            out.append(path)

    visit(Path(root), set())
    return out


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument('--home', default=str(WT / 'out/sm-lab/official'))
    parser.add_argument('--name', default='sm-B-v1')
    args = parser.parse_args()
    home = Path(args.home).resolve()
    home.mkdir(parents=True, exist_ok=True)
    (home / 'localinputs').mkdir(exist_ok=True)
    (home / 'runs').mkdir(exist_ok=True)

    helper_dir = home / 'build/helper-runtime-model' / HELPER_SRC.name
    if not helper_dir.exists():
        shutil.copytree(HELPER_SRC, helper_dir)
    helper = helper_dir / 'wine_runtime_model.exe'
    manifest_path = helper_dir / 'build.json'
    manifest = json.loads(manifest_path.read_text())
    manifest['artifact'] = str(helper)
    manifest_path.write_text(json.dumps(manifest, indent=2) + '\n')
    manifest_sha = sha(manifest_path)
    print('helper', helper, manifest_sha)

    config = {
        'schema': 'nr-local-runtime-inputs-v2',
        'assets': {'path': str(ASSETS), 'access': 'read-only'},
        'seed': str(SEED),
        'ge_files': str(GE),
        'sysroot_lib': str(SYSROOT),
        'helper': {'path': str(helper), 'manifest_sha256': manifest_sha},
        'forwarder': str(FORWARDER),
        'model': str(MODEL),
        'system_ngx': str(SYSTEM_NGX),
        'ngx_dir': str(NGX_DIR),
        'nr_preset': 'B',
        'pins': {'path': '', 'sha256': ''},
    }
    config_path = home / 'localinputs' / (args.name + '.json')
    pins_path = home / 'localinputs' / (args.name + '-pins.json')

    required = [helper, manifest_path, SEED / 'manifest.json', ASSETS / 'eboot.bin',
                FORWARDER, MODEL, SYSTEM_NGX,
                GE / 'lib/wine/x86_64-unix/wine', GE / 'lib/wine/x86_64-unix/wine-preloader',
                NGX_DIR / '_nvngx.dll', NGX_DIR / 'nvngx.dll']
    seed_record = json.loads((SEED / 'manifest.json').read_text())
    required += [SEED / 'savedata' / item['relative'] for item in seed_record['files']]

    pins = {}
    for root in (GE, SYSROOT, NGX_DIR):
        for path in tree_files(root):
            pins[str(path)] = (path, path.resolve())
    for path in required:
        pins.setdefault(str(path), (path, path.resolve()))

    records = []
    for name, (path, real) in sorted(pins.items()):
        if not Path(name).is_file():
            raise SystemExit('missing required input: ' + name)
        records.append({'path': name, 'real_path': str(real),
                        'bytes': Path(name).stat().st_size, 'sha256': sha(name)})
    pins_path.write_text(json.dumps({'schema': 'nr-runtime-input-pins-v1',
                                     'pins': records}, indent=1) + '\n')

    config['pins'] = {'path': str(pins_path), 'sha256': sha(pins_path)}
    config_path.write_text(json.dumps(config, indent=2) + '\n')
    print('config', config_path)
    print('pins', pins_path, len(records), 'files')

    sys.path.insert(0, str(WT / 'tools/nr_dev'))
    import runtime_inputs as ri
    binding = ri.load(config_path, home)
    print('VALIDATED', json.dumps({'sha256': binding['sha256'],
                                   'pins_sha256': binding['pins_sha256'],
                                   'helper_build': binding['helper_build']['sha256']}, indent=1))


if __name__ == '__main__':
    main()
