"""Explicit Experimental DLSSG backend inputs; no NVIDIA runtime installation."""
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
    multiplier=getattr(args,'dlssg_multiplier',2)
    if not 2<=multiplier<=6:raise ValueError('DLSS multiplier 2..6 required')
    if multiplier>2 and (getattr(args,'dlssg_transport',None)!='gpu-dmabuf-v1' or getattr(args,'dlssg_pipeline',None)!='consumer-queue-v1'):
        raise ValueError('DLSS 3X–6X requires GPU DMA-BUF consumer-queue-v1')
    if not (getattr(args,'daily',False) and getattr(args,'fg_native',False)):
        raise ValueError('DLSSG requires daily --fg-native real-source binding')
    if getattr(args,'dlssg_pipeline','sync-v1') not in ('sync-v1','submit-ready-v1','late-ready-v1','consumer-ready-v1','consumer-queue-v1'):
        raise ValueError('unsupported DLSSG pipeline')
    if getattr(args,'dlssg_helper_wake','poll-v1')=='notify-v1' and getattr(args,'dlssg_transport',None)!='gpu-dmabuf-v1':
        raise ValueError('notify-v1 requires gpu-dmabuf-v1')
    if getattr(args,'dlssg_pipeline','sync-v1')!='sync-v1' and getattr(args,'dlssg_transport',None)!='gpu-dmabuf-v1':
        raise ValueError('submit-ready-v1 requires gpu-dmabuf-v1')
    if not getattr(args,'dlssg_model',None):
        raise ValueError('DLSSG requires an explicit existing --dlssg-model path')


