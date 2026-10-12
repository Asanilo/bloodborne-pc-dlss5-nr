#!/usr/bin/env python3
"""Bounded CPU-only source-coordinate comparisons; stdout JSON, no GPU/model calls.

The fixed plan is docs/q2-source-phase-plan.json. Existing archives are read-only.
Physical output-pixel motion conversion is not an assertion about NR MVecScale.
"""
import argparse
import hashlib
import json
import re
from pathlib import Path

import numpy as np


def ref(p, base):
    data = p.read_bytes()
    return {"path": str(p.relative_to(base)), "bytes": len(data),
            "sha256": hashlib.sha256(data).hexdigest()}


def geometry(arrays, camera, matrix, prior):
    # Same fixed geometry-only screen and half-error budget as the accepted Q0 audit.
    depth = arrays["depth"][..., 0]
    h, w = depth.shape
    size = np.asarray((w, h), np.float32)
    y, x = np.indices((h, w), dtype=np.float32)
    center = np.stack((x + np.float32(.5), y + np.float32(.5)), -1)
    uv = center / size - camera["jitter"] / size
    ndc = uv * np.float32(2) - np.float32(1)
    z = np.where(depth >= 1, np.float32(1), camera["proj"][3] /
                 np.minimum(depth - camera["proj"][2], np.float32(-1e-8)))
    view = np.stack((ndc[..., 0] * z / camera["proj"][0],
                     ndc[..., 1] * z / camera["proj"][1], z,
                     (depth < 1).astype(np.float32)), -1)
    prev = np.stack([np.sum(view * row, axis=-1, dtype=np.float32)
                     for row in matrix], -1)
    prev_uv = prev[..., :2] * camera["prev_proj"][:2] / np.maximum(
        prev[..., 2:3], np.float32(1e-4)) * np.float32(.5) + np.float32(.5)
    predicted = (prev_uv - uv) * size
    dj = camera["prev_jitter"] - camera["jitter"]
    prev_center = center + predicted + dj
    mask = ((depth > 0) & (depth < 1) & np.isfinite(predicted).all(-1) &
            (prev[..., 2] > 1e-4) & (x >= 2) & (x < w-2) & (y >= 2) & (y < h-2) &
            (prev_center[..., 0] >= 2) & (prev_center[..., 0] < w-2) &
            (prev_center[..., 1] >= 2) & (prev_center[..., 1] < h-2) &
            (local_span(z) <= .02))
    if prior is not None:
        pd = prior["depth"][..., 0]
        pz = camera["prev_proj"][3] / np.minimum(pd-camera["prev_proj"][2], np.float32(-1e-8))
        ix = np.clip(np.floor(prev_center[..., 0]).astype(np.int64), 0, w-1)
        iy = np.clip(np.floor(prev_center[..., 1]).astype(np.int64), 0, h-1)
        mask &= ((pd[iy, ix] > 0) & (pd[iy, ix] < 1) &
                 (np.abs(pz[iy, ix]-prev[..., 2]) / np.maximum(np.abs(prev[..., 2]), 1e-6) <= .02) &
                 (local_span(pz)[iy, ix] <= .02))
    rounded = predicted.astype(np.float16)
    up = np.nextafter(rounded, np.float16(np.inf)).astype(np.float32)
    down = np.nextafter(rounded, np.float16(-np.inf)).astype(np.float32)
    budget = .5 * np.maximum(np.abs(up-rounded.astype(np.float32)),
                              np.abs(rounded.astype(np.float32)-down)) + np.float32(.01)
    return predicted, mask, budget, center


def local_span(z):
    lo, hi = z.copy(), z.copy()
    for dy in (-1, 0, 1):
        for dx in (-1, 0, 1):
            a = np.roll(np.roll(z, dy, 0), dx, 1)
            lo, hi = np.minimum(lo, a), np.maximum(hi, a)
    return (hi-lo) / np.maximum(np.abs(z), np.float32(1e-6))


def camera_line(line):
    words, result = line.split(), {}
    for key, count in (("view", 12), ("prev_view", 12), ("proj", 4),
                       ("prev_proj", 4), ("jitter", 2), ("prev_jitter", 2)):
        start = words.index(key)+1
        result[key] = np.asarray(words[start:start+count], np.float32)
    return result


