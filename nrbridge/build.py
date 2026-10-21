#!/usr/bin/env python3
"""Build the standalone current runtime-model Helper, CPU checks and protocol export."""
import argparse
import fcntl
import importlib.util
import json
import os
from pathlib import Path
import subprocess
import sys

from build_support import capture, clean_environment, compiler_inputs, sha
import shaders

HERE = Path(__file__).resolve().parent
ROOT = HERE.parent
DEV = Path(os.environ.get('NR_DEV_ROOT', str(ROOT / 'out/nr-dev')))
OUT = DEV / 'build/helper-runtime-model'
PREPARE = DEV / 'build/convergence/runtime-model-prepare'
spec = importlib.util.spec_from_file_location('protocol_export', HERE / 'protocol/export.py')
protocol = importlib.util.module_from_spec(spec)
spec.loader.exec_module(protocol)


def parser():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--llvm-root', type=Path, default=Path(os.environ.get('NR_LLVM_ROOT', str(ROOT / 'out/deps/llvm-mingw'))))
    p.add_argument('--vulkan-include', type=Path, default=Path(os.environ.get('NR_VULKAN_INCLUDE', str(ROOT / 'out/deps/root/usr/include'))))
    p.add_argument('--ngx-sdk-root', type=Path, default=Path(os.environ.get('NR_NGX_SDK_ROOT', str(ROOT / 'out/deps/ngx-sdk'))))
    p.add_argument('--json-root', type=Path, default=Path(os.environ.get('NR_JSON_ROOT', str(ROOT / 'out/deps/nlohmann'))))
    p.add_argument('--check-only', action='store_true')
    return p


def prepare(args):
    lock = json.loads((HERE / 'source-provenance.json').read_text())
    for row in lock['sources']:
        path = ROOT / row['target']
        if sha(path) != row['migrated_sha256']:
            raise ValueError('migrated source drift: ' + str(path))
    for path in (args.llvm_root, args.vulkan_include, args.ngx_sdk_root, args.json_root, DEV):
        if not path.is_absolute() or (path != DEV and not path.is_dir()):
            raise ValueError('existing absolute dependency root required: ' + str(path))
    compiler = args.llvm_root / 'bin/x86_64-w64-mingw32-clang++'
    if sha(compiler) != lock['compiler']['sha256']:
        raise ValueError('locked LLVM MinGW compiler wrapper drift')
    # Check the existing SDK/json locks against the selected independent roots.
    external = {}
    for original, expected in lock['external_dependency_pins'].items():
        path = Path(original)
        if original.startswith('vulkan/'):
            actual = args.vulkan_include / 'vulkan' / path.name
        elif path.name == 'json.hpp':
            actual = args.json_root / path.name
        else:
            actual = args.ngx_sdk_root / path.name
        if sha(actual) != expected:
            raise ValueError('locked SDK/json dependency drift: ' + str(actual))
        external[str(actual)] = expected
    shader = shaders.validate()
    sources = {str(p): sha(p) for p in sorted(HERE.rglob('*')) if p.is_file()
               and p.suffix in ('.cpp', '.h', '.py', '.json', '.hlsl', '.md')}
    for rel in (lock['shader']['production_spv'],
                'nrbridge/shaders/DlssNr_Common.h',
                'nrbridge/shaders/upstream-tools/build_precompiled_shader_vk.bat',
                'nrbridge/shaders/upstream-tools/create_header.py'):
        sources[str(ROOT / rel)] = sha(ROOT / rel)
    import hashlib
    source_identity = hashlib.sha256(json.dumps(sources, sort_keys=True).encode()).hexdigest()
    generated = PREPARE / source_identity[:16]
    exports = protocol.export(generated)
    header = generated / 'DlssNr_Shader_Vk.h'
    data = shaders.header(shader)
    if header.exists() and header.read_bytes() != data:
        raise ValueError('existing generated shader header drift')
    if not header.exists():
        header.write_bytes(data)
    exports['export_manifest_sha256'] = sha(generated / 'protocol-export.json')
    includes = ['-I' + str(p) for p in (args.vulkan_include, args.ngx_sdk_root,
                args.json_root, HERE / 'protocol', HERE / 'runtime', generated)]
    flags = ['-std=c++17', '-static', '-O1', '-Wall', '-Wextra',
             '-Wno-missing-field-initializers', '-Wno-unused-function', *includes]
    environment = clean_environment()
    dependencies, trace = compiler_inputs(compiler, flags, HERE / 'runtime/wine_runtime_model.cpp', ROOT, environment)
    dependencies.update(external)
    for tool in ('/usr/bin/c++', '/usr/bin/spirv-val', '/usr/bin/llvm-readobj', str(Path(sys.executable).resolve())):
        dependencies[tool] = sha(tool)
    host_flags = ['-std=c++17', '-O2', '-Wall', '-Wextra', '-Werror',
                  '-I' + str(args.json_root), '-I' + str(HERE / 'protocol'),
                  '-I' + str(HERE / 'runtime'), '-I' + str(generated)]
    for name in ('wire_check', 'test_protocol', 'test_runtime_config', 'test_daily_transport'):
        files, _ = compiler_inputs(Path('/usr/bin/c++'), host_flags, HERE / ('tests/' + name + '.cpp'), ROOT, environment)
        dependencies.update(files)
    inputs = {'schema': 'nrbridge-helper-build-inputs-v1', 'sources': sources,
              'dependencies': dict(sorted(dependencies.items())),
              'compiler': {'path': str(compiler), 'sha256': sha(compiler),
                           'version': capture([str(compiler), '--version'], ROOT, environment).stdout},
              'command_template': [str(compiler), *flags, str(HERE / 'runtime/wine_runtime_model.cpp'), '-o', 'OUTPUT_EXE'],
              'host_flags': host_flags, 'protocol': exports,
              'runtime_defaults': lock['runtime_defaults'],
              'shader': {'SPIRV': lock['shader'], 'SPIRV_to_header': 'PASS: exact shader byte array',
                         'HLSL_to_SPIRV': 'NOT_RUN: no Wine or replacement DXC',
                         'generated_header_sha256': sha(header)},
              'selected_roots': {key: str(getattr(args, key)) for key in ('llvm_root', 'vulkan_include', 'ngx_sdk_root', 'json_root')}}
    digest = hashlib.sha256(json.dumps(inputs, sort_keys=True).encode()).hexdigest()
    return inputs, digest, generated, environment, trace


