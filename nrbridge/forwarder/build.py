#!/usr/bin/env python3
"""Offline forwarder only: preserve the original flags, never install a DLL."""
import argparse
import fcntl
import hashlib
import json
import os
from pathlib import Path
import re
import shlex
import subprocess

HERE = Path(__file__).resolve().parent
ROOT = HERE.parents[1]
OUTPUT = Path(os.environ.get('NR_DEV_ROOT', str(ROOT / 'out/nr-dev'))) / 'build/convergence/forwarder'
READOBJ = Path('/usr/bin/llvm-readobj')


def sha(path):
    with Path(path).open('rb') as stream:
        return hashlib.file_digest(stream, 'sha256').hexdigest()


def capture(command, environment):
    return subprocess.run(command, cwd=ROOT, env=environment, check=True,
                          capture_output=True, text=True)


def exports(text):
    result = [{'ordinal': int(ordinal), 'name': name} for ordinal, name in
              re.findall(r'Export \{\s+Ordinal: (\d+)\s+Name: (\S+)', text)]
    if not result or len({item['name'] for item in result}) != len(result):
        raise ValueError('missing/duplicate PE exports')
    return result


def checked_inputs(llvm_root):
    lock = json.loads((HERE / 'source-provenance.json').read_text())
    production = ROOT / lock['production_source']['path']
    required = {production: lock['production_source']['sha256']}
    for name in ('shutdown_bridge.cpp', 'parameter_abi.h'):
        required[HERE / name] = lock['sources'][name]['sha256']
    compiler = llvm_root / 'bin/x86_64-w64-mingw32-clang++'
    if not llvm_root.is_absolute() or not OUTPUT.is_absolute():
        raise ValueError('absolute compiler and output roots required')
    required[compiler] = lock['compiler']['sha256']
    for path, expected in required.items():
        if sha(path) != expected:
            raise ValueError('locked input drift: ' + str(path))
    # Ambient include/search/cache settings must not change this locked recipe.
    environment = dict(os.environ)
    removed = ('CPATH', 'CPLUS_INCLUDE_PATH', 'C_INCLUDE_PATH', 'LIBRARY_PATH',
               'COMPILER_PATH', 'GCC_EXEC_PREFIX', 'CCACHE')
    for name in removed:
        environment.pop(name, None)
    sources = [str(production), str(HERE / 'shutdown_bridge.cpp')]
    flags = ['-std=c++17', '-static', '-shared']
    # Read the compiler's actual transitive header closure; -M does not compile
    # or link, and includes the compiler's Windows and C++ standard headers.
    dependencies = {}
    for source in sources:
        text = capture([str(compiler), *flags, '-M', '-MT', 'dependencies', source], environment).stdout
        body = text.replace('\\\n', ' ').split(':', 1)[1]
        for item in shlex.split(body):
            path = Path(item)
            if not path.is_absolute():
                path = ROOT / path
            dependencies[str(path)] = sha(path)
    command = [str(compiler), *flags, *sources, '-ld3d12', '-o', 'OUTPUT_DLL']
    # -### prints the real frontend/linker commands without running them. Resolve
    # every persistent linker object/archive so the manifest identifies the
    # standard runtime/import libraries, not merely the compiler wrapper.
    trace = capture([str(compiler), '-###', *command[1:]], environment).stderr
    driver_commands = [shlex.split(line) for line in trace.splitlines() if line.lstrip().startswith('"')]
    linker = next(args for args in driver_commands if Path(args[0]).name in ('ld.lld', 'ld'))
    directories = [Path(arg[2:]) for arg in linker if arg.startswith('-L')]
    for args in driver_commands:
        dependencies[args[0]] = sha(args[0])
    for arg in linker[1:]:
        if arg.startswith('-l'):
            library = arg[2:]
            names = [library[1:]] if library.startswith(':') else ['lib' + library + '.a', 'lib' + library + '.dll.a']
            path = next((directory / name for directory in directories for name in names if (directory / name).is_file()), None)
            if path is None:
                raise ValueError('unresolved linker library: ' + arg)
            dependencies[str(path)] = sha(path)
        elif Path(arg).is_absolute() and Path(arg).is_file():
            dependencies[arg] = sha(arg)
    for path in (HERE / 'build.py', HERE / 'source-provenance.json', HERE / 'parameter_abi.h', compiler):
        dependencies[str(path)] = sha(path)
    inputs = {'schema': 'nrbridge-forwarder-build-inputs-v1',
              'recipe_source': lock['sources']['build.py'],
              'source_provenance': lock, 'file_sha256': dict(sorted(dependencies.items())),
              'compiler_version': capture([str(compiler), '--version'], environment).stdout,
              'command_template': command, 'cwd': str(ROOT),
              'environment_removed': list(removed),
              'export_inspector': {'path': str(READOBJ), 'sha256': sha(READOBJ)}}
    return inputs, environment


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--check-only', action='store_true', help='verify locked sources and discover dependencies without writing outputs')
    parser.add_argument('--llvm-root', type=Path, default=Path(os.environ.get('NR_LLVM_ROOT', str(ROOT / 'out/deps/llvm-mingw'))))
    args = parser.parse_args()
    inputs, environment = checked_inputs(args.llvm_root)
    digest = hashlib.sha256(json.dumps(inputs, sort_keys=True).encode()).hexdigest()
    if args.check_only:
        print('INPUT_CHECK_PASS', digest, len(inputs['file_sha256']), 'files; no compilation or GPU')
        return
    output = OUTPUT / digest[:16]
    output.mkdir(parents=True, exist_ok=True)
    with (output / 'build.lock').open('w') as writer:
        fcntl.flock(writer, fcntl.LOCK_EX | fcntl.LOCK_NB)
        artifact = output / 'nvngx.dll_dlssnr.dll'
        manifest = output / 'build.json'
        if artifact.exists() or manifest.exists():
            if not manifest.is_file() or not artifact.is_file():
                raise ValueError('incomplete output preserved: ' + str(output))
            previous = json.loads(manifest.read_text())
            if previous['inputs'] != inputs or sha(artifact) != previous['artifact_sha256']:
                raise ValueError('existing immutable forwarder drift: ' + str(output))
            print('EXISTING_BUILD_VERIFIED', artifact)
            return
        command = [str(artifact) if arg == 'OUTPUT_DLL' else arg for arg in inputs['command_template']]
        # Only paths differ from the original single forwarder command.
        environment['TMPDIR'] = str(output)
        result = subprocess.run(command, cwd=ROOT, env=environment,
                                stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
        (output / 'compile.log').write_bytes(result.stdout)
        if result.returncode:
            raise RuntimeError(result.stdout.decode(errors='replace'))
        report = capture([str(READOBJ), '--coff-exports', str(artifact)], environment).stdout
        actual = exports(report)
        expected = inputs['source_provenance']['original_forwarder']['exports']
        if actual != expected or not any(item['name'] == 'nr_poc_vk_shutdown' for item in actual):
            raise ValueError('forwarder export name/ordinal drift')
        (output / 'exports.txt').write_text(report)
        imports = capture([str(READOBJ), '--coff-imports', str(artifact)], environment).stdout
        (output / 'imports.txt').write_text(imports)
        # Detect a concurrently modified header/library/source before publishing
        # a successful provenance record.
        for path, expected_sha in inputs['file_sha256'].items():
            if sha(path) != expected_sha:
                raise ValueError('input changed during compilation: ' + path)
        record = {'schema': 'nrbridge-forwarder-build-v1', 'status': 'BUILD_EXPORTS_PASS',
                  'source_head': capture(['git', 'rev-parse', 'HEAD'], environment).stdout.strip(),
                  'source_diff': capture(['git', 'diff', '--binary', 'HEAD'], environment).stdout,
                  'input_identity': digest, 'inputs': inputs, 'command': command,
                  'temporary_directory': str(output), 'artifact': str(artifact),
                  'artifact_sha256': sha(artifact), 'exports': actual,
                  'export_comparison': 'exact names and ordinals match locked original 26-export reference',
                  'runtime_imports_sha256': sha(output / 'imports.txt'),
                  'GPU': 'NOT_RUN', 'Wine': 'NOT_RUN', 'installed': False}
        manifest.write_text(json.dumps(record, indent=2) + '\n')
        print('BUILD_EXPORTS_PASS', artifact, record['artifact_sha256'])
        print('MANIFEST', manifest, sha(manifest))


if __name__ == '__main__':
    main()