def bilinear(a, pos):
    x, y = pos[:, 0], pos[:, 1]
    ix, iy = np.floor(x).astype(np.int64), np.floor(y).astype(np.int64)
    # Caller enforces a common strictly in-bounds domain for all hypotheses.
    fx, fy = x-ix, y-iy
    if a.ndim == 3:
        fx, fy = fx[:, None], fy[:, None]
    return ((1-fy)*((1-fx)*a[iy, ix]+fx*a[iy, ix+1]) +
            fy*((1-fx)*a[iy+1, ix]+fx*a[iy+1, ix+1]))


def image_features(a):
    rgb = a[..., :3].astype(np.float64)
    luma = np.log1p(np.maximum(rgb @ np.asarray((.2126, .7152, .0722)), 0))
    gy, gx = np.gradient(luma)
    return luma, np.stack((gx, gy), -1)


def metric(current, previous, cg, pg, subset):
    if not np.any(subset):
        return {"samples": 0, "status": "NO_SAMPLES"}
    c, p, g, h = current[subset], previous[subset], cg[subset], pg[subset]
    norm = np.sqrt(np.sum(g*g)*np.sum(h*h))
    return {"samples": int(subset.sum()), "luma_abs_error_p50_p95":
            np.quantile(np.abs(c-p), (.5, .95)).tolist(),
            "luma_mean_abs_error": float(np.mean(np.abs(c-p))),
            "gradient_joint_dot": float(np.sum(g*h)/norm) if norm > 0 else None}


