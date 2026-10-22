"""Explicit Experimental DLSSG CPU backend inputs; no NVIDIA runtime installation."""
import hashlib
import importlib.util
import json
from pathlib import Path
import tempfile
import os
import shutil
import stat


def validate_args(args):
    if getattr(args,'experimental_fg',None)!='dlssg':
        if getattr(args,'dlssg_model',None):
            raise ValueError('--dlssg-model requires --experimental-fg dlssg')
        return
    if not (getattr(args,'daily',False) and getattr(args,'fg_native',False)):
        raise ValueError('DLSSG requires daily --fg-native real-source binding')
    if not getattr(args,'dlssg_model',None):
        raise ValueError('DLSSG requires an explicit existing --dlssg-model path')


def check(args,build):
    if getattr(args,'experimental_fg',None)!='dlssg':return None
    if b'DLSSG_CPU_BRIDGE_READY' not in (Path(build)/'gpu/libbbgpu.so').read_bytes():
        raise ValueError('DLSSG requires rebuilt Native CPU bridge')
    source=Path(__file__).resolve().parents[2]
    spec=importlib.util.spec_from_file_location('dlssg_wine_build_inputs',source/'tools/fg_dev/build_wine_probe.py')
    module=importlib.util.module_from_spec(spec);spec.loader.exec_module(module)
    record=json.loads((source/'out/m2-wine-probe-build.json').read_text())
    if record.get('schema')!='dlssg-wine-probe-build-v1' or record['inputs']!=module.source_identity():
        raise ValueError('rebuild changed DLSSG Helper sources')
    for name,identity in record['artifacts'].items():
        if module.identity(source/'out'/name)!=identity:
            raise ValueError('changed DLSSG Helper artifact')
    model=Path(args.dlssg_model).resolve(strict=True)
    return {'source':source,'build':record,'model':module.identity(model),
            'cuda':module.identity(source/'out/nvcuda.dll'),'core':module.identity('/usr/lib/nvidia/wine/_nvngx.dll')}


def prepare(args,build,run,binding,runner):
    state=check(args,build)
    if state is None:return {}
    import peer_retirement
    previous=runner['checked_run'](args.dlssg_prefix_session)
    result=json.loads((previous/'result.json').read_text())
    if result.get('status')!='PASS_NORMAL_TEARDOWN':raise ValueError('DLSSG prefix requires a retired source session')
    peer_retirement.audit_session(previous)
    prefix=previous/'profile/wine-prefix'
    if not prefix.is_dir() or prefix.resolve()!=prefix:raise ValueError('existing private DLSSG prefix required')
    source=state['source'];ipc=Path(tempfile.mkdtemp(prefix='bb-dlssg-',dir='/tmp'))
    config={'schema':'dlssg-native-cpu-v1','session':args.session,'run':str(run),'ipc':str(ipc),
            'prefix':str(prefix),'ge_files':binding['config']['ge_files'],'ngx_dir':binding['config']['ngx_dir'],
            'executable':str(source/'out/wine-dlssg-probe.exe'),'adapter':str(source/'out/nvngx.dll_dlssg.dll'),
            'model':state['model']['path'],'cuda':str(source/'out/nvcuda.dll'),
            'identities':{k:state[k] for k in ['model','cuda','core']},'helper_build':state['build'],
            'transport':args.dlssg_transport,'reused_retired_prefix':args.dlssg_prefix_session,'scope':'synchronous CPU transport; independent persistent FG history; no GPU shared images'}
    path=run/'dlssg-config.json';runner['atomic'](path,config)
    return {'BB_DLSSG_STREAM_CONFIG':str(path),'BB_DLSSG_STREAM_DIR':str(ipc),
            'BB_DLSSG_CONFIG_SHA256':hashlib.sha256(path.read_bytes()).hexdigest()}


def validate_manifest(run,manifest):
    env=manifest.get('environment',{})
    if env.get('BB_FRAME_GEN_BACKEND')!='dlssg':return
    path=Path(env.get('BB_DLSSG_STREAM_CONFIG',''))
    if path!=Path(run)/'dlssg-config.json' or path.is_symlink() or hashlib.sha256(path.read_bytes()).hexdigest()!=env.get('BB_DLSSG_CONFIG_SHA256'):
        raise ValueError('DLSSG runtime config drift')
    config=json.loads(path.read_text())
    if config['session']!=manifest['session'] or config['run']!=str(run) or config['ipc']!=env.get('BB_DLSSG_STREAM_DIR') or env.get('BB_FRAME_GEN_NATIVE')!='1':
        raise ValueError('DLSSG private scope drift')
    # NR's owner requires its canonical per-session prefix. FG prefix reuse
    # cannot override that independent process boundary.
    if env.get('BB_NR_NATIVE_ORIGIN_PREFIX')!=str(Path(run)/'profile/wine-prefix'):
        raise ValueError('DLSSG must preserve NR per-session prefix scope')


def cleanup_cpu_slot(run,manifest,result):
    if manifest.get('environment',{}).get('BB_FRAME_GEN_BACKEND')!='dlssg':return None
    if result.get('status')!='PASS_NORMAL_TEARDOWN' or result.get('native_lastuse_proof') is not True or result.get('residuals')!=[]:
        return {'status':'RETAINED_UNPROVED'}
    validate_manifest(run,manifest)
    config=json.loads((Path(run)/'dlssg-config.json').read_text());ipc=Path(config['ipc'])
    info=ipc.lstat()
    if ipc.parent!=Path('/tmp') or not ipc.name.startswith('bb-dlssg-') or ipc.resolve()!=ipc or not stat.S_ISDIR(info.st_mode) or info.st_uid!=os.getuid() or stat.S_IMODE(info.st_mode)!=0o700:
        raise ValueError('private CPU slot cleanup scope')
    retained=Path(run)/'dlssg-ipc-terminal';retained.mkdir()
    records=[]
    for path in ipc.iterdir():
        if path.is_symlink() or not path.is_file():raise ValueError('unexpected CPU slot entry retained')
        if path.suffix=='.json':
            if path.stat().st_size>131072:raise ValueError('bounded terminal metadata required')
            shutil.copyfile(path,retained/path.name)
            records.append({'name':path.name,'sha256':hashlib.sha256(path.read_bytes()).hexdigest()})
    shutil.rmtree(ipc) # Owned CPU files only, after actual Helper + Native + Present retirement.
    return {'status':'RETIRED','directory':str(ipc),'terminal_metadata':records}
