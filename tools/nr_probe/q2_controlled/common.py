#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""CPU-only analytic scene and immutable Q2 evidence helpers."""
import hashlib
import json
from pathlib import Path
import numpy as np

SCHEMA = 'q2-known-translation-v1'
W, H = 1920, 1080
BRANCHES = ['primary', 'repeat_primary', 'zero_mv', 'reverse_mv', 'reset_second']
ROOT = Path(__file__).resolve().parent


def strict_json(path):
    def unique(pairs):
        d = {}
        for k, v in pairs:
            if k in d:
                raise ValueError('duplicate key: '+k)
            d[k] = v
        return d
    def invalid(v):
        raise ValueError('nonfinite JSON '+v)
    return json.loads(Path(path).read_text(), object_pairs_hook=unique, parse_constant=invalid)


def sha(path):
    return hashlib.sha256(Path(path).read_bytes()).hexdigest()


def spec():
    d = strict_json(ROOT/'fixture-spec.json')
    if d['schema'] != SCHEMA or len(d['cases']) != 6 or d['branches'] != BRANCHES:
        raise ValueError('unexpected experiment plan')
    return d


def scene(x, y, frame):
    """Physical scene at output-index coordinates, independent of guide extent/phase."""
    u, v = x-12*frame, y+6*frame
    a = u-576
    slab = ((u >= 576) & (u < 1344) & (v >= 288) & (v < 792) & (np.mod(a, 48) < 32))
    z_bg = .75 + .06*x/W
    z_fg = .25 + .15*(u-576)/768 + .025*(v-288)/504
    depth = np.where(slab, z_fg, z_bg)
    color = np.empty(x.shape+(4,), dtype='<f4')
    for c in range(3):
        bg = .35+.08*c+.10*np.sin(2*np.pi*(x+3*c)/53)+.08*np.cos(2*np.pi*(y-c)/37)
        fg = .75+.10*c+.22*np.sin(2*np.pi*(u+4*c)/17)+.12*np.cos(2*np.pi*(v+2*c)/29)
        color[...,c] = np.where(slab, fg, bg)
    color[...,3] = 1
    # Distance to the boundary of the union of translated slats.
    period = np.mod(a, 48)
    dx_inside = np.minimum(period, 32-period)
    dx_gap = np.minimum(np.abs(period-32), 48-period)
    dx_inside = np.minimum(dx_inside, np.minimum(u-576, 1344-u))
    dy_inside = np.minimum(v-288, 792-v)
    distance = np.where(slab, np.minimum(dx_inside, dy_inside),
                        np.maximum(np.maximum(576-u,u-1344),np.maximum(288-v,v-792)))
    distance = np.where((~slab)&(u>=576)&(u<1344)&(v>=288)&(v<792),dx_gap,distance)
    return color, depth.astype('<f4'), slab, np.maximum(distance,0)


def output_grid():
    y,x=np.indices((H,W),dtype=np.float64)
    return x,y


def noisy_source(truth, frame):
    index=np.arange(W*H*3,dtype=np.uint32).reshape(H,W,3)
    z=index ^ np.uint32((frame+1)*0x13579bdf)
    z ^= z >> np.uint32(16);z *= np.uint32(0x7feb352d)
    z ^= z >> np.uint32(15);z *= np.uint32(0x846ca68b)
    z ^= z >> np.uint32(16)
    noise=((z.astype(np.float64)+.5)/4294967296.0*2-1)*.1
    out=truth.copy();out[...,:3]=(out[...,:3].astype(np.float64)+noise).astype('<f4')
    return out


def guides(extent, phase, frame):
    gw,gh=extent;y,x=np.indices((gh,gw),dtype=np.float64)
    px=(x+.5)*W/gw-.5;py=(y+.5)*H/gh-.5
    if phase=='raw':
        j=spec()['jitter_render_reference_pixels'][frame]
        px-=j[0]*W/1280;py-=j[1]*H/720
    elif phase!='corrected':raise ValueError('unknown phase')
    _,depth,slab,_=scene(px,py,frame)
    motion=np.zeros((gh,gw,2),dtype='<f4')
    motion[...,0]=np.where(slab,-12*gw/W,0)
    motion[...,1]=np.where(slab,6*gh/H,0)
    return depth,motion


def masks():
    x,y=output_grid();_,_,fg,distance=scene(x,y,1)
    _,_,old_fg,old_distance=scene(x,y,0)
    border=(x>=64)&(x<W-64)&(y>=64)&(y<H-64)
    fg_core=border&fg&(distance>=4)
    fg_boundary=border&fg&(distance<=2)
    bg_core=border&(~fg)&(~old_fg)&(distance>=4)&(old_distance>=4)
    domain=fg_core|fg_boundary|bg_core
    parity=((x//160+y//90).astype(np.int64)%2)
    return {'all':domain,'foreground_interior':fg_core,'foreground_boundary':fg_boundary,
            'background_interior':bg_core,'fixed_even_tiles':domain&(parity==0),
            'fixed_odd_tiles':domain&(parity==1)}


def asset_ref(path, array, fmt):
    return {'path':str(path.resolve()),'windows_path':'Z:'+str(path.resolve()),
            'sha256':sha(path),'bytes':int(array.nbytes),'format':fmt,
            'extent':[int(array.shape[1]),int(array.shape[0])],
            'channels':1 if array.ndim==2 else int(array.shape[2]),'dtype':array.dtype.str}


def load_asset(a):
    p=Path(a['path'])
    if p.stat().st_size!=a['bytes'] or sha(p)!=a['sha256']:
        raise ValueError('asset bytes/SHA changed: '+str(p))
    w,h=a['extent'];shape=(h,w) if a['channels']==1 else (h,w,a['channels'])
    out=np.fromfile(p,dtype=a['dtype']).reshape(shape)
    if not np.isfinite(out).all():raise ValueError('nonfinite source asset')
    return out