def main():
    args = parser().parse_args()
    inputs, digest, generated, environment, trace = prepare(args)
    if args.check_only:
        print('INPUT_CHECK_PASS', digest, len(inputs['dependencies']), 'dependencies')
        return
    output = OUT / digest[:16]
    output.mkdir(parents=True, exist_ok=True)
    with (output / 'build.lock').open('w') as writer:
        fcntl.flock(writer, fcntl.LOCK_EX | fcntl.LOCK_NB)
        binary = output / 'wine_runtime_model.exe'
        manifest = output / 'build.json'
        if binary.exists() or manifest.exists():
            if not binary.is_file() or not manifest.is_file():
                raise ValueError('incomplete output preserved: ' + str(output))
            existing = json.loads(manifest.read_text())
            if existing['input_identity'] != digest or existing['artifact_sha256'] != sha(binary):
                raise ValueError('existing immutable Helper drift')
            print('EXISTING_HELPER_VERIFIED', binary)
            return
        environment['TMPDIR'] = str(output)
        commands = []
        def run(name, command):
            result = subprocess.run(command, cwd=ROOT, env=environment,
                                    stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
            (output / (name + '.log')).write_bytes(result.stdout)
            commands.append({'name': name, 'command': command, 'exit_code': result.returncode})
            if result.returncode:
                raise RuntimeError(name + ': ' + result.stdout.decode(errors='replace'))
        for name in ('wire_check', 'test_protocol', 'test_runtime_config', 'test_daily_transport'):
            checker = output / name
            run(name + '-compile', ['/usr/bin/c++', *inputs['host_flags'], str(HERE / ('tests/' + name + '.cpp')), '-o', str(checker)])
            run(name, [str(checker)])
        run('retirement', [sys.executable, str(HERE / 'tests/test_retirement.py')])
        run('builder-tests', [sys.executable, '-m', 'unittest', 'discover', '-s', str(HERE / 'tests'), '-p', 'test_build.py'])
        command = [str(binary) if arg == 'OUTPUT_EXE' else arg for arg in inputs['command_template']]
        run('PE-compile', command)
        for path, expected in {**inputs['sources'], **inputs['dependencies']}.items():
            if sha(path) != expected:
                raise ValueError('source/dependency changed during compile: ' + path)
        protocol.export(output / 'protocol')
        (output / 'compiler-driver.txt').write_text(trace)
        report = capture(['/usr/bin/llvm-readobj', '--coff-imports', str(binary)], ROOT, environment).stdout
        (output / 'imports.txt').write_text(report)
        info = {'schema': 'nrbridge-helper-build-v1', 'status': 'BUILD_ONLY_PASS',
                'base': 'f527d8f2f12d474a66bbb4c1e231d6cd9b673476',
                'source_head': capture(['git', 'rev-parse', 'HEAD'], ROOT, environment).stdout.strip(),
                'source_diff': capture(['git', 'diff', '--binary', 'HEAD'], ROOT, environment).stdout,
                'input_identity': digest, 'inputs': inputs['sources'],
                'dependencies': inputs['dependencies'], 'compiler': inputs['compiler'],
                'commands': commands, 'build_inputs': inputs,
                'artifact': str(binary), 'artifact_sha256': sha(binary),
                'protocol': inputs['protocol'], 'runtime_defaults': inputs['runtime_defaults'],
                'runtime_config_schema': 'nrbridge-helper-runtime-v1',
                'runtime_config_keys': ['schema', 'model', 'forwarder', 'ngx_dir', 'ge_files'],
                'shader_validation': inputs['shader'],
                'CPU': 'PASS: wire, runtime model, runtime config, retirement',
                'GPU': 'NOT_RUN', 'Wine': 'NOT_RUN'}
        manifest.write_text(json.dumps(info, indent=2) + '\n')
        print('HELPER_BUILD_ONLY_PASS', binary, sha(binary))
        print('MANIFEST', manifest, sha(manifest))


if __name__ == '__main__':
    main()
