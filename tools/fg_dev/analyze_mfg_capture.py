#!/usr/bin/env python3
"""Archive bounded Present readbacks losslessly and inspect MFG temporal ordering.
Requires NumPy and Pillow; performs no GPU operations or source-file deletion.
Patch displacement is a diagnostic estimate, not input latency or scanout.
"""
import argparse
import sys,json,re,hashlib
from pathlib import Path
import numpy as np
from PIL import Image,ImageDraw
parser=argparse.ArgumentParser(description=__doc__)
parser.add_argument('capture',type=Path);parser.add_argument('--output',type=Path,required=True)
parser.add_argument('--mapping-log',type=Path,help='explicit retained Native log when daily stdout has rotated past the capture')
args=parser.parse_args();r=args.capture;out=args.output;out.mkdir(parents=True,exist_ok=True)
meta=json.loads((r/'capture.json').read_text());rows=[];arrays=[]
for p in sorted(r.glob('f*.raw')):
 m=re.search(r'f(\d+)_(interp|real)_(\d+)x(\d+)_(bgra|rgba)',p.name);index,kind,w,h,order=m.groups();w=int(w);h=int(h)
 raw=p.read_bytes();a=np.frombuffer(raw,np.uint8).reshape(h,w,4);rgba=a[:,:,[2,1,0,3]] if order=='bgra' else a
 target=out/(p.stem+'.png');Image.fromarray(rgba).save(target,compress_level=3)
 decoded=np.array(Image.open(target));restored=decoded[:,:,[2,1,0,3]] if order=='bgra' else decoded
 assert restored.tobytes()==raw
 arrays.append(rgba[:,:,:3].copy());rows.append(dict(index=int(index),kind=kind,width=w,height=h,raw_sha256=hashlib.sha256(raw).hexdigest(),png=str(target)))
assert len(rows)==16
width,height=rows[0]['width'],rows[0]['height']
assert all((row['width'],row['height'])==(width,height) for row in rows),'capture must settle in one output layout before triggering'
# Phase correlation of rigid world patches; HUD and player body excluded.
def shift(a,b):
 a=a.astype(float).mean(axis=2);b=b.astype(float).mean(axis=2)
 win=np.hanning(a.shape[0])[:,None]*np.hanning(a.shape[1])[None,:]
 fa=np.fft.fft2((a-a.mean())*win);fb=np.fft.fft2((b-b.mean())*win);cross=fa*np.conj(fb);corr=np.fft.ifft2(cross/np.maximum(abs(cross),1e-10)).real
 y,x=np.unravel_index(corr.argmax(),corr.shape)
 def sub(center,minus,plus):
  den=minus-2*center+plus
  return .5*(minus-plus)/den if abs(den)>1e-12 else 0
 dx=sub(corr[y,x],corr[y,(x-1)%corr.shape[1]],corr[y,(x+1)%corr.shape[1]])
 dy=sub(corr[y,x],corr[(y-1)%corr.shape[0],x],corr[(y+1)%corr.shape[0],x])
 return [float((x if x<=corr.shape[1]//2 else x-corr.shape[1])+dx),float((y if y<=corr.shape[0]//2 else y-corr.shape[0])+dy),float(corr[y,x])]
def region(rect):
 x0,y0,x1,y1=rect
 return (round(x0*width/1920),round(y0*height/1080),round(x1*width/1920),round(y1*height/1080))
patches=[region(rect) for rect in [(320,280,780,700),(1220,260,1680,700),(700,200,1200,430)]]
wx0,wy0,wx1,wy1=region((250,220,1700,850))
groups=[];real=[i for i,x in enumerate(rows) if x['kind']=='real']
for first,last in zip(real,real[1:]):
 if last-first<2:continue
 segment=arrays[first:last+1];mae=[float(np.abs(a[wy0:wy1,wx0:wx1].astype(float)-b[wy0:wy1,wx0:wx1]).mean()) for a,b in zip(segment,segment[1:])]
 shifts=[]
 for x0,y0,x1,y1 in patches:
  ref=segment[0][y0:y1,x0:x1];shifts.append([shift(ref,a[y0:y1,x0:x1]) for a in segment[1:]])
 groups.append(dict(previous=first,current=last,generated=list(range(first+1,last)),adjacent_world_mae=mae,patch_shifts=shifts,all_frames_unique=len({rows[i]['raw_sha256'] for i in range(first,last+1)})==last-first+1))
# Preserve exact source/index mapping printed by the Presenter during the bounded readback.
run=r.parent.parent;mapping=[];pending=None
log_paths=[args.mapping_log] if args.mapping_log else [q for q in (run/'game.log.1',run/'game.log') if q.exists()]
assert all(q.is_file() and not q.is_symlink() for q in log_paths),'regular recorded mapping log required'
log='\n'.join(q.read_text(errors='replace') for q in log_paths)
log_inputs=[dict(path=str(q.resolve()),sha256=hashlib.sha256(q.read_bytes()).hexdigest()) for q in log_paths]
trigger=meta.get('trigger_monotonic_ns',0)
for line in log.splitlines():
 if 'FG_SEQUENCE_CAPTURE ' in line:
  pending=dict(re.findall(r'(\w+)=([^ ]+)',line.split('FG_SEQUENCE_CAPTURE ',1)[1]))
 elif pending is not None and 'FG_PRESENT ' in line:
  fields=dict(re.findall(r'(\w+)=([^ ]+)',line.split('FG_PRESENT ',1)[1]));ns=int(fields.get('cpu_monotonic_ns','0'))
  if ns>=trigger and len(mapping)<16:pending['cpu_monotonic_ns']=str(ns);mapping.append(pending)
  pending=None
assert len(mapping)==16,'capture mapping absent from recorded Native logs'
for i,m in enumerate(mapping):assert int(m['index'])==i and (int(m['generated_index'])==0)==(rows[i]['kind']=='real')
result=dict(capture=str(r),width=width,height=height,patch_regions=patches,frames=rows,groups=groups,present_mapping=mapping,mapping_log_inputs=log_inputs,scope='lossless encoded Present inputs; rigid patch displacement is diagnostic, not optical input latency or scanout')
(out/'analysis.json').write_text(json.dumps(result,indent=2)+'\n')
if groups:
 inds=list(range(groups[0]['previous'],groups[0]['current']+1));thumbs=[]
 for i in inds:
  a=Image.fromarray(arrays[i]);a.thumbnail((640,360));draw=ImageDraw.Draw(a);draw.rectangle((0,0,220,24),fill='black');draw.text((5,5),str(i)+' '+rows[i]['kind'],fill='white');thumbs.append(a)
 sheet=Image.new('RGB',(640*len(thumbs),360));
 for i,a in enumerate(thumbs):sheet.paste(a,(i*640,0))
 sheet.save(out/'contact.png')
print(json.dumps(dict(capture=r.name,unique_groups=[x['all_frames_unique'] for x in groups],mae=[x['adjacent_world_mae'] for x in groups],first_group_shifts=groups[0]['patch_shifts'] if groups else [],mapping_rows=len(mapping),output=str(out))))
