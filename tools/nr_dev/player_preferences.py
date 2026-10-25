"""Merge player menu changes after normal retirement, without replacing controls."""
import hashlib
import json
import os
from pathlib import Path
import uuid
import shutil

KEYS = set('menu_language upscaler preset sharpen sharpness jitter reactive object_motion '
           'reactive_scale reactive_threshold reactive_max show_fps mouse_capture '
           'fsr4_auto_exposure fsr4_invert_jitter model_lod aspect_ratio output_res '
           'live_resolution menu_pos skip_intro'.split()) | {
    'effect_' + name for name in ('chromatic_aberration','dof','motion_blur','ssao',
                                'game_aa','dynamic_shadows','ssr','bloom')}


def fields(text):
    out = {}
    for line in text.splitlines():
        if line.lstrip().startswith(('#',';')) or '=' not in line:continue
        key, value = (s.strip() for s in line.split('=',1))
        if key in out:raise ValueError('Duplicate player preference: '+key)
        out[key] = value
    return out


def commit(run, result):
    if (result.get('status') != 'PASS_NORMAL_TEARDOWN' or result.get('returncode') != 0
            or result.get('native_lastuse_proof') is not True or result.get('residuals') != []):
        return {'status':'retained_unretired'}
    obj=json.loads((Path(run)/'player-settings.json').read_text())
    target=Path(obj['ini_path']); private=Path(run)/'profile/bbport.ini'
    if target.is_symlink() or target.resolve()!=target or private.is_symlink():
        raise ValueError('Player preferences must be regular canonical files')
    original=target.read_bytes()
    if hashlib.sha256(original).hexdigest()!=obj.get('ini_sha256'):
        return {'status':'retained_external_changes'}
    if private.stat().st_size>65536:raise ValueError('Player preferences too large')
    before=fields(obj['ini_text']); after=fields(private.read_text())
    changes={k:v for k,v in after.items() if k in KEYS and before.get(k)!=v}
    if not changes:return {'status':'unchanged'}
    lines=[]; pending=dict(changes)
    for line in original.decode().splitlines():
        key=line.split('=',1)[0].strip() if '=' in line and not line.lstrip().startswith(('#',';')) else ''
        if key in pending:line=key+'='+pending.pop(key)
        lines.append(line)
    lines.extend(k+'='+v for k,v in pending.items())
    temp=target.with_name('.'+target.name+'.'+uuid.uuid4().hex)
    try:
        fd=os.open(temp,os.O_CREAT|os.O_EXCL|os.O_WRONLY,target.stat().st_mode&0o777)
        with os.fdopen(fd,'w') as stream:
            stream.write('\n'.join(lines)+'\n');stream.flush();os.fsync(stream.fileno())
        if target.read_bytes()!=original:return {'status':'retained_external_changes'}
        os.replace(temp,target)
    finally:
        if temp.exists():temp.unlink()
    return {'status':'saved','keys':sorted(changes)}


def prune_save_copies(root, current, *, apply=False):
    """Keep two recent normal player copies; preserve all lifecycle evidence/prefixes.

    Failed, non-player and unsynced sessions are never eligible. A dry-run list
    is returned before each deletion, and only the redundant savedata is pruned.
    """
    candidates=[]
    for run in Path(root).iterdir():
        if run.is_symlink() or not run.is_dir() or not (run/'player-settings.json').is_file():continue
        if (run/'profile/wine-prefix').exists():continue
        try:
            result=json.loads((run/'result.json').read_text())
            if not (result.get('status')=='PASS_NORMAL_TEARDOWN' and result.get('returncode')==0
                    and result.get('native_lastuse_proof') is True and result.get('residuals')==[]
                    and result.get('player_save_sync',{}).get('status')=='synced'):continue
            candidates.append(run)
        except (OSError,ValueError):continue
    candidates.sort(key=lambda p:(p/'result.json').stat().st_mtime,reverse=True)
    removed=[]
    for run in candidates[2:]:
        target=run/'profile/user/savedata'
        if run==Path(current) or target.is_symlink() or target.resolve()!=target or not target.is_dir():continue
        removed.append(str(target))
        if apply:shutil.rmtree(target)
    return {'mode':'apply' if apply else 'dry-run','redundant_save_copies':removed}
