"""Versioned local runtime inputs. No build sources or historical approval inputs.

The default runner remains legacy. Explicit configurations pin their own complete
GE/sysroot/NGX file inventories, the private seed and selected runtime artifacts.
They do not relocate ROOT/runs or grant permission to start a GPU workload.
"""
import hashlib
import json
import os
import re
from pathlib import Path

SCHEMA = 'nr-local-runtime-inputs-v1'
PINS_SCHEMA = 'nr-runtime-input-pins-v1'
HELPER_PROTOCOL = {'runtime_model_schema': 'nr-runtime-model-v1',
                   'job_model_schema': 'nr-runtime-job-model-v1',
                   'wire_version': 1, 'interface_revision': 3}


def need(value, reason):
    if not value:
        raise ValueError('runtime inputs: ' + reason)


def sha(path):
    digest = hashlib.sha256()
    with Path(path).open('rb') as stream:
        for block in iter(lambda: stream.read(4 * 1024 * 1024), b''):
            digest.update(block)
    return digest.hexdigest()


def canonical(value, directory=False):
    need(isinstance(value, str) and value and '\x00' not in value
         and '\n' not in value and ':' not in value, 'safe absolute path required')
    path = Path(value)
    need(path.is_absolute() and path.resolve() == path and not path.is_symlink(),
         'canonical path required: ' + value)
    need(path.is_dir() if directory else path.is_file(), 'missing input: ' + value)
    return path


def read(path):
    def unique(pairs):
        result = {}
        for key, value in pairs:
            need(key not in result, 'duplicate JSON key ' + key)
            result[key] = value
        return result
    return json.loads(Path(path).read_text(), object_pairs_hook=unique)


def exact(value, keys, reason):
    need(isinstance(value, dict) and set(value) == set(keys), reason)


def digest(value):
    need(isinstance(value, str) and re.fullmatch('[0-9a-f]{64}', value),
         'full SHA256 required')
    return value


def files_under(root):
    """Follow legitimate runtime directory links, rejecting loops/special files."""
    out = set()
    def visit(path, ancestors):
        real = path.resolve(strict=True)
        if path.is_dir():
            need(real not in ancestors, 'runtime directory symlink loop')
            for child in path.iterdir():
                visit(child, ancestors | {real})
        else:
            need(path.is_file(), 'runtime special file refused: ' + str(path))
            out.add(str(path))
    visit(root, set())
    return out


def verify_pins(path, expected_sha, required, trees):
    canonical(str(path))
    need(sha(path) == digest(expected_sha), 'pins manifest SHA drift')
    record = read(path)
    exact(record, ('schema', 'pins'), 'invalid pins manifest')
    need(record['schema'] == PINS_SCHEMA and isinstance(record['pins'], list)
         and record['pins'], 'invalid runtime pins schema')
    pins = {}
    for pin in record['pins']:
        exact(pin, ('path', 'real_path', 'bytes', 'sha256'), 'invalid runtime pin')
        name = pin['path']
        need(isinstance(name, str) and Path(name).is_absolute()
             and str(Path(name)) == name and '..' not in Path(name).parts
             and name not in pins, 'duplicate/unsafe pin path')
        source = Path(name)
        need(source.is_file() and str(source.resolve()) == pin['real_path'],
             'runtime pin identity drift: ' + name)
        need(type(pin['bytes']) is int and source.stat().st_size == pin['bytes']
             and sha(source) == digest(pin['sha256']), 'runtime pin drift: ' + name)
        pins[name] = pin
    for tree in trees:
        need(files_under(tree) <= pins.keys(), 'runtime tree has unpinned files: ' + str(tree))
    need({str(p) for p in required} <= pins.keys(), 'required runtime resource not pinned')
    return record


