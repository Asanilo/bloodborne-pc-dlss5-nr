"""Full-launcher settings adapter; contains no GPU or account implementation."""
import hashlib
import importlib.util
import json
import os
import re
from pathlib import Path


def add_arguments(parser):
    parser.add_argument('--launcher-settings')
    parser.add_argument('--launcher-ini')
    parser.add_argument('--player-data-dir')
    parser.add_argument('--sr-only', action='store_true')


def canonical(value, kind=None):
    p = Path(value).expanduser()
    if (not p.is_absolute() or str(p) != str(value) or p.resolve() != p
            or p.is_symlink() or any(c in str(p) for c in ('\0', '\n', ':'))):
        raise ValueError('player settings: canonical absolute path required')
    if kind == 'file' and not p.is_file():
        raise ValueError('player settings: required file missing: ' + str(p))
    if kind == 'dir' and not p.is_dir():
        raise ValueError('player settings: required directory missing: ' + str(p))
    return p


def _json(path):
    p = canonical(path, 'file')
    if p.stat().st_size > 131072:
        raise ValueError('player settings: configuration too large')
    def unique(items):
        out = {}
        for key, value in items:
            if key in out:
                raise ValueError('player settings: duplicate JSON key')
            out[key] = value
        return out
    obj = json.loads(p.read_text(), object_pairs_hook=unique)
    if not isinstance(obj, dict):
        raise ValueError('player settings: expected object')
    return obj


def _text(value, limit=4096):
    if not isinstance(value, str) or len(value) > limit or any(c in value for c in ('\0', '\n', '\r')):
        raise ValueError('player settings: invalid string')
    return value


