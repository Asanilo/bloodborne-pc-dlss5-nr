#!/usr/bin/env python3
"""Delegate normal Helper builds; explicitly isolate frozen legacy rebuilds."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import subprocess
import sys

ROOT = Path(os.environ.get('NR_DEV_ROOT', os.environ.get('NRBRIDGE_HOME', str(Path('/home/arinp/Code/nr-home')))))
HELPER = Path(os.environ['NR_HELPER_ROOT']) if os.environ.get('NR_HELPER_ROOT') else Path(__file__).resolve().parents[2]


def sha(path):
    with Path(path).open('rb') as stream:
        return hashlib.file_digest(stream, 'sha256').hexdigest()


def delegate(helper_root, arguments):
    if helper_root is None:
        raise ValueError('independent Helper repository required; select --helper-root or NR_HELPER_ROOT')
    helper_root = Path(helper_root)
    script = helper_root / 'nrbridge/build.py'
    if not helper_root.is_absolute() or not script.is_file():
        raise ValueError('independent Helper repository required; select --helper-root /absolute/Helper')
    # The standalone Helper subtree owns source, SDK selection, tests and provenance.
    # Normal builds never read/re-export the historical frozen closure.
    return subprocess.run([sys.executable, str(script), *arguments], check=False).returncode


def legacy_rebuild(output=None):
    source = ROOT / 'inputs/helper-f527'
    manifest = source / 'manifest.json'
    info = json.loads(manifest.read_text())
    for path, expected in info.get('dependency_pins', {}).items():
        if sha(path) != expected:
            raise ValueError('legacy dependency drift: ' + path)
    for item in info['files']:
        if sha(item['export']) != item['sha256']:
            raise ValueError('legacy frozen closure drift: ' + item['export'])
    if sha(source / 'effective-generated-spec.json') != info['effective_spec_sha256']:
        raise ValueError('legacy effective spec drift')
    if sha(info['compiler']['path']) != info['compiler']['sha256']:
        raise ValueError('legacy compiler drift')
    allowed = (ROOT / 'build/convergence/legacy-helper-rebuild').resolve()
    output = Path(output) if output is not None else allowed / sha(manifest)[:16]
    if not output.is_absolute() or output.resolve() != output or not output.is_relative_to(allowed) or output == allowed:
        raise ValueError('legacy output must be a new canonical subdirectory of build/convergence/legacy-helper-rebuild')
    if output.exists():
        raise ValueError('existing legacy output preserved; choose a new output directory')
    old_output = ROOT / 'build/helper'
    # Validate every stored command and map only the old output prefix. All
    # frozen source/SDK inputs remain read-only at their recorded paths.
    commands = []
    for command in info['commands']:
        if not isinstance(command, list) or not command or not all(isinstance(arg, str) for arg in command):
            raise ValueError('invalid frozen legacy command')
        changed = [str(output / Path(arg).relative_to(old_output)) if Path(arg).is_relative_to(old_output) else arg for arg in command]
        if changed == command:
            raise ValueError('legacy command has no isolated output mapping')
        commands.append(changed)
    output.mkdir(parents=True)
    steps = []
    for number, command in enumerate(commands):
        result = subprocess.run(command, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, timeout=180,
                                env=dict(os.environ, TMPDIR=str(output), PYTHONDONTWRITEBYTECODE='1'))
        (output / ('step-%d.log' % number)).write_bytes(result.stdout)
        steps.append({'command': command, 'exit_code': result.returncode})
        if result.returncode:
            raise RuntimeError('isolated legacy rebuild failed; output preserved: ' + str(output))
    artifact = output / 'wine_async_presr_play.exe'
    record = {'schema': 'nrbridge-legacy-helper-rebuild-v1', 'status': 'BUILD_ONLY_PASS',
              'frozen_manifest': str(manifest), 'frozen_manifest_sha256': sha(manifest),
              'frozen_expected_artifact_sha256': info['artifact_sha256'],
              'steps': steps, 'artifact': str(artifact), 'artifact_sha256': sha(artifact),
              'legacy_originals_modified': False, 'GPU': 'NOT_RUN', 'Wine': 'NOT_RUN'}
    (output / 'build.json').write_text(json.dumps(record, indent=2) + '\n')
    print('LEGACY_ISOLATED_BUILD_ONLY_PASS', artifact, record['artifact_sha256'])
    return 0


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--helper-root', type=Path, default=HELPER)
    parser.add_argument('--legacy-rebuild', '--rebuild', action='store_true',
                        help='rebuild frozen inputs only to a new isolated directory; --rebuild is the legacy alias')
    parser.add_argument('--legacy-output', type=Path)
    args, remaining = parser.parse_known_args(argv)
    try:
        if args.legacy_rebuild:
            if remaining:
                parser.error('unexpected legacy rebuild options: ' + ' '.join(remaining))
            return legacy_rebuild(args.legacy_output)
        if args.legacy_output:
            parser.error('--legacy-output requires --legacy-rebuild')
        return delegate(args.helper_root, remaining)
    except (OSError, ValueError, KeyError, TypeError, RuntimeError) as error:
        print('nr-dev Helper: ' + str(error), file=sys.stderr)
        return 3


if __name__ == '__main__':
    raise SystemExit(main())
