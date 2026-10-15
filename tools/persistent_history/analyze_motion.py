#!/usr/bin/env python3
"""Read-only cold-capture geometry/colour gate analysis; not a full GPU replay.

All assets are size/SHA verified. Pair gates overlap; they are potential rejection
conditions, never causal attribution when candidate adoption or source gaps exist.
No image outputs or capture modifications. Requires numpy.
"""
import argparse
import hashlib
import json
from pathlib import Path
import numpy as np

EPS = 1 / 512
NAMES = {
    'raw': ('original-current720.rgba16f', '<f2', 4),
    'edit': ('active-edit-confidence720.rgba16f', '<f2', 4),
    'meta': ('active-depth-guide-age720.rgba32f', '<f4', 4),
    'depth': ('current-depth720.r32f', '<f4', 1),
    'motion': ('current-motion720.rg32f', '<f4', 2),
}


def load(path):
    m = json.loads(path.read_text())
    w, h = m['image_extent']
    assets = {a['file']: a for a in m['assets']}
    data = {}
    for key, (name, dtype, channels) in NAMES.items():
        if name not in assets:
            continue
        a = assets[name]
        blob = (path.parent / name).read_bytes()
        if len(blob) != a['bytes'] or hashlib.sha256(blob).hexdigest() != a['sha256']:
            raise ValueError(f'asset verification failed: {path.parent / name}')
        arr = np.frombuffer(blob, dtype=dtype).reshape(h, w, channels)
        data[key] = arr.astype(np.float32) if dtype == '<f2' else arr
    if m['copy_observed_tick'] < m['copy_target_tick']:
        raise ValueError('capture lacks completed native tick proof')
    return m, data


def same_surface(a, b, projection_z, tolerance):
    finite = np.isfinite(a) & np.isfinite(b) & (a >= 0) & (a <= 1) & (b >= 0) & (b <= 1)
    return finite & np.where((a >= 1) | (b >= 1), (a >= 1) & (b >= 1),
                            np.abs(a-b) <= np.maximum(2e-7, np.abs(projection_z-b)*tolerance))


def guide(packed):
    u = np.ascontiguousarray(packed).view(np.uint32)
    q = np.stack((u & 127, (u >> 7) & 255, (u >> 15) & 255), axis=-1)
    return np.maximum(np.exp2(q / np.array([127, 255, 255])*32-16)-EPS, 0)


