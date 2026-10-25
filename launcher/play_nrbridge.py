#!/usr/bin/env python3
"""Player presets over the existing managed entry; no second GPU lifecycle.

Local paths live in the ignored out/player-preview/profile.json, prepared once
from an existing installation. Models, DLLs, prefixes and saves are not bundled.
"""
import argparse
import json
import os
import re
from pathlib import Path
import sys
import time
import uuid

APP = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(APP / 'tools/nr_dev'))
sys.path.insert(0, str(APP / 'launcher'))
import dimension_profile
import fg_preferences
import nrbridge_launcher as bridge


def arguments(argv=None):
    p = argparse.ArgumentParser(description='Bloodborne private player preview (managed NR/SR/FG)')
    p.add_argument('--profile', type=Path, default=Path(os.environ.get(
        'NRBRIDGE_PLAYER_PROFILE', APP / 'out/player-preview/profile.json')))
    p.add_argument('--memory-model', choices=('old', 'new'), default='old')
    p.add_argument('--nr', choices=('on', 'off'), default='on')
    p.add_argument('--sr', choices=tuple(dimension_profile.PRESETS), default='quality')
    p.add_argument('--output', default='1920x1080')
    p.add_argument('--execution', default='1280x720')
    p.add_argument('--fg', choices=('off', 'dlssg', 'fsr31', 'fsr316'), default=None,
                   help='override saved in-game FG backend; default uses saved selection')
    p.add_argument('--multiplier', type=int, choices=range(2, 7), default=None)
    p.add_argument('--gpu-validation', action='store_true')
    p.add_argument('--fg-profile', action='store_true', help='bounded FG stage timings and capture diagnostics')
    p.add_argument('--check', action='store_true', help='validate launch without starting GPU work')
    p.add_argument('--session', help=argparse.SUPPRESS)
    return p.parse_args(argv)


def path(value, kind='file'):
    p = Path(value).expanduser()
    if not p.is_absolute() or p.resolve() != p or p.is_symlink():
        raise ValueError('A canonical absolute local path is required')
    if kind == 'file' and not p.is_file() or kind == 'dir' and not p.is_dir():
        raise ValueError('Prepared local runtime path is missing: ' + str(p))
    return p


