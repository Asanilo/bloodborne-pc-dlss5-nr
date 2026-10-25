"""FG-only next-launch preferences, committed after real session retirement.

Never changes a live sealed launch or NR/SR settings. Owners separate the ordinary
launcher from a private preview that happens to share the same game data folder.
"""
import hashlib
import json
import os
from pathlib import Path
import uuid

NAME = 'fg-preferences.json'
BACKENDS = ('off', 'fsr31', 'dlssg', 'fsr316')


def path_for(owner, data):
    key = hashlib.sha256(str(owner).encode()).hexdigest()[:16]
    return Path(data) / ('fg-preferences-' + key + '.json')


def digest(path):
    return hashlib.sha256(Path(path).read_bytes()).hexdigest()


def values(backend, multiplier, enabled):
    if backend not in BACKENDS:
        raise ValueError('FG backend is unavailable: ' + str(backend))
    if type(multiplier) is not int or not 2 <= multiplier <= 6 or type(enabled) is not bool:
        raise ValueError('Invalid saved FG multiplier or enable state')
    if backend in ('fsr31', 'fsr316') and multiplier != 2:
        raise ValueError('FSR FG supports x2')
    return dict(fg_backend=backend, fg_dlss_multiplier=multiplier,
                fg_enabled=enabled and backend != 'off')


def read(owner, data):
    path = path_for(owner, data)
    if not path.exists() and not path.is_symlink():
        return {}
    if path.is_symlink() or path.resolve() != path or not path.is_file() or path.stat().st_size > 4096:
        raise ValueError('Invalid FG preferences path')
    obj = json.loads(path.read_text())
    if obj.get('schema') != 'bbport-fg-preferences-v1':
        raise ValueError('Unsupported FG preferences schema')
    if obj.get('owner') != str(owner):
        return {}
    return values(obj['backend'], obj['multiplier'], obj['enabled'])


def commit(run, result):
    if (result.get('status') != 'PASS_NORMAL_TEARDOWN' or result.get('returncode') != 0
            or result.get('native_lastuse_proof') is not True or result.get('residuals') != []):
        return {'status': 'not_applied_unretired'}
    snapshot = Path(run) / 'player-settings.json'
    if not snapshot.exists():
        return {'status': 'no_player_settings'}
    obj = json.loads(snapshot.read_text())
    ini = Path(run) / 'profile/bbport.ini'
    if ini.is_symlink() or ini.stat().st_size > 65536:
        raise ValueError('Invalid FG settings request')
    fields = {}
    for line in ini.read_text().splitlines():
        if '=' in line and not line.lstrip().startswith(('#', ';')):
            key, value = (s.strip() for s in line.split('=', 1))
            if key.startswith('frame_generation'):
                if key in fields:
                    raise ValueError('Duplicate FG preference')
                fields[key] = value
    if 'frame_generation_backend' not in fields:
        return {'status': 'no_game_request'}
    backend = fields['frame_generation_backend']
    multiplier = int(fields['frame_generation_multiplier'])
    if fields.get('frame_generation') not in ('0', '1'):
        raise ValueError('Invalid FG enable request')
    selected = values(backend, multiplier, fields['frame_generation'] == '1')
    owner = Path(obj['fg_preferences_owner'])
    if owner.is_symlink() or owner.resolve() != owner or digest(owner) != obj['fg_preferences_owner_sha256']:
        return {'status': 'retained_owner_changed'}
    data = Path(obj['data_dir'])
    if data.resolve() != data or data.is_symlink():
        raise ValueError('Invalid FG preferences directory')
    path = path_for(owner, data)
    if path.is_symlink():
        raise ValueError('FG preferences target is a symlink')
    record = dict(schema='bbport-fg-preferences-v1', owner=str(owner),
                  backend=backend, multiplier=multiplier, enabled=selected['fg_enabled'],
                  retired_session=Path(run).name)
    temp = data / (NAME + '.' + uuid.uuid4().hex + '.tmp')
    try:
        fd = os.open(temp, os.O_WRONLY | os.O_CREAT | os.O_EXCL, 0o600)
        with os.fdopen(fd, 'w') as out:
            json.dump(record, out);out.write('\n');out.flush();os.fsync(out.fileno())
        os.replace(temp, path)
    finally:
        if temp.exists():temp.unlink()
    return {'status': 'saved', 'path': str(path), **selected}
