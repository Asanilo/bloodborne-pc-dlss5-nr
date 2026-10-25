#!/usr/bin/env python3
"""Seal a private runtime config and a separate runtime-only pins manifest.

This only reads inputs and creates new files in ROOT/localinputs; it never starts
Wine/GPU, changes legacy pins, copies games/models/saves or grants launch safety.
"""
import argparse
import json
import os
from pathlib import Path
import runtime_inputs as inputs

def workspace_root():
    value = os.environ.get('NRBRIDGE_HOME', str(Path('/home/arinp/Code/nr-home')))
    path = Path(value)
    inputs.need(bool(value) and not any(c in value for c in ('\x00', '\n', ':'))
                and path.is_absolute() and str(path) == value and path.resolve() == path
                and path not in (Path('/'), Path.home()),
                'NRBRIDGE_HOME requires a dedicated canonical absolute directory')
    return path


ROOT = workspace_root()


def seal(draft, output, root=ROOT, extra_runtime_files=()):
    output = Path(output)
    inputs.need(output.is_absolute() and output.parent == root / 'localinputs'
                and output.parent.resolve() == output.parent, 'output must be in canonical ROOT/localinputs')
    pins_path = output.with_name(output.stem + '-pins.json')
    inputs.need(not output.exists() and not output.is_symlink()
                and not pins_path.exists() and not pins_path.is_symlink(), 'refusing to overwrite local inputs')
    config = inputs.read(inputs.canonical(str(draft)))
    inputs.need(config.get('schema') in (inputs.SCHEMA, inputs.PRESET_SCHEMA), 'unsupported config draft')
    resources = {str(inputs.canonical(config['helper']['path'])),
                 str(inputs.canonical(str(Path(config['helper']['path']).parent / 'build.json'))),
                 str(inputs.canonical(config['model'])), str(inputs.canonical(config['forwarder'])),
                 str(inputs.canonical(config['system_ngx'])),
                 str(inputs.canonical(str(Path(config['assets']['path']) / 'eboot.bin'))),
                 str(inputs.canonical(str(Path(config['seed']) / 'manifest.json')))}
    for key in ('ge_files', 'sysroot_lib', 'ngx_dir'):
        resources |= inputs.files_under(inputs.canonical(config[key], True))
    resources |= inputs.files_under(inputs.canonical(str(Path(config['seed']) / 'savedata'), True))
    for path in extra_runtime_files:
        # System runtime symlinks are pinned by path, resolved identity and bytes.
        path = Path(path)
        inputs.need(path.is_absolute() and path.is_file(), 'regular extra runtime input required')
        resources.add(str(path))
    pins = []
    for name in sorted(resources):
        path = Path(name)
        pins.append({'path': name, 'real_path': str(path.resolve()),
                     'bytes': path.stat().st_size, 'sha256': inputs.sha(path)})
    config['pins'] = {'path': str(pins_path), 'sha256': ''}
    created = []
    try:
        with pins_path.open('x') as stream:
            created.append(pins_path)
            json.dump({'schema': inputs.PINS_SCHEMA, 'pins': pins}, stream, indent=2)
            stream.write('\n')
        config['pins']['sha256'] = inputs.sha(pins_path)
        with output.open('x') as stream:
            created.append(output)
            json.dump(config, stream, indent=2)
            stream.write('\n')
        binding = inputs.load(output, root)
    except BaseException:
        # Remove only files exclusively created by this call, never old inputs.
        for path in reversed(created):
            path.unlink()
        raise
    return binding


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--draft', help='private filled-in local config template')
    parser.add_argument('--output', help='new ROOT/localinputs config path')
    parser.add_argument('--extra-runtime-file', action='append', default=[],
                        help='additional actual system runtime file, e.g. the pinned NGX driver library')
    parser.add_argument('--check', help='read-only CPU validation of an existing local config')
    args = parser.parse_args()
    if args.check:
        if args.draft or args.output or args.extra_runtime_file:
            parser.error('--check cannot be mixed with sealing options')
        binding = inputs.load(args.check, ROOT)
    else:
        if not args.draft or not args.output:
            parser.error('--draft and --output required for sealing')
        binding = seal(args.draft, args.output, extra_runtime_files=args.extra_runtime_file)
    print(json.dumps({k: binding[k] for k in ('path', 'sha256', 'pins_path', 'pins_sha256')}, indent=2))


if __name__ == '__main__':
    main()
