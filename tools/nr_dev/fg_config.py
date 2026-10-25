"""One managed FG selection. Legacy flags are derived compatibility inputs only."""
import json
import re
from pathlib import Path

BACKENDS = ('off', 'fsr31', 'dlssg', 'smooth-motion', 'fsr316')
LABELS = ('Off', 'FSR 3.1 x2 (HUDless)', 'NVIDIA DLSS FG / MFG', 'NVIDIA Smooth Motion', 'FSR 3.1.6 x2 (Experimental)')
PIPELINES = ('submit-ready-v1', 'consumer-queue-v1')
SM_UNAVAILABLE = ('Smooth Motion temporarily unavailable: M3 exit produced ErrorDeviceLost; '
                  'normal GPU retirement is unverified. Use Off, FSR or DLSS FG.')


def selection(settings):
    backend = settings.get('fg_backend')
    if backend is None:
        backend = 'fsr31' if settings.get('nrbridge_fsr_fg', False) else 'off'
    if backend not in BACKENDS:
        raise ValueError('Unavailable FG backend: ' + str(backend))
    pipeline = settings.get('fg_dlss_pipeline', 'submit-ready-v1')
    transport = settings.get('fg_dlss_transport', 'gpu-dmabuf-v1')
    if pipeline not in PIPELINES or transport not in ('gpu-dmabuf-v1', 'mapped-v1', 'files-v1'):
        raise ValueError('Unsupported DLSS FG pipeline/transport')
    multiplier=settings.get('fg_dlss_multiplier',2)
    if type(multiplier) is not int or not 2<=multiplier<=6:raise ValueError('DLSS multiplier must be 2..6')
    if multiplier>2 and transport=='gpu-dmabuf-v1':pipeline='consumer-queue-v1'
    return dict(multiplier=multiplier,backend=backend, pipeline=pipeline if transport=='gpu-dmabuf-v1' else 'sync-v1',
                transport=transport, model=settings.get('fg_dlss_model',''),
                runtime=settings.get('fg_dlss_runtime',''), prefix_session=settings.get('fg_dlss_prefix_session',''))


def discover(home):
    """Read the last selected FG inputs; never create a prefix or change settings."""
    root=Path(home)
    candidates=[]
    try:
        name=json.loads((root/'state.json').read_text()).get('last_session','')
        if re.fullmatch(r'[A-Za-z0-9][A-Za-z0-9-]{0,62}',name):candidates.append(root/'runs'/name/'dlssg-config.json')
    except (OSError,ValueError,TypeError):pass
    candidates += sorted((root/'runs').glob('*/dlssg-config.json'),key=lambda p:p.stat().st_mtime,reverse=True)[:32]
    for path in candidates:
        try:
            if path.is_symlink() or path.stat().st_size>131072:continue
            if json.loads((path.parent/'result.json').read_text()).get('status')!='PASS_NORMAL_TEARDOWN':continue
            c=json.loads(path.read_text());name=c['reused_retired_prefix']
            if not re.fullmatch(r'[A-Za-z0-9][A-Za-z0-9-]{0,62}',name):continue
            if not (root/'runs'/name/'profile/wine-prefix').is_dir():continue
            return dict(fg_dlss_model=c['model'],fg_dlss_runtime=str(Path(c['executable']).parent),fg_dlss_prefix_session=name)
        except (OSError,ValueError,KeyError,TypeError):continue
    return {}


def command_options(config):
    result=['--fg-backend',config['backend']]
    if config['backend']=='dlssg':
        result += ['--dlssg-multiplier',str(config.get('multiplier',2)),'--dlssg-transport',config['transport'],'--dlssg-pipeline',config['pipeline'],
                   '--dlssg-model',config['model'],'--dlssg-runtime',config['runtime'],
                   '--dlssg-prefix-session',config['prefix_session']]
    return result


def add_argument(parser):
    parser.add_argument('--fg-start-off',action='store_true',help='keep the selected application FG backend available, initially disabled; toggle in game')
    parser.add_argument('--dlssg-multiplier',type=int,choices=range(2,7),default=2)
    parser.add_argument('--fg-backend',choices=BACKENDS,default=None,
                        help='one FG backend; NR and SR remain independent')


