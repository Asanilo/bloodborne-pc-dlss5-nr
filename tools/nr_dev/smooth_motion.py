"""Explicit driver-level SM experiment; never combined with application FG."""
from pathlib import Path


def validate_args(args):
    if not getattr(args, 'smooth_motion', False):
        return
    if not getattr(args, 'daily', False) or getattr(args, 'experimental_fg', None) or getattr(args, 'fg_native', False):
        raise ValueError('Smooth Motion requires daily mode without application FG')


def check_capability(args, build):
    if getattr(args, 'smooth_motion', False) and b'SM_EXIT_PROOF' not in (Path(build) / 'gpu/libbbgpu.so').read_bytes():
        raise ValueError('Smooth Motion requires a rebuilt Native with driver-layer retirement')


def environment(run):
    return {'NVPRESENT_ENABLE_SMOOTH_MOTION': '1', 'NVPRESENT_LOG_LEVEL': '4',
            'NVPRESENT_LOG_FILE': str(run / 'nvpresent.log'),
            'VK_LOADER_LAYERS_ALLOW': 'VK_LAYER_NV_present',
            'VK_LOADER_DEBUG': 'layer', 'BB_FRAME_GEN_AUDIT': '1',
            'BB_FPS_LIMIT': '60', 'BB_PRESENT_MODE': 'Immediate'}


def validate_manifest(run, manifest):
    env = manifest.get('environment', {})
    enabled = manifest.get('smooth_motion', False)
    if type(enabled) is not bool:
        raise ValueError('Smooth Motion manifest mode must be boolean')
    if enabled:
        if env.get('BB_FRAME_GEN') not in (None, '0') or env.get('BB_FRAME_GEN_NATIVE') is not None:
            raise ValueError('Smooth Motion and application FG are mutually exclusive')
        if env.get('VK_LOADER_LAYERS_DISABLE') != '~implicit~' or any(env.get(k) != v for k, v in environment(run).items()):
            raise ValueError('Smooth Motion private layer environment drift')
    elif any(k in env for k in ('NVPRESENT_ENABLE_SMOOTH_MOTION', 'VK_LOADER_LAYERS_ALLOW')):
        raise ValueError('Unselected Smooth Motion layer environment')
