"""Read-only compiler closure discovery for the isolated Helper build."""
import hashlib
import os
from pathlib import Path
import shlex
import subprocess


def sha(path):
    with Path(path).open('rb') as stream:
        return hashlib.file_digest(stream, 'sha256').hexdigest()


def clean_environment():
    env = dict(os.environ)
    for key in ('CPATH', 'CPLUS_INCLUDE_PATH', 'C_INCLUDE_PATH', 'LIBRARY_PATH',
                'COMPILER_PATH', 'GCC_EXEC_PREFIX', 'CCACHE'):
        env.pop(key, None)
    env['PYTHONDONTWRITEBYTECODE'] = '1'
    return env


def capture(command, cwd, env):
    return subprocess.run(command, cwd=cwd, env=env, capture_output=True, text=True, check=True)


def compiler_inputs(compiler, flags, source, cwd, env):
    result = {}
    dep = capture([str(compiler), *flags, '-M', '-MT', 'dependencies', str(source)], cwd, env).stdout
    for name in shlex.split(dep.replace('\\\n', ' ').split(':', 1)[1]):
        path = Path(name)
        if not path.is_absolute():
            path = cwd / path
        result[str(path)] = sha(path)
    trace = capture([str(compiler), '-###', *flags, str(source), '-o', 'OUTPUT_EXE'], cwd, env).stderr
    commands = [shlex.split(line) for line in trace.splitlines() if line.lstrip().startswith('"')]
    for args in commands:
        path = Path(args[0])
        if path.is_file():
            result[str(path)] = sha(path)
    linker = next((args for args in commands if Path(args[0]).name in ('ld.lld', 'ld')), None)
    if linker:
        paths = [Path(arg[2:]) for arg in linker if arg.startswith('-L')]
        for arg in linker[1:]:
            if arg.startswith('-l'):
                name = arg[2:]
                names = [name[1:]] if name.startswith(':') else ['lib' + name + '.a', 'lib' + name + '.dll.a']
                library = next((p / n for p in paths for n in names if (p / n).is_file()), None)
                if library is None:
                    raise ValueError('unresolved link dependency: ' + arg)
                result[str(library)] = sha(library)
            elif Path(arg).is_absolute() and Path(arg).is_file():
                result[arg] = sha(arg)
    result[str(compiler)] = sha(compiler)
    return result, trace