def pair(pm, p, m, d):
    missing = sorted({'raw', 'edit', 'meta', 'depth', 'motion'} - d.keys())
    missing += ['previous.'+k for k in ('edit', 'meta') if k not in p]
    if missing:
        return {'unavailable': missing}
    if m['previous_source'] != pm['current_source'] or m['visual_generation'] != pm['visual_generation']:
        return {'unavailable': 'not adjacent sources in same visual generation'}
    h, w = d['depth'].shape[:2]
    y, x = np.indices((h, w), dtype=np.float32)
    mv = d['motion']
    jitter = np.asarray(m['previous_jitter'])[:2] - np.asarray(m['current_jitter'])[:2]
    qx, qy = x+.5+mv[..., 0]+jitter[0], y+.5+mv[..., 1]+jitter[1]
    inside = np.isfinite(qx) & np.isfinite(qy) & (qx >= 0) & (qy >= 0) & (qx < w) & (qy < h)
    ix = np.nan_to_num(qx, nan=0, posinf=0, neginf=0).clip(0, w-1).astype(int)
    iy = np.nan_to_num(qy, nan=0, posinf=0, neginf=0).clip(0, h-1).astype(int)
    meta, edit = p['meta'][iy, ix], p['edit'][iy, ix]
    z = d['depth'][..., 0]
    cp, pp, row = (np.asarray(m[k]) for k in ('current_projection', 'previous_projection', 'previous_view_z'))
    with np.errstate(all='ignore'):
        vz = cp[3] / (z-cp[2])
        xx = ((x+.5-m['current_jitter'][0])/w*2-1)*vz/cp[0]
        yy = ((y+.5-m['current_jitter'][1])/h*2-1)*vz/cp[1]
        prevz = row[0]*xx + row[1]*yy + row[2]*vz + row[3]
        expected = np.where(z >= 1, 1, pp[2]+pp[3]/prevz)
        geometry = np.isfinite(expected) & (expected >= 0) & (expected <= 1) & ((z >= 1) | ((vz > 0) & (prevz > 0)))
        projection_ok = all(np.all(np.isfinite(v)) for v in (cp, pp, row)) and cp[0] != 0 and cp[1] != 0 and pp[0] != 0 and pp[1] != 0 and cp[2] >= 1 and pp[2] >= 1 and cp[3] < 0 and pp[3] < 0
        geometry &= projection_ok & np.isfinite(z) & (z >= 0) & (z <= 1)
        colour_delta = np.abs(np.log2((np.maximum(d['raw'][..., :3], 0)+EPS)/(guide(meta[..., 1])+EPS)))
        colour = np.all(colour_delta <= m['colour_tolerance'], axis=-1)
    edge = np.ones((h, w), dtype=bool)
    pad = np.pad(z, 1, mode='edge')
    for dy in range(3):
        for dx in range(3):
            edge &= same_surface(pad[dy:dy+h, dx:dx+w], z, cp[2], m['depth_tolerance'])
    eligible = inside & (edit[..., 3] > 0)
    count = int(eligible.sum())
    gates = {'geometry': geometry, 'reprojected_depth': same_surface(meta[..., 0], expected, pp[2], m['depth_tolerance']), 'current_3x3_depth': edge, 'colour': colour}
    current_origin = m['current_source']-d['meta'][..., 2]
    previous_origin = pm['current_source']-meta[..., 2]
    regions = {}
    for ry in range(3):
        for rx in range(3):
            sl = np.s_[ry*h//3:(ry+1)*h//3, rx*w//3:(rx+1)*w//3]
            base = eligible[sl]
            n = int(base.sum())
            current_valid = d['edit'][sl][..., 3] > 0
            ages = d['meta'][sl][..., 2:4][current_valid]
            motion_lengths = np.linalg.norm(mv[sl], axis=-1)
            finite_motion = motion_lengths[np.isfinite(motion_lengths)]
            regions[f'row{ry}_col{rx}'] = {
                'bounds_xyxy': [rx*w//3, ry*h//3, (rx+1)*w//3, (ry+1)*h//3],
                'eligible_prior_pixels': n,
                'current_valid_fraction': float(current_valid.mean()),
                'age_frames_min_max': [float(ages[:,0].min()), float(ages[:,0].max())] if ages.size else None,
                'age_ms_min_max': [float(ages[:,1].min()), float(ages[:,1].max())] if ages.size else None,
                'MV_length_pixels_p50_p95': np.percentile(finite_motion, [50,95]).tolist() if finite_motion.size else None,
                'potential_rejection_fraction_of_eligible': {k: float((base & ~v[sl]).sum()/n) if n else None for k,v in gates.items()},
                'depth_colour_reject_overlap_pixels': int((base & ~gates['reprojected_depth'][sl] & ~gates['colour'][sl]).sum()),
            }
    return {
        'regions_3x3': regions,
        'eligible_prior_pixels': count,
        'motion_outside_or_nonfinite_fraction': float((~inside).mean()),
        'potential_rejection_fraction_of_eligible': {k: float((eligible & ~v).sum()/count) if count else None for k,v in gates.items()},
        'all_geometry_colour_gates_pass_pixels': int((eligible & np.logical_and.reduce(list(gates.values()))).sum()),
        'current_valid_with_different_model_origin_pixels': int((eligible & (d['edit'][..., 3] > 0) & (np.abs(current_origin-previous_origin) > .1)).sum()),
        'candidate_recorded_this_source': m.get('candidate_recorded_this_source'),
        'limitations': 'Overlapping potential gates only. Does not replay candidate catch-up/adoption, TTL deltaMs, bank invalid propagation, or Return. New candidate may replace prior origin. ExposureRatio assumed actual default 1.',
    }


def self_test():
    a = np.array([.5, 1., np.nan], dtype=np.float32)
    assert same_surface(a, a, 1., .02).tolist() == [True, True, False]
    e = np.array([0., 1.])
    for threshold in (0., .25, .5, 1.):
        assert ((e > 0) & (e >= threshold)).tolist() == [False, True]
    # Shader's pixel-center + backward MV + previous-current jitter convention.
    assert int(4+.5+2+(.25-(-.25))) == 7
    shape = (3, 4)
    raw = np.ones((*shape, 4), dtype=np.float32)
    edit = raw.copy()
    meta = np.zeros_like(raw)
    meta[..., 0] = .5
    # RGB log zero quantizes to midpoint codes; exponent keeps packed float finite.
    bits = np.full(shape, 0x3f800000 | 64 | (128 << 7) | (128 << 15), dtype=np.uint32)
    meta[..., 1] = bits.view(np.float32)
    meta[..., 2:] = 1
    data = dict(raw=raw, edit=edit, meta=meta, depth=meta[..., :1],
                motion=np.zeros((*shape, 2), dtype=np.float32))
    m = dict(current_source=2, previous_source=1, visual_generation=1,
             current_jitter=[0, 0], previous_jitter=[0, 0],
             current_projection=[1, 1, 1.01, -.1],
             previous_projection=[1, 1, 1.01, -.1], previous_view_z=[0, 0, 1, 0],
             depth_tolerance=.02, colour_tolerance=.5)
    previous = dict(m, current_source=1)
    result = pair(previous, data, m, data)
    assert result['all_geometry_colour_gates_pass_pixels'] == 12
    data['motion'][..., 0] = 1
    assert pair(previous, data, m, data)['eligible_prior_pixels'] == 9
    print('self-test PASS')


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('capture', nargs='?', type=Path)
    parser.add_argument('--self-test', action='store_true')
    args = parser.parse_args()
    if args.self_test:
        self_test()
        return
    if not args.capture:
        parser.error('capture path required')
    paths = sorted(args.capture.glob('frame-*/readback.json'), key=lambda p: json.loads(p.read_text())['current_source'])
    if not paths:
        parser.error('no frame readback manifests')
    output, previous = [], None
    for path in paths:
        m, d = load(path)
        values, counts = np.unique(d['edit'][..., 3], return_counts=True)
        item = {'source': m['current_source'], 'manifest': str(path), 'control_revision': m.get('control_revision'), 'confidence_counts': {str(float(v)): int(c) for v,c in zip(values,counts)}, 'min_confidence': m['min_confidence'] if 'min_confidence' in m else None}
        if previous:
            item['pair'] = pair(*previous, m, d)
        output.append(item)
        previous = m, d
    print(json.dumps(output, indent=2, allow_nan=False))


if __name__ == '__main__':
    main()