def load_request(args):
    fields = [getattr(args, k, None) for k in ('launcher_settings', 'launcher_ini', 'player_data_dir')]
    if not any(fields):
        return None
    if not all(fields) or not getattr(args, 'daily', False):
        raise ValueError('player settings require daily and all three launcher paths')
    if getattr(args, 'continue_session', None):
        raise ValueError('player settings use the selected save folder, not a diagnostic continuation')
    settings_path, ini_path = (canonical(x, 'file') for x in fields[:2])
    data = canonical(fields[2], 'dir')
    source = _json(str(settings_path))
    game = canonical(source.get('game_dir', ''), 'dir')
    canonical(str(game / 'eboot.bin'), 'file')
    user = canonical(source.get('user_dir') or str(data / 'user'))
    if user in (Path('/'), Path.home(), data, game):
        raise ValueError('player settings: dedicated save directory required')
    if ini_path.stat().st_size > 65536:
        raise ValueError('player settings: INI too large')
    ini_text = ini_path.read_text()
    ini = {}
    for line in ini_text.splitlines():
        if not line.strip() or line.lstrip().startswith(('#', ';')) or '=' not in line:
            continue
        k, v = (x.strip() for x in line.split('=', 1))
        if not re.fullmatch(r'[A-Za-z][A-Za-z0-9_.-]{0,80}', k):
            raise ValueError('player settings: invalid INI key')
        ini[k] = _text(v, 1024)
    nr = not getattr(args, 'sr_only', False)
    if nr:
        required = {'upscaler': 'dlss', 'preset': '1', 'output_res': '1920x1080', 'object_motion': '1'}
        dimensions = getattr(args, 'nr_sr_preset', None)
        if dimensions:
            dimension_spec=importlib.util.spec_from_file_location('player_dimensions',Path(__file__).with_name('dimension_profile.py'))
            module=importlib.util.module_from_spec(dimension_spec);dimension_spec.loader.exec_module(module)
            profile=module.select(args)
            required.update(preset=str(profile['preset']),output_res=f"{profile['output_width']}x{profile['output_height']}")
        mismatched = [k for k, v in required.items() if ini.get(k, v) != v]
        if ini.get('aspect_ratio', '16:9') not in ((profile['aspect_ratio'], 'auto') if dimensions else ('16:9', 'auto')):
            mismatched.append('aspect_ratio')
        if ini.get('jitter', '1') != '1':
            mismatched.append('jitter')
        if mismatched:
            raise ValueError('NR requires matching SR profile, DLSS, jitter/object motion on; change: ' + ', '.join(mismatched))
        if source.get('memory_model', 'auto') not in ('auto', 'old') or source.get('as_0_3', False):
            raise ValueError('NR requires the validated old memory model and current synchronization')
    upscaler = ini.get('upscaler', 'dlss')
    if upscaler not in ('dlss', 'fsr3', 'fsr4', 'fsr411', 'taa', 'off'):
        raise ValueError('player settings: unsupported upscaler')
    env = {'BB_GAME_DIR': str(game), 'BB_PC_MODEL': '1' if source.get('memory_model') == 'new' else '0',
           'BB_UPSCALER': upscaler, 'BB_DLSS': '1' if upscaler == 'dlss' else '0',
           'BB_UPSCALE_PRESET': ini.get('preset', '1'), 'BB_JITTER': ini.get('jitter', '1'),
           'BB_OBJECT_MOTION': ini.get('object_motion', '1')}
    selected = {}
    booleans = {'fullscreen': 'BB_FULLSCREEN', 'hdr': 'BB_HDR', 'mods_enabled': 'BB_MODS_ENABLED',
                'skip_network_choice': 'BB_SKIP_NETWORK_CHOICE', 'save_log': 'BB_SAVE_LOG',
                'mangohud': 'MANGOHUD', 'frame_stats': 'BB_FRAME_STATS', 'gpu_profile': 'BB_GPU_PROFILE',
                'vk_validation': 'BB_VK_VALIDATION', 'as_0_3': 'BB_AS_0_3'}
    for key, target in booleans.items():
        value = source.get(key, key in ('mods_enabled', 'skip_network_choice'))
        if type(value) is not bool:
            raise ValueError('player settings: boolean required for ' + key)
        env[target] = '1' if value else '0'
        selected[key] = value
    env['DISABLE_MANGOHUD'] = '0' if selected['mangohud'] else '1'
    strings = {'present_mode': ('BB_PRESENT_MODE', 'Mailbox'), 'gamepad': ('BB_GAMEPAD', ''),
               'display': ('BB_DISPLAY', ''), 'draw_pipe': ('BB_DRAW_PIPE', ''),
               'readbacks': ('BB_READBACKS', ''), 'preupload': ('BB_PREUPLOAD', '')}
    for key, (target, default) in strings.items():
        value = _text(source.get(key, default))
        if value:
            env[target] = value
        selected[key] = value
    language = _text(source.get('language', '1'))
    if language not in {str(i) for i in range(21)}:
        raise ValueError('player settings: unsupported game language')
    ui = _text(source.get('ui_language', ''))
    env.update(BB_LANGUAGE=language, BB_MENU_LANGUAGE=ui if ui in ('zh_CN', 'en', 'ru', 'pt_BR') else 'en')
    fps = _text(source.get('fps_mode', 'uncap'))
    limit = source.get('fps_limit', 0)
    if fps not in ('uncap', '30', '60', '90') or type(limit) is not int or not 0 <= limit <= 1000:
        raise ValueError('player settings: invalid FPS option')
    env.update(BB_FPS=fps, BB_FPS_LIMIT=str(limit))
    selected.update(language=language, ui_language=ui, fps_mode=fps, fps_limit=limit,
                    memory_model=source.get('memory_model', 'auto'))
    for key, target, default in [('mods_dir', 'BB_MODS_DIR', data / 'mods'), ('patches_dir', 'BB_PATCHES_DIR', data / 'patches')]:
        env[target] = str(canonical(source.get(key) or str(default)))
    env.update(BB_MODS_CONFIG=str(data / 'mods.json'), BB_PATCHES_CONFIG=str(data / 'patches.json'),
               BB_FSR4_DIR=str(data / 'fsr4_shaders'), BB_FSR411_DIR=str(data / 'fsr4_411'))
    if source.get('crash_diag', False):
        env.update(BB_FREE_CHECK='1', BB_WRITE_LOG='1', BB_PRODUCER_CHECK='1')
    extra = _text(source.get('extra_env', ''))
    permitted = {'BB_MENU_POINTER', 'BB_CURSOR', 'BB_CJK_FONT', 'BB_SAVE_LOG', 'BB_FPS_LIMIT'}
    for item in extra.split():
        if '=' not in item:
            raise ValueError('player settings: extra variables require NAME=value')
        key, value = item.split('=', 1)
        if key not in permitted:
            raise ValueError('player settings: extra variable is not supported by the managed launcher: ' + key)
        env[key] = _text(value)
    # GPU selection is already enumerated by the launcher; use its saved index only.
    gpu_id = source.get('nrbridge_gpu_id')
    if gpu_id is not None:
        if type(gpu_id) is not int or not 0 <= gpu_id <= 31:
            raise ValueError('player settings: invalid GPU index')
        env['BB_GPU_ID'] = str(gpu_id)
    return {'schema': 'nrbridge-player-settings-v1', 'settings_path': str(settings_path),
            'ini_path': str(ini_path), 'data_dir': str(data), 'game_dir': str(game),
            'user_dir': str(user), 'nr_enabled': nr, 'settings': selected,
            'ini_text': ini_text, 'environment': env}