def normalize(args):
    backend=getattr(args,'fg_backend',None)
    if getattr(args,'dlssg_transport',None) is None:args.dlssg_transport='gpu-dmabuf-v1' if backend=='dlssg' else 'mapped-v1'
    if getattr(args,'dlssg_pipeline',None) is None:args.dlssg_pipeline=('consumer-queue-v1' if getattr(args,'dlssg_multiplier',2)>2 else 'submit-ready-v1') if backend=='dlssg' and args.dlssg_transport=='gpu-dmabuf-v1' else 'sync-v1'
    legacy=getattr(args,'experimental_fg',None)
    sm=getattr(args,'smooth_motion',False)
    if getattr(args,'fg_start_off',False) and (backend not in ('fsr31','fsr316','dlssg') or sm):
        raise ValueError('--fg-start-off requires one application FG backend')
    if backend is None:
        # Historical diagnostic capture/plain-FSR paths retain their exact flags.
        return
    if backend=='smooth-motion':raise ValueError(SM_UNAVAILABLE)
    expected={'fsr31':'fsr31-hudless','fsr316':'fsr316','dlssg':'dlssg'}.get(backend)
    if legacy and legacy!=expected or sm and backend!='smooth-motion':
        raise ValueError('Conflicting FG backend selections; choose exactly one')
    if getattr(args,'fg_native',False) and not expected:
        raise ValueError('Off/Smooth Motion cannot enable application FG')
    args.experimental_fg=expected
    args.smooth_motion=backend=='smooth-motion'
    args.fg_native=bool(expected)


def requested(args):
    if getattr(args,'fg_backend',None) is not None:return args.fg_backend
    if getattr(args,'smooth_motion',False):return 'smooth-motion'
    return {'fsr31':'fsr31','fsr31-hudless':'fsr31','fsr316':'fsr316','dlssg':'dlssg',
            'dlssg-capture':'dlssg-capture'}.get(getattr(args,'experimental_fg',None),'off')


def validate_environment(backend,env):
    if backend not in (*BACKENDS,'dlssg-capture'):raise ValueError('unknown sealed FG backend')
    app=backend in ('fsr31','fsr316','dlssg','dlssg-capture')
    if (env.get('BB_FRAME_GEN')=='1')!=app:
        raise ValueError('FG application enable/selection drift')
    if (env.get('NVPRESENT_ENABLE_SMOOTH_MOTION')=='1')!=(backend=='smooth-motion'):
        raise ValueError('FG driver enable/selection drift')
    if env.get('BB_FRAME_GEN_BACKEND')!=backend:
        raise ValueError('FG backend environment drift')
    if not app and env.get('BB_FRAME_GEN_NATIVE'):
        raise ValueError('driver/off backend cannot acquire Native FG input')


def availability(config,home):
    """Cheap UI checks. The locked runner performs hashes, GPU identity and retirement checks."""
    backend=config['backend']
    if backend=='smooth-motion':
        raise ValueError(SM_UNAVAILABLE)
    if backend=='dlssg':
        if config.get('multiplier',2)>2 and (config['transport']!='gpu-dmabuf-v1' or config['pipeline']!='consumer-queue-v1'):
            raise ValueError('DLSS 3X–6X requires GPU DMA-BUF and consumer-queue-v1; x2 retains all fallbacks')
        if not config['model'] or not Path(config['model']).is_file():
            raise ValueError('DLSS FG unavailable: choose the existing nvngx_dlssg.dll model')
        runtime=Path(config['runtime'])
        if not config['runtime'] or any(not (runtime/n).is_file() for n in ('m2-wine-probe-build.json','wine-dlssg-probe.exe','nvngx.dll_dlssg.dll','nvcuda.dll')):
            raise ValueError('DLSS FG unavailable: choose a built FG Helper runtime directory')
        name=config['prefix_session']
        if not re.fullmatch(r'[A-Za-z0-9][A-Za-z0-9-]{0,62}',name):
            raise ValueError('DLSS FG unavailable: choose a normally retired FG prefix session')
        r=Path(home)/'runs'/name
        try:
            if json.loads((r/'result.json').read_text()).get('status')!='PASS_NORMAL_TEARDOWN' or not (r/'profile/wine-prefix').is_dir():raise ValueError()
        except (OSError,ValueError):raise ValueError('DLSS FG unavailable: the selected prefix is missing or not normally retired')
        return f"DLSS FG {config.get('multiplier',2)}X; Actual DLSS SR render/output dimensions; NR is independent. Backend changes require restart."
    return 'Backend changes require restart. FSR/DLSS can be enabled or disabled in the game.'