def temporal(cur, prior, cam, mask, center, plan):
    h, w = mask.shape
    yy, xx = np.indices(mask.shape)
    screen = mask & (xx >= 40) & (xx < w-40) & (yy >= 40) & (yy < h-40)
    screen &= (xx % 8 == 0) & (yy % 8 == 0)
    q = center[screen].astype(np.float64)-.5
    mv = cur["motion"][screen].astype(np.float64)
    j, jp = cam["jitter"].astype(np.float64), cam["prev_jitter"].astype(np.float64)
    scale = np.asarray((cur["output"].shape[1]/w, cur["output"].shape[0]/h))
    assert np.array_equal(scale, (1.5, 1.5))
    phases = {"k0": (np.zeros(2), np.zeros(2)),
              "jitter_retained": (scale*j, scale*jp),
              "reverse_jitter": (2*scale*j, 2*scale*jp),
              "half_plus": (np.full(2, .5), np.full(2, .5)),
              "half_minus": (np.full(2, -.5), np.full(2, -.5))}
    motions = {"output_pixel_units_1.5": scale*mv,
               "raw_pixel_units_1": mv, "wrong_sign": -scale*mv,
               "double_prev_current_jitter": scale*(mv+jp-j),
               "double_current_prev_jitter": scale*(mv-jp+j)}
    candidates, common = {}, np.ones(len(q), bool)
    oh, ow = cur["output"].shape[:2]
    for phase, (k, kp) in phases.items():
        current_p = (q+.5)*scale-.5-scale*j+k
        for motion, delta in motions.items():
            prev_p = current_p + delta + kp-k
            candidates[phase+"/"+motion] = (current_p, prev_p)
            for pos in (current_p, prev_p):
                common &= ((pos[:, 0] >= 1) & (pos[:, 0] < ow-2) &
                           (pos[:, 1] >= 1) & (pos[:, 1] < oh-2))
    assert len(candidates) == plan["max_candidates"] == 25
    q = q[common]
    tile = ((q[:, 0]//160 + q[:, 1]//90).astype(np.int64) % 2)
    subsets = {"all": np.ones(len(q), bool), "fixed_even_tiles": tile == 0,
               "fixed_odd_tiles": tile == 1}
    for xside in range(2):
        for yside in range(2):
            subsets[f"quadrant_{xside}_{yside}"] = ((q[:, 0] >= w/2) == bool(xside)) & ((q[:, 1] >= h/2) == bool(yside))
    current, cg = image_features(cur["output"])
    previous, pg = image_features(prior["output"])
    result = {}
    for name, (cp, pp) in candidates.items():
        cp, pp = cp[common], pp[common]
        cv, pv, cgrad, pgrad = (bilinear(a, pos) for a, pos in
                              ((current, cp), (previous, pp), (cg, cp), (pg, pp)))
        result[name] = {k: metric(cv, pv, cgrad, pgrad, v) for k, v in subsets.items()}
    return {"samples_common_all_variants": len(q), "candidates": result,
            "interpretation": "Descriptive SR temporal reprojection only; no residual mask or exposure fitting. Unknown object regions and SR history can change ranking. NOT NR scale or phase acceptance."}


def q1_contract_evidence(poc):
    q1=[]
    for session, count, mode in [('q1-single-20261012', 1, 'single'),
                                 ('q1-resident4-20261012', 4, 'resident')]:
        folder=poc/'bbport/runs'/('q1-game-'+session)
        leaf=folder/'frames/bucket-000000'
        assert leaf.is_dir(), leaf
        paths=sorted(leaf.glob('input-*.json'))
        expected=[leaf/f'input-{session}-g0-f{f}.json' for f in range(count)]
        assert paths==expected, f'{session}: expected exact {count} input basenames, got {paths}'
        previous_source=0
        for f,p in enumerate(paths):
            v=json.loads(p.read_text())
            assert v['session']==session and v['generation']==0 and v['frame']==f
            assert v['mode']==mode and v['status']=='INPUT_READY' and v['op']=='input'
            source=v['source_game_frame']
            assert type(source) is int and source>0
            assert v['prev_source_game_frame']==previous_source
            assert f==0 or source==previous_source+1, f'{session}: nonadjacent source'
            previous_source=source
            assert (v['render_width'],v['render_height'],v['width'],v['height'])==(1280,720,1920,1080)
            assert v['quality_profile']['phase_correction']=='none'
            q1.append({'input':ref(p,poc),'source':v['source_game_frame'],
                       'guide_extent':[v['render_width'],v['render_height']],
                       'output_extent':[v['width'],v['height']],
                       'current_jitter':[v['viewport_jitter_x'],v['viewport_jitter_y']],
                       'previous_jitter':[v['previous_viewport_jitter_x'],v['previous_viewport_jitter_y']],
                       'NR_scale_candidate':[v['quality_profile']['nr_mvec_scale_x'],v['quality_profile']['nr_mvec_scale_y']],
                       'same_texel_conversion':'previously independently byte-verified; no spatial correction',
                       'camera_matrices':'NOT_RECORDED_IN_INPUT; no Q0 camera mask borrowed'})
    assert len(q1)==5
    return q1


def run(wt, poc):
    plan_path = wt/'docs/q2-source-phase-plan.json'
    plan = json.loads(plan_path.read_text())
    case = poc/'bbport/runs/q0-game-q0-quality-motion-20261011'
    audit_path = case/'root-resource-audit.json'
    log_path = poc/'runs/quality-capture-20261011T163641.147928Z/game.log'
    root = json.loads(audit_path.read_text())
    assert root["status"] == 'PASS_RESOURCE_EVIDENCE' and len(root["frames"]) == 4
    cameras, matrices = {}, {}
    for line in log_path.read_text().splitlines():
        if line.startswith('Dump camera: frame '):
            cameras[int(line.split()[3])] = camera_line(line)
        if line.startswith('Q0_CAMERA '):
            f = int(re.search(r'dump_frame=(\d+)', line)[1])
            matrices[f] = np.asarray(re.search(r'reproject=([^ ]+)', line)[1].rstrip(',').split(','), np.float32).reshape(3,4)
    data, caps, evidence = {}, {}, []
    for entry in root['frames']:
        cap = entry['capture'];f = int(cap['dump_frame']);caps[f] = cap
        assert cap['route']=='Run' and cap['SR_success']==cap['transfer_sources_legal']=='1'
        assert cap['render']=='1280x720' and cap['output']=='1920x1080'
        arrays = {}
        for role, item in entry['resources'].items():
            p = poc/item['path'];r = ref(p, poc)
            assert r['bytes']==item['bytes'] and r['sha256']==item['sha256'],p
            assert item['completion']['GPU_complete']==item['completion']['file_ok']=='1'
            h,w=(1080,1920) if role=='output' else (720,1280)
            ch=1 if role=='depth' else 2 if role=='motion' else 4
            arrays[role]=np.fromfile(p,dtype='<f4' if role=='depth' else '<f2').reshape(h,w,ch).astype(np.float32)
            assert np.isfinite(arrays[role]).all();evidence.append(r)
        data[f] = arrays
    original=json.loads((wt/'docs/q0-camera-audit.json').read_text())
    frames=[]
    for f in range(4):
        prior=data.get(f-1) if f in (1,3) else None
        pred,mask,budget,center=geometry(data[f],cameras[f],matrices[f],prior)
        dj=cameras[f]['prev_jitter']-cameras[f]['jitter']
        variants={'camera_render_pixels':pred,'wrong_sign':-pred,
                  'camera_output_pixels_1.5':pred*np.float32(1.5),
                  'camera_render_pixels_div_1.5':pred/np.float32(1.5),
                  'extra_previous_minus_current_jitter':pred+dj,
                  'extra_current_minus_previous_jitter':pred-dj}
        summaries={}
        for name,expected in variants.items():
            error=np.abs(data[f]['motion']-expected)[mask]
            bounded=np.all(error<=budget[mask],axis=1)
            summaries[name]={'samples':int(mask.sum()),'within_budget_pixels':int(bounded.sum()),
                             'within_budget_fraction':float(bounded.mean()),
                             'component_error_p50_p95_p99':np.quantile(error.max(axis=1),(.5,.95,.99)).tolist()}
        old=original['frames'][f]['comparisons']['camera_previous_minus_current']
        assert summaries['camera_render_pixels']['samples']==old['samples']
        assert summaries['camera_render_pixels']['within_budget_pixels']==old['within_budget_pixels']
        row={'dump_frame':f,'source':int(caps[f]['source_game_frame']),
             'jitter_render_pixels':cameras[f]['jitter'].tolist(),'MV_unit_variants':summaries,
             'original_Q0_camera_integer_counts_reproduced':True}
        if prior is not None:
            assert int(caps[f-1]['source_game_frame'])+1==row['source']
            row['SR_temporal_pair']=temporal(data[f],prior,cameras[f],mask,center,plan)
        frames.append(row)
    q1=q1_contract_evidence(poc)
    sources=[wt/'gpu/shadps4/video_core/host_shaders/camera_motion.comp',
             wt/'gpu/shadps4/video_core/renderer_vulkan/vk_camera_motion.cpp',
             wt/'gpu/shadps4/video_core/renderer_vulkan/vk_temporal_upscaler.cpp',
             wt/'gpu/dlss_bridge/bbport_dlss_bridge.cpp',wt/'tools/nr_probe/audit_q0_camera.py',
             wt/'gpu/shadps4/video_core/host_shaders/q1_game_bridge.comp']
    return {'status':'OFFLINE_SOURCE_COORDINATE_COMPARISON_ONLY','GPU':'NOT_RUN',
            'numpy_version':np.__version__,'script_sha256':hashlib.sha256(Path(__file__).read_bytes()).hexdigest(),
            'plan':ref(plan_path,poc),'original_Q0_camera_audit':ref(wt/'docs/q0-camera-audit.json',poc),'Q0_log':ref(log_path,poc),'Q0_resource_audit':ref(audit_path,poc),
            'source_pins':[ref(p,poc) for p in sources],'raw_evidence':evidence,'frames':frames,
            'Q1_recorded_contract_evidence':q1,
            'NR_MVecScale':'UNVERIFIED; output coordinate factor1.5 is not model API parameter proof',
            'k_output':'NOT_IDENTIFIED','object_mask':'UNAVAILABLE; geometric samples not certified camera-only',
            'NR_phase':'UNVERIFIED','quality_performance':'NOT_ASSESSED'}


def main():
    parser=argparse.ArgumentParser()
    parser.add_argument('--poc',type=Path,required=True)
    args=parser.parse_args()
    wt=Path(__file__).resolve().parents[2]
    print(json.dumps(run(wt,args.poc.resolve()),indent=2,allow_nan=False))


if __name__=='__main__':
    main()