def load(path, root):
    path = canonical(str(path))
    need(path.parent == root / 'localinputs', 'config must be in ROOT/localinputs')
    config = read(path)
    exact(config, ('schema', 'assets', 'seed', 'ge_files', 'sysroot_lib', 'helper',
                   'forwarder', 'model', 'system_ngx', 'ngx_dir', 'pins'), 'invalid local config fields')
    need(config['schema'] == SCHEMA, 'unsupported local config version')
    exact(config['assets'], ('path', 'access'), 'invalid asset reference')
    need(config['assets']['access'] == 'read-only', 'assets must be referenced read-only')
    directories = {key: canonical(config[key], True)
                   for key in ('seed', 'ge_files', 'sysroot_lib', 'ngx_dir')}
    directories['assets'] = canonical(config['assets']['path'], True)
    # No configured input may alias the mutable session workspace.
    runs = root / 'runs'
    for directory in directories.values():
        need(not directory.is_relative_to(runs) and not runs.is_relative_to(directory),
             'runtime input overlaps runs')
    exact(config['helper'], ('path', 'manifest_sha256'), 'invalid Helper reference')
    helper = canonical(config['helper']['path'])
    need(helper.is_relative_to(root / 'build/helper-runtime-model')
         and helper.name == 'wine_runtime_model.exe', 'isolated Helper required')
    manifest_path = canonical(str(helper.parent / 'build.json'))
    need(sha(manifest_path) == digest(config['helper']['manifest_sha256']),
         'Helper manifest SHA drift')
    manifest = read(manifest_path)
    need(isinstance(manifest, dict), 'invalid Helper manifest')
    need(manifest.get('schema') == 'nrbridge-helper-build-v1'
         and manifest.get('status') == 'BUILD_ONLY_PASS'
         and manifest.get('artifact') == str(helper)
         and manifest.get('artifact_sha256') == sha(helper), 'Helper artifact identity drift')
    protocol = manifest.get('protocol', {})
    need(isinstance(protocol, dict), 'invalid Helper protocol manifest')
    need(all(type(protocol.get(k)) is type(v) and
             (protocol[k] in (3, 4) if k == 'interface_revision' else protocol[k] == v)
             for k, v in HELPER_PROTOCOL.items()), 'Helper protocol/version mismatch')
    exports = protocol.get('exports')
    need(isinstance(exports, dict) and set(exports) == {'native_runtime_model.h', 'pre_sr_wire.h', 'pre_sr_spec.h', 'effective-spec.json'}
         and digest(protocol.get('export_manifest_sha256'))
         and all(isinstance(k, str) and digest(v) for k, v in exports.items()),
         'Helper protocol export identities required')
    files = {key: canonical(config[key]) for key in ('forwarder', 'model', 'system_ngx')}
    exact(config['pins'], ('path', 'sha256'), 'invalid runtime pins reference')
    pins = canonical(config['pins']['path'])
    need(pins.parent == root / 'localinputs', 'pins must be in ROOT/localinputs')
    seed = directories['seed']
    seed_manifest = canonical(str(seed / 'manifest.json'))
    seed_record = read(seed_manifest)
    need(isinstance(seed_record, dict) and isinstance(seed_record.get('files'), list) and seed_record['files'],
         'private seed inventory required')
    savedata = seed / 'savedata'
    inventory = set()
    for item in seed_record['files']:
        need(isinstance(item, dict) and {'relative', 'sha256'} <= item.keys(),
             'invalid private seed inventory entry')
        relative = item['relative']
        need(isinstance(relative, str) and relative and not Path(relative).is_absolute()
             and '..' not in Path(relative).parts and relative not in inventory,
             'invalid seed inventory path')
        source = canonical(str(savedata / relative))
        need(sha(source) == digest(item['sha256']), 'private seed SHA drift')
        inventory.add(relative)
    need(files_under(savedata) == {str(savedata / x) for x in inventory},
         'private seed inventory mismatch')
    ge = directories['ge_files']
    required = [helper, manifest_path, seed_manifest, directories['assets'] / 'eboot.bin',
                *files.values(), *(savedata / x for x in inventory),
                ge / 'lib/wine/x86_64-unix/wine', ge / 'lib/wine/x86_64-unix/wine-preloader',
                directories['ngx_dir'] / '_nvngx.dll', directories['ngx_dir'] / 'nvngx.dll']
    verify_pins(pins, config['pins']['sha256'], required,
                [ge, directories['sysroot_lib'], directories['ngx_dir']])
    return {'config': config, 'path': str(path), 'sha256': sha(path),
            'pins_path': str(pins), 'pins_sha256': sha(pins),
            'helper_build': {'path': str(manifest_path), 'sha256': sha(manifest_path),
                             'artifact_sha256': sha(helper)}}


def helper_config(binding):
    c = binding['config']
    return {'schema': 'nrbridge-helper-runtime-v1',
            **{k: c[k] for k in ('model', 'forwarder', 'ngx_dir', 'ge_files')}}


def native_capability(build):
    # This is an opt-in compatibility marker in the already SHA-checked library,
    # not a permission proof. Old validated Native builds remain legacy-only.
    need(b'NRBRIDGE_NATIVE_HELPER_RUNTIME_CONFIG_V1' in (build / 'gpu/libbbgpu.so').read_bytes(),
         'Native build does not support explicit runtime inputs; use legacy mode')


def daily_capability(build, binding):
    need(b'NRBRIDGE_NATIVE_DAILY_RING64_V2' in (build / 'gpu/libbbgpu.so').read_bytes(),
         'Native build does not support the daily ring64 transport')
    proof = binding['helper_build']
    path = canonical(proof['path'])
    need(sha(path) == proof['sha256'], 'daily Helper manifest SHA drift')
    protocol = read(path).get('protocol', {})
    need(type(protocol.get('interface_revision')) is int and protocol['interface_revision'] == 4
         and protocol.get('runtime_model_schema') == HELPER_PROTOCOL['runtime_model_schema']
         and protocol.get('job_model_schema') == HELPER_PROTOCOL['job_model_schema']
         and type(protocol.get('wire_version')) is int and protocol['wire_version'] == 1,
         'daily Helper requires interface revision 4 and existing model/wire schemas')


def recheck(binding, root):
    current = load(binding['path'], root)
    need(current == binding, 'session runtime input binding drift')
    return current
