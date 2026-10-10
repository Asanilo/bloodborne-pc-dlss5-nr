#!/usr/bin/env python3
"""Fail closed on absent DLSS evaluations, fallback, NGX errors or inconsistent input metadata.
This checks a trace contract, not temporal image quality or correctness of camera reprojection.
"""
import argparse
import math
from pathlib import Path
import re
import sys

CREATE=re.compile(r'AUDIT create input=(\d+)x(\d+) output=(\d+)x(\d+) quality=(\d+) hdr=(\d+) depth_inverted=(\d+) flags=0x([\da-fA-F]+)')
FRAME=re.compile(r'AUDIT frame=(\d+) color=(\d+)x(\d+):(\d+) depth=(\d+)x(\d+):(\d+) mv=(\d+)x(\d+):(\d+) output=(\d+)x(\d+):(\d+) jitter=([^, ]+),([^ ]+) mv_scale=([^, ]+),([^ ]+) reset=(\d+) frame_ms=([^ ]+) result=0x([\da-fA-F]+)')

def check(text,mode,min_frames=8,synthetic=False):
    def demand(ok,code):
        if not ok:raise ValueError(code)
    demand(not re.search(r'falling back|unavailable|evaluation failed|feature creation failed|Validation Error|VUID-|SYNC-HAZARD|DLSS_SMOKE FAIL',text,re.I),'E_FALLBACK_OR_RUNTIME_ERROR')
    demand(('DLSS_SMOKE PASS' if synthetic else 'DLSS: ready') in text,'E_READY_MISSING')
    create=None;count=0;jitters=set();resets=set()
    for line in text.splitlines():
        c=CREATE.search(line)
        if c:
            w,h,ow,oh,q,hdr,inv=map(int,c.groups()[:7])
            flags=int(c[8],16);create=(w,h,ow,oh)
            demand(q==(0 if mode=='dlaa' else 1),'E_MODE')
            demand(w>0 and h>0 and ow>0 and oh>0,'E_EXTENT')
            demand((w,h)==(ow,oh) if mode=='dlaa' else abs(ow/w-1.5)<.02 and abs(oh/h-1.5)<.02,'E_SCALE')
            demand(inv==0 and flags&2 and not flags&12 and bool(flags&1)==bool(hdr),'E_DEPTH_OR_MV_FLAGS')
            continue
        f=FRAME.search(line)
        if not f:continue
        demand(create is not None,'E_CREATE_MISSING')
        a=list(map(int,f.groups()[:13]));_,cw,ch,cf,dw,dh,df,mw,mh,mf,ow,oh,of=a
        w,h,targetw,targeth=create
        demand(cw>=w and ch>=h and dw>=w and dh>=h and (mw,mh)==(w,h) and (ow,oh)==(targetw,targeth),'E_RESOURCE_EXTENT')
        demand(mf==83 and of==97 and df in (100,126,130),'E_RESOURCE_FORMAT')
        jx,jy,sx,sy=map(float,f.groups()[13:17]);reset=int(f[18]);ms=float(f[19]);result=int(f[20],16)
        demand(all(math.isfinite(x) for x in (jx,jy,sx,sy,ms)) and abs(jx)<=.5 and abs(jy)<=.5 and (sx,sy)==(1,1) and 0<ms<=200,'E_FRAME_METADATA')
        demand(reset in (0,1) and result==1,'E_NGX_RESULT')
        resets.add(reset);jitters.add((jx,jy));count+=1
    demand(count>=min_frames,'E_FRAMES_MISSING')
    demand(resets=={0,1},'E_RESET_MISSING')
    demand(len(jitters)>1,'E_JITTER_STATIC')
    return count

def main():
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('log',type=Path)
    p.add_argument('--mode',choices=('dlaa','quality'),required=True);p.add_argument('--min-frames',type=int,default=8);p.add_argument('--synthetic',action='store_true');a=p.parse_args()
    try:print(f'DLSS_LOG PASS frames={check(a.log.read_text(),a.mode,a.min_frames,a.synthetic)} visual_quality=UNVERIFIED')
    except (OSError,ValueError) as e:print(f'DLSS_LOG FAIL {e}',file=sys.stderr);return 2
    return 0
if __name__=='__main__':sys.exit(main())