def prepare(a):
    profile = path(a.profile)
    if profile.stat().st_size > 65536:
        raise ValueError('Player profile is too large')
    local = json.loads(profile.read_text())
    if local.get('schema') != 'bbport-player-preview-v1':
        raise ValueError('Expected a prepared bbport-player-preview-v1 profile')
    home = path(local['home'], 'dir')
    build = path(local['native_build'], 'dir')
    inputs = path(local['local_inputs'])
    base = json.loads(path(local['base_settings']).read_text())
    ini_text = path(local['base_ini']).read_text()
    data = path(local['player_data_dir'], 'dir')
    preferences = fg_preferences.read(profile, data)
    explicit_fg = a.fg is not None
    if a.fg is None:a.fg=preferences.get('fg_backend','off')
    if a.multiplier is None:a.multiplier=preferences.get('fg_dlss_multiplier',2) if a.fg=='dlssg' else 2
    # User saves belong to this private preview, separate from experiment runs.
    user = path(base['user_dir'], 'dir')
    if not user.is_relative_to(profile.parent):
        raise ValueError('Preview user_dir must be inside its private profile directory')
    if a.fg in ('fsr31','fsr316') and (a.multiplier != 2 or a.output != '1920x1080' or a.sr != 'quality'):
        raise ValueError('FSR fallback currently uses 1080p Quality x2; DLSS supports the dynamic profiles')
    # Keep x2 available in the menu even when starting with FG disabled.
    standby = a.fg=='off' and a.sr!='native' and (explicit_fg or not preferences)
    backend = 'dlssg' if standby else a.fg
    fg = dict(local.get('fg', {}), backend=backend, multiplier=a.multiplier,
              pipeline='consumer-queue-v1', transport='gpu-dmabuf-v1')
    if backend == 'dlssg':
        bridge.fg_config.availability(fg, home)
    dims = dimension_profile.select(argparse.Namespace(
        nr_sr_preset=a.sr, nr_output=a.output, nr_execution=a.execution))
    base.update(memory_model=a.memory_model, nrbridge_enabled=a.nr == 'on',
                nrbridge_native_mouse=True, nrbridge_dimensions=backend not in ('fsr31', 'fsr316'),
                nrbridge_execution=a.execution, online=False, extra_env='')
    base['fg_preferences_owner']=str(profile)
    base['fg_enabled']=not standby and (preferences.get('fg_enabled',True) if not explicit_fg else a.fg!='off')
    for key in ('backend', 'multiplier', 'pipeline', 'transport', 'model', 'runtime', 'prefix_session'):
        target = 'fg_backend' if key == 'backend' else 'fg_dlss_' + key
        base[target] = fg.get(key, '')
    session = a.session or ('player-' + time.strftime('%Y%m%d-%H%M%S') + '-' + uuid.uuid4().hex[:6])
    if not re.fullmatch(r'[A-Za-z0-9][A-Za-z0-9-]{0,62}', session):
        raise ValueError('Invalid player session name')
    # The runner remains the sole owner of save transactions, lock and retirement.
    recipes = home / 'player-profiles'
    if recipes.is_symlink() or recipes.resolve() != recipes:
        raise ValueError('Unsafe player recipe directory')
    recipes.mkdir(exist_ok=True)
    recipe = recipes / session
    recipe.mkdir(exist_ok=False)
    settings = recipe / 'settings.json'; ini = recipe / 'bbport.ini'
    settings.write_text(json.dumps(base, indent=2) + '\n')
    ini.write_text(dimension_profile.ini(dims, ini_text))
    command = bridge.player_command(base, dict(line.split('=', 1) for line in ini.read_text().splitlines()
        if '=' in line and not line.lstrip().startswith(('#', ';'))),
        settings_path=settings, ini_path=ini, data_dir=data, home_path=home, session=session)
    command += ['--native-build', str(build), '--local-inputs', str(inputs)]
    command[1] = str(APP / 'launcher/nrbridge_entry.py')
    if a.nr == 'on' and local.get('reuse_nr_prefix'):
        command += ['--reuse-nr-prefix', local['reuse_nr_prefix']]
    if a.gpu_validation:
        command += ['--gpu-validation']
    if a.fg_profile:
        command += ['--fg-profile']
    env = dict(os.environ, NRBRIDGE_HOME=str(home), NRBRIDGE_APP=str(APP),
               NRBRIDGE_ENTRY=command[1], BB_NRBRIDGE_ACTIVE='1', BB_PREBUILT='1')
    return command, env, user


def main(argv=None):
    a = arguments(argv)
    try:
        command, env, user = prepare(a)
        if a.check:
            # Exercise the real coordinator's CPU admission, without a supervisor/GPU.
            import runner
            import play
            runner.ROOT = Path(env['NRBRIDGE_HOME'])
            forwarded=[];i=2
            while i<len(command):
                if command[i]=='--home':i+=2;continue
                if command[i] in ('--no-terminal','--offline'):i+=1;continue
                forwarded.append(command[i]);i+=1
            args = play.parse_args(forwarded)
            play.launch_command(args)
            print('READY: ' + a.memory_model + ', NR ' + a.nr + ', SR ' + a.sr +
                  ', FG ' + a.fg + '; private saves: ' + str(user))
            return 0
        print('Private preview saves: ' + str(user), flush=True)
        print('F8: start/toggle NR after entering the scene. Delete: settings and FG toggle.', flush=True)
        os.execvpe(sys.executable, command, env)
    except (ValueError, OSError, KeyError) as error:
        print('Player preview: ' + str(error), file=sys.stderr)
        return 2


if __name__ == '__main__':
    raise SystemExit(main())