def check(args,build):
    if getattr(args,'experimental_fg',None)!='dlssg':return None
    marker=b'DLSSG_GPU_BRIDGE_READY' if getattr(args,'dlssg_transport',None)=='gpu-dmabuf-v1' else b'DLSSG_CPU_BRIDGE_READY'
    if marker not in (Path(build)/'gpu/libbbgpu.so').read_bytes():
        raise ValueError('DLSSG requires rebuilt Native selected bridge')
    if (getattr(args,'dlssg_pipeline','sync-v1')!='sync-v1' and
        b'DLSSG_GPU_SUBMIT_READY_PROFILE' not in (Path(build)/'gpu/libbbgpu.so').read_bytes()):
        raise ValueError('submit-ready-v1 requires rebuilt asynchronous Native bridge')
    if (getattr(args,'dlssg_pipeline','sync-v1')=='late-ready-v1' and
        b'GPU dormant request resolved before guide reuse' not in (Path(build)/'gpu/libbbgpu.so').read_bytes()):
        raise ValueError('late-ready-v1 requires rebuilt deferred Native bridge')
    if (getattr(args,'dlssg_pipeline','sync-v1') in ('consumer-ready-v1','consumer-queue-v1') and
        b'DLSSG_INDEPENDENT_COPY' not in (Path(build)/'gpu/libbbgpu.so').read_bytes()):
        raise ValueError('consumer-ready-v1 requires rebuilt independent consumer bridge')
    if (getattr(args,'dlssg_pipeline','sync-v1')=='consumer-queue-v1' and
        b'DLSSG_NATIVE_QUEUE' not in (Path(build)/'gpu/libbbgpu.so').read_bytes()):
        raise ValueError('consumer-queue-v1 requires rebuilt second Native queue')
    source=Path(__file__).resolve().parents[2]
    spec=importlib.util.spec_from_file_location('dlssg_wine_build_inputs',source/'tools/fg_dev/build_wine_probe.py')
    module=importlib.util.module_from_spec(spec);spec.loader.exec_module(module)
    runtime=Path(getattr(args,'dlssg_runtime',None) or source/'out').expanduser().resolve(strict=True)
    record=json.loads((runtime/'m2-wine-probe-build.json').read_text())
    def content_identity(items):return {k:{f:v[f] for f in ('bytes','sha256')} for k,v in items.items()}
    if record.get('schema')!='dlssg-wine-probe-build-v1' or content_identity(record['inputs'])!=content_identity(module.source_identity()):
        raise ValueError('rebuild changed DLSSG Helper sources')
    for name,identity in record['artifacts'].items():
        if name!=Path(name).name:raise ValueError('invalid Helper artifact name')
        actual=module.identity(runtime/name)
        if any(actual[k]!=identity[k] for k in ('bytes','sha256')):
            raise ValueError('changed DLSSG Helper artifact')
    model=Path(args.dlssg_model).resolve(strict=True)
    if getattr(args,'dlssg_multiplier',2)>2:
        if module.identity(model)['sha256']!='ff6e90eb78b827927dff5b4ecc6b1c870c2e9bca29ed9f48c7d348cc9e170b82':
            raise ValueError('MFG requires verified DLSSG 310.9.1.0 SHA256; model left unchanged')
        if b'DLSSG_MFG_PRESENT' not in (Path(build)/'gpu/libbbgpu.so').read_bytes():raise ValueError('MFG requires rebuilt M4 Native')
    return {'source':source,'runtime':runtime,'build':record,'model':module.identity(model),
            'cuda':module.identity(runtime/'nvcuda.dll'),'core':module.identity('/usr/lib/nvidia/wine/_nvngx.dll')}


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
    source=state['source'];runtime=state['runtime'];ipc=Path(tempfile.mkdtemp(prefix='bb-dlssg-',dir='/tmp'))
    gpu=args.dlssg_transport=='gpu-dmabuf-v1'
    config={'schema':'dlssg-native-gpu-v1' if gpu else 'dlssg-native-cpu-v1','session':args.session,'run':str(run),'ipc':str(ipc),
            'prefix':str(prefix),'ge_files':binding['config']['ge_files'],'ngx_dir':binding['config']['ngx_dir'],
            'executable':str(runtime/'wine-dlssg-probe.exe'),'adapter':str(runtime/'nvngx.dll_dlssg.dll'),
            'model':state['model']['path'],'cuda':str(runtime/'nvcuda.dll'),
            'identities':{k:state[k] for k in ['model','cuda','core']},'helper_build':state['build'],
            'multiplier':getattr(args,'dlssg_multiplier',2),'pipeline':getattr(args,'dlssg_pipeline','sync-v1'),'transport':args.dlssg_transport,'helper_wake':getattr(args,'dlssg_helper_wake','poll-v1') if gpu else 'poll-v1','reused_retired_prefix':args.dlssg_prefix_session,'scope':'DMA-BUF GPU mailbox; private persistent FG history; actual GPU last-use ACK' if gpu else 'synchronous CPU transport; independent persistent FG history; no GPU shared images'}
    path=run/'dlssg-config.json';runner['atomic'](path,config)
    return {'BB_DLSSG_MULTIPLIER':str(config['multiplier']),'BB_DLSSG_STREAM_CONFIG':str(path),'BB_DLSSG_STREAM_DIR':str(ipc),'BB_DLSSG_TRANSPORT':args.dlssg_transport,'BB_DLSSG_HELPER_WAKE':config['helper_wake'],'BB_DLSSG_PIPELINE':config['pipeline'],
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
    multiplier=config.get('multiplier',2)
    if env.get('BB_DLSSG_MULTIPLIER','2')!=str(multiplier):raise ValueError('DLSS multiplier environment drift')
    if type(multiplier) is not int or not 2<=multiplier<=6:raise ValueError('invalid sealed DLSS multiplier')
    if multiplier>2 and (config.get('transport')!='gpu-dmabuf-v1' or config.get('pipeline')!='consumer-queue-v1' or config['identities']['model']['sha256']!='ff6e90eb78b827927dff5b4ecc6b1c870c2e9bca29ed9f48c7d348cc9e170b82'):
        raise ValueError('MFG sealed runtime/pipeline mismatch')
    transport=config.get('transport','files-v1')
    schema='dlssg-native-gpu-v1' if transport=='gpu-dmabuf-v1' else 'dlssg-native-cpu-v1'
    if transport not in ('files-v1','mapped-v1','gpu-dmabuf-v1') or config.get('schema')!=schema:
        raise ValueError('DLSSG transport/schema mismatch')
    if env.get('BB_DLSSG_TRANSPORT',transport if transport!='gpu-dmabuf-v1' else None)!=transport:
        raise ValueError('DLSSG sealed transport drift')
    if config.get('helper_wake','poll-v1') not in ('poll-v1','notify-v1') or env.get('BB_DLSSG_HELPER_WAKE','poll-v1')!=config.get('helper_wake','poll-v1'):
        raise ValueError('DLSSG sealed Helper wake drift')
    # Read sealed historical r37 and rejected M2.3 input-reuse experiments for independent retirement
    # audit. New admission above/CLI never permits this rejected WSI mode.
    if config.get('pipeline','sync-v1') not in ('sync-v1','submit-ready-v1','late-ready-v1','consumer-ready-v1','consumer-input-ready-v1','consumer-queue-v1','gpu-gate-v1') or env.get('BB_DLSSG_PIPELINE','sync-v1')!=config.get('pipeline','sync-v1'):
        raise ValueError('DLSSG sealed pipeline drift')
    if config.get('pipeline','sync-v1')!='sync-v1' and transport!='gpu-dmabuf-v1':
        raise ValueError('DLSSG async prerequisite requires GPU transport')
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
