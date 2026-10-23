#!/usr/bin/env python3
"""Bind six retired Bloodborne captures and reconstruct their unjittered cameras."""
import argparse
import hashlib
import json
from pathlib import Path
import numpy as np


def digest(path):
    with path.open('rb') as stream:
        return hashlib.file_digest(stream, 'sha256').hexdigest()


def matrices(frame):
    near, far, fov = [float(frame[k]) for k in ['near', 'far', 'vertical_fov']]
    if not (0 < near < far and 0 < fov < np.pi):
        raise ValueError('finite positive-z perspective required')
    pos = np.array(frame['camera_position'], dtype=np.float64)
    basis = np.array([frame[k] for k in ['camera_right', 'camera_up', 'camera_forward']], dtype=np.float64)
    if not np.isfinite(pos).all() or not np.isfinite(basis).all() or not np.allclose(basis @ basis.T, np.eye(3), atol=1e-4) or np.linalg.det(basis) < .999:
        raise ValueError('orthonormal positive-z camera required')
    # NVIDIA row-vector matrices, standard clip +Y up, positive view Z, depth [0,1].
    # Motion remains in screen pixels (previous-current); normalized by NGX.
    q = 1 / np.tan(fov / 2)
    projection = np.array([[q/(frame['width']/frame['height']),0,0,0],
                           [0,q,0,0], [0,0,far/(far-near),1],
                           [0,0,-near*far/(far-near),0]], dtype=np.float64)
    view = np.eye(4)
    view[:3,:3] = basis.T
    view[3,:3] = -pos @ basis.T
    return projection, view


def prepare(directory):
    directory = Path(directory).resolve(strict=True)
    run = directory.parent
    result = json.loads((run/'result.json').read_text())
    if result['status'] != 'PASS_NORMAL_TEARDOWN' or result['native_lastuse_proof'] is not True:
        raise ValueError('normally retired capture required')
    frames, previous, previous_source = [], None, 0
    def packed(m):
        if not np.isfinite(m).all():
            raise ValueError('finite matrix required')
        return m.astype('<f4').ravel().tolist()
    for n in range(6):
        path = directory/f'frame-{n}.json'
        f = json.loads(path.read_text())
        if f['schema'] != 'dlssg-bloodborne-input-v1' or f['index'] != n or f['actual_GPU_complete'] is not True or f['generated_frames'] != 0:
            raise ValueError('actual real-source capture metadata required')
        if [f[k] for k in ['width','height','render_width','render_height']] != [1920,1080,1280,720] or f['color_format'] not in [37,44]:
            raise ValueError('fixed 1080p Quality format required')
        if n and (f['source_game_frame'] != previous_source+1 or f['previous_source_game_frame'] != previous_source):
            raise ValueError('consecutive real-source IDs required')
        files = {}
        for role, size in [('color',1920*1080*4),('depth',1280*720*4),('motion',1280*720*4)]:
            p = directory/f'frame-{n}-{role}.raw'
            if p.is_symlink() or p.stat().st_size != size:
                raise ValueError('bounded complete capture required')
            files[role] = {'path':str(p), 'bytes':size, 'sha256':digest(p)}
        d = np.fromfile(files['depth']['path'], dtype='<f4')
        mv = np.fromfile(files['motion']['path'], dtype='<f2')
        if not np.isfinite(d).all() or not np.isfinite(mv).all() or d.min()<0 or d.max()>1:
            raise ValueError('finite depth [0,1] and motion required')
        projection, view = matrices(f)
        vp = view @ projection
        to_previous = np.linalg.inv(vp) @ previous if previous is not None else np.eye(4)
        f.update(files=files, metadata_sha256=digest(path),
                 camera_view_to_clip=packed(projection), clip_to_camera_view=packed(np.linalg.inv(projection)),
                 clip_to_previous_clip=packed(to_previous), previous_clip_to_clip=packed(np.linalg.inv(to_previous)))
        frames.append(f)
        previous, previous_source = vp, f['source_game_frame']
    return {'schema':'dlssg-game-input-fixture-v1','capture_directory':str(directory),
            'capture_result_sha256':digest(run/'result.json'),'capture_manifest_sha256':digest(run/'manifest.json'),
            'scope':'offline captured Bloodborne inputs, no Native Present',
            'camera_convention':'reconstructed positive-z row-vector depth01 standard clip +Y up; screen-pixel MVs', 'frames':frames}


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('directory');p.add_argument('--output',required=True)
    a = p.parse_args()
    record = prepare(a.directory)
    Path(a.output).write_text(json.dumps(record,indent=2)+'\n')
    print('Bound 6 consecutive real-source frames; no image copies or GPU work.')


if __name__ == '__main__':
    main()
