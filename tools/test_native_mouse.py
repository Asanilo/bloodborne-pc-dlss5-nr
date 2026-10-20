#!/usr/bin/env python3
"""Build and run isolated CPU tests; never loads bb-probe, Vulkan or game assets."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import subprocess
import sys


def main():
    root = Path(__file__).resolve().parents[1]
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--out', type=Path, default=root / 'out/native-mouse-validation')
    parser.add_argument('--sanitize', choices=('none', 'address,undefined'), default='none')
    args = parser.parse_args()
    output = args.out.resolve()
    if not output.is_relative_to(root / 'out'):
        parser.error('CPU test output must be inside this worktree/out')
    output.mkdir(parents=True, exist_ok=True)
    subprocess.run([sys.executable, str(root / 'tools/check_native_mouse_stub.py')], check=True)
    subprocess.run([sys.executable, str(root / 'tests/pm4/native_mouse_gate_test.py')], check=True)
    subprocess.run([sys.executable, str(root / 'tests/native_mouse_edges.py'),
                    *(['--sanitize'] if args.sanitize != 'none' else [])], check=True)
    sanitizer = [] if args.sanitize == 'none' else [
        '-fsanitize=' + args.sanitize, '-fno-omit-frame-pointer']
    common = ['-I' + str(root), '-pthread', '-g', '-O1', '-UNDEBUG', *sanitizer]
    module = ['gpu/shim/bbport_mouse_look.cpp']
    # Most implementations embed the relocatable leaf template in C++. Keep explicit
    # optional assembly inputs for the separate fixture/template form as well.
    for name in ('gpu/shim/native_mouse_stub.S', 'gpu/shim/bbport_mouse_stub.S'):
        if (root / name).is_file():
            module.append(name)
    fixture = ['tests/native_mouse_fixture.S'] if (root / 'tests/native_mouse_fixture.S').is_file() else []
    cases = [
        ('mailbox', ['tests/test_native_mouse_mailbox.cpp'], True),
        ('stub', ['tests/test_native_mouse_stub.cpp', *module, *fixture], True),
        ('xstate', ['tests/test_native_mouse_xstate.cpp', *module,
                    'tests/native_mouse_xstate_fixture.S'], True),
        ('input', ['tests/test_native_mouse_input.cpp', *module], True),
        ('host-input', ['tests/test_host_input.cpp'], True),
        ('pad', ['tests/test_pad.c'], False),
        ('chord-capture', ['tests/test_chord_capture.c'], False),
    ]
    environment = dict(os.environ, SDL_VIDEODRIVER='dummy', SDL_AUDIODRIVER='dummy')
    if sanitizer:
        # LSan's process inspection fails under the managed execution tracer. Address
        # and undefined-behaviour checks remain active; leak detection is not claimed.
        environment['ASAN_OPTIONS'] = 'detect_leaks=0'
        environment['UBSAN_OPTIONS'] = 'halt_on_error=1:print_stacktrace=1'
    record = {
        'scope': 'CPU_ONLY_NO_GAME_NO_VULKAN', 'sanitizer': args.sanitize,
        'leak_detection': False if sanitizer else 'not_requested',
        'head': subprocess.check_output(['git', 'rev-parse', 'HEAD'], cwd=root, text=True).strip(),
        'cases': [],
    }
    proof_inputs = {'gpu/bbgpu.h', 'src/runtime_pad.c', 'gpu/shim/host_input.h',
                    'gpu/shim/native_mouse_mailbox.h', 'gpu/shim/bbport_mouse_look.h',
                    'gpu/shim/window.cpp', 'gpu/shim/bbgpu.cpp', 'gpu/shim/bbport_guest_hooks.cpp',
                    'gpu/shim/bbport_guest_hooks.h', 'src/probe.c',
                    'gpu/shim/native_mouse_stub_template.S', 'tools/check_native_mouse_stub.py',
                    'tools/test_native_mouse.py', 'tests/pm4/native_mouse_gate_test.py',
                    'tests/native_mouse_edges.py'}
    for _, sources, _ in cases:
        proof_inputs.update(sources)
    record['inputs'] = {name: hashlib.sha256((root / name).read_bytes()).hexdigest()
                        for name in sorted(proof_inputs)}
    failed = False
    for name, sources, cpp in cases:
        binary = output / name
        command = [os.environ.get('CXX' if cpp else 'CC', 'clang++' if cpp else 'clang'),
                   '-std=c++23' if cpp else '-std=gnu11', *common, '-Wall', '-Wextra']
        if cpp:
            command += ['-Werror', '-mavx', '-msse4.1', '-mxsave']
        else:
            command += ['-Wno-unused-function']
        command += [*sources, '-o', str(binary)]
        if not cpp:
            command += ['-lSDL3', '-lm']
        item = {'name': name, 'build_command': command, 'sources': {
            source: hashlib.sha256((root / source).read_bytes()).hexdigest() for source in sources}}
        with (output / (name + '-build.log')).open('w') as log:
            build = subprocess.run(command, cwd=root, env=environment, stdout=log, stderr=subprocess.STDOUT)
        item['build_exit'] = build.returncode
        if build.returncode == 0:
            with (output / (name + '.log')).open('w') as log:
                run = subprocess.run([str(binary)], cwd=root, env=environment,
                                     stdout=log, stderr=subprocess.STDOUT)
            item['run_exit'] = run.returncode
        item['pass'] = item.get('run_exit') == 0
        failed |= not item['pass']
        record['cases'].append(item)
        print(name + ': ' + ('PASS' if item['pass'] else 'FAIL'), flush=True)
    (output / 'validation.json').write_text(json.dumps(record, indent=2) + '\n')
    return int(failed)


if __name__ == '__main__':
    raise SystemExit(main())
