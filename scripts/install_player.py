#!/usr/bin/env python3
"""Stage a local complete player release; activate only after actual gameplay QA.

Reuses an existing packaged GTK closure and retired NR/FG Wine environments.
Settings, saves, the old runtime inputs and previous installation stay intact.
"""
import argparse
from datetime import datetime, timezone
import fcntl
import hashlib
import importlib.util
import json
import os
from pathlib import Path
import shlex
import shutil
import sys


def load(path, name):
    spec=importlib.util.spec_from_file_location(name,path)
    module=importlib.util.module_from_spec(spec);spec.loader.exec_module(module)
    return module


def digest(path):
    with Path(path).open('rb') as stream:return hashlib.file_digest(stream,'sha256').hexdigest()


def write(path, value):
    if path.exists():raise ValueError('Refusing to replace '+str(path))
    path.write_text(json.dumps(value,indent=2)+'\n');path.chmod(0o600)


def model_copy(source, root):
    identity=digest(source);target=root/identity[:16]/source.name
    target.parent.mkdir(parents=True,exist_ok=True)
    if target.exists():
        if digest(target)!=identity:raise ValueError('Installed model checksum mismatch')
    else:
        temp=target.with_suffix('.copying');shutil.copy2(source,temp)
        if digest(temp)!=identity:raise ValueError('Model copy changed')
        temp.replace(target)
    return target


def stage(args):
    root=args.root.resolve();home=args.home.resolve();app=args.appdir.resolve()
    if app==Path('/') or not (app/'usr/share/bbport/nrbridge-bundle.json').is_file():
        raise ValueError('Expected a built complete AppDir')
    settings=json.loads((root/'config/bbport-launcher/settings.json').read_text())
    game=Path(settings['game_dir']).resolve()
    if not (game/'eboot.bin').is_file():raise ValueError('Installed game missing')
    old=json.loads((home/'localinputs/runtime.json').read_text())
    source=json.loads(args.runtime_inputs.read_text())
    share=app/'usr/share/bbport';sys.path.insert(0,str(share/'tools/nr_dev'))
    import peer_retirement
    # Admission and prefix proof are checked under the existing global lock.
    for name in (args.nr_prefix,args.fg_prefix):
        if not name or Path(name).name!=name:raise ValueError('Invalid retired prefix name')
        prior=home/'runs'/name
        peer_retirement.audit_session(prior)
        prefix=prior/'profile/wine-prefix'
        if not (prefix/'system.reg').is_file() or prefix.resolve()!=prefix:
            raise ValueError('Initialized canonical retired prefix required')
        manifest=json.loads((prior/'manifest.json').read_text())
        # The runner repeats exact environment/owner admission before use.
        if json.loads((prior/'result.json').read_text()).get('returncode')!=0:
            raise ValueError('Prefix source did not exit normally')
    head=json.loads((share/'nrbridge-bundle.json').read_text())['native']['source']
    release=root/'runtime/releases'/(datetime.now(timezone.utc).strftime('%Y%m%dT%H%M%SZ')+'-player-'+head[:8])
    release.mkdir(parents=True,exist_ok=False)
    shutil.move(str(app),str(release/'AppDir'))
    app=release/'AppDir';share=app/'usr/share/bbport'
    entry=load(share/'launcher/nrbridge_entry.py','installed_player_entry')
    native=entry.stage_native(home,replace=False)
    helper=entry.stage_helper(home,replace=False)
    forwarder_dir=home/'build/player'/release.name;forwarder_dir.mkdir(parents=True)
    forwarder=forwarder_dir/'nvngx.dll_dlssnr.dll'
    shutil.copy2(share/'nrbridge/forwarder/nvngx.dll_dlssnr.dll',forwarder)
    models=root/'runtime/shared/models'
    nr_model=model_copy(Path(source['model']),models)
    fg_model=model_copy(args.fg_model.resolve(),models)
    cfg=dict(source)
    cfg.update(assets={'path':str(game),'access':'read-only'},seed=old['seed'],
               schema='nr-local-runtime-inputs-v2',nr_preset='B',
               sysroot_lib=str(app/'usr/lib/bbport'),
               helper={'path':str(helper),'manifest_sha256':digest(helper.parent/'build.json')},
               forwarder=str(forwarder),model=str(nr_model))
    inputs=home/'localinputs'/('runtime-'+release.name+'.json')
    pins=inputs.with_name(inputs.stem+'-pins.json')
    runtime=entry.load_runtime_inputs()
    seed=Path(cfg['seed']);ge=Path(cfg['ge_files']);ngx=Path(cfg['ngx_dir'])
    required=[helper,helper.parent/'build.json',seed/'manifest.json',game/'eboot.bin',
              forwarder,nr_model,Path(cfg['system_ngx']),ge/'lib/wine/x86_64-unix/wine',
              ge/'lib/wine/x86_64-unix/wine-preloader',ngx/'_nvngx.dll',ngx/'nvngx.dll']
    required += [seed/'savedata'/item['relative'] for item in json.loads((seed/'manifest.json').read_text())['files']]
    inventory=entry.generate_pins(runtime,cfg,required,[ge,Path(cfg['sysroot_lib']),ngx])
    write(pins,{'schema':'nr-runtime-input-pins-v1','pins':inventory})
    cfg['pins']={'path':str(pins),'sha256':digest(pins)};write(inputs,cfg)
    runtime.load(inputs,home)
    player_runtime=release/'player-runtime.json'
    write(player_runtime,dict(schema='bbport-player-runtime-v1',native_source=head,
        fg=dict(fg_dlss_model=str(fg_model),fg_dlss_runtime=str(share/'fg-runtime'),
                fg_dlss_prefix_session=args.fg_prefix)))
    environment=dict(NRBRIDGE_HOME=str(home),NRBRIDGE_NATIVE_BUILD=str(native),
        NRBRIDGE_RUNTIME_INPUTS=str(inputs),NRBRIDGE_REUSE_NR_PREFIX=args.nr_prefix,
        BB_PLAYER_RUNTIME=str(player_runtime))
    lines=['#!/usr/bin/env bash','set -euo pipefail']
    lines += ['export '+key+'='+shlex.quote(value) for key,value in environment.items()]
    lines += ['exec '+shlex.quote(str(app/'AppRun'))+' "$@"']
    (release/'launch.sh').write_text('\n'.join(lines)+'\n');(release/'launch.sh').chmod(0o755)
    write(release/'release.json',dict(schema='bbport-local-player-release-v1',native_source=head,
        environment=environment,stage_status='AWAITING_GAMEPLAY_QA',settings_preserved=True,saves_preserved=True))
    print(release)
    return release


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--appdir',type=Path,required=True)
    parser.add_argument('--root',type=Path,default=Path.home()/'bloodborne')
    parser.add_argument('--home',type=Path,default=Path.home()/'.local/share/bbport/nrbridge-home')
    parser.add_argument('--runtime-inputs',type=Path,required=True)
    parser.add_argument('--fg-model',type=Path,required=True)
    parser.add_argument('--nr-prefix',required=True);parser.add_argument('--fg-prefix',required=True)
    args=parser.parse_args()
    with open('/tmp/nrbridge-poc-gpu.lock','a') as lock:
        fcntl.flock(lock,fcntl.LOCK_EX|fcntl.LOCK_NB)
        stage(args)


if __name__=='__main__':main()