def runtime_for_player(args, home):
    """Derive pinned game selection without changing the seed or any old config."""
    request = load_request(args)
    if request is None:
        return args.local_inputs
    spec = importlib.util.spec_from_file_location('player_runtime_inputs', Path(__file__).with_name('runtime_inputs.py'))
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    binding = module.load(args.local_inputs, home)
    config = json.loads(json.dumps(binding['config']))
    if config['assets']['path'] == request['game_dir']:
        return args.local_inputs
    # This SHA-validated runtime inventory covers the whole GE/library closure.
    # It is not a launcher-settings file and can legitimately exceed 128 KiB.
    pins = module.read(binding['pins_path'])
    game = Path(request['game_dir']) / 'eboot.bin'
    pins['pins'] = [p for p in pins['pins'] if p['path'] != str(game)]
    pins['pins'].append({'path': str(game), 'real_path': str(game.resolve()), 'bytes': game.stat().st_size,
                         'sha256': module.sha(game)})
    pins['pins'].sort(key=lambda x: x['path'])
    config['assets']['path'] = request['game_dir']
    identity = hashlib.sha256((json.dumps(config, sort_keys=True) + json.dumps(pins, sort_keys=True)).encode()).hexdigest()[:24]
    pinpath = home / 'localinputs' / ('player-' + identity + '-pins.json')
    path = home / 'localinputs' / ('player-' + identity + '.json')
    _write_exact(pinpath, pins)
    config['pins'] = {'path': str(pinpath), 'sha256': module.sha(pinpath)}
    _write_exact(path, config)
    module.load(path, home)
    return str(path)


def _write_exact(path, value):
    data = (json.dumps(value, indent=2) + '\n').encode()
    if path.exists():
        if path.is_symlink() or path.read_bytes() != data:
            raise ValueError('player settings: derived configuration drift')
        return
    fd = os.open(path, os.O_WRONLY | os.O_CREAT | os.O_EXCL, 0o600)
    with os.fdopen(fd, 'wb') as out:
        out.write(data)
        out.flush()
        os.fsync(out.fileno())


def write_snapshot(run, request):
    path = run / 'player-settings.json'
    _write_exact(path, request)
    return {'path': str(path), 'sha256': hashlib.sha256(path.read_bytes()).hexdigest(),
            'nr_enabled': request['nr_enabled'], 'user_dir': request['user_dir']}


def verify_snapshot(run, record, environment):
    path = run / 'player-settings.json'
    if record.get('path') != str(path) or path.is_symlink() or hashlib.sha256(path.read_bytes()).hexdigest() != record.get('sha256'):
        raise ValueError('player settings: snapshot identity drift')
    obj = _json(str(path))
    if obj.get('schema') != 'nrbridge-player-settings-v1' or obj.get('nr_enabled') is not record.get('nr_enabled'):
        raise ValueError('player settings: snapshot schema drift')
    # Online selection can replace offline's network-choice field, never settings.
    for key, value in obj['environment'].items():
        if key != 'BB_SKIP_NETWORK_CHOICE' and environment.get(key) != value:
            raise ValueError('player settings: selected option drift: ' + key)
    return obj
