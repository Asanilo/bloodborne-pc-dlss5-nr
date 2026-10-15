#!/usr/bin/env python3
"""Actual unique nonreuse Present timing. Never promote selected pixels or quality to PASS."""
import argparse,json,re
from pathlib import Path
def rows(text,event):
 return [dict(re.findall(r'(\w+)=([^\s]+)',line)) for line in text.splitlines() if line.startswith(event+' ')]
def percentile(a,p):
 if not a:return None
 a=sorted(a);v=(len(a)-1)*p;i=int(v);return a[i]+(a[min(i+1,len(a)-1)]-a[i])*(v-i)
def measure(run):
 text=(run/'game.log').read_text(errors='replace');r=rows(text,'NATIVE_ASYNC_PRE_SR_CURRENT_PRESENT')
 r=[x for x in r if x.get('swapchain_present')=='1']
 # Schema validation is deliberately strict; missing log fields mean no metric.
 if len({x['source_game_frame'] for x in r})!=len(r) or len({x['present_frame_number'] for x in r})!=len(r):return {'status':'DUPLICATE_PRESENT_REJECTED'}
 times=[int(x['cpu_monotonic_ns']) for x in r];intervals=[(b-a)/1e6 for a,b in zip(times,times[1:])]
 if len(times)<2 or any(x<=0 for x in intervals):return {'status':'NO_VALID_ACTUAL_PRESENT_WINDOW','available_fields':sorted(r[0]) if r else []}
 return {'status':'MEASURED_ACTUAL_NONREUSE_PRESENT','frames':len(times),'FPS':1000*len(intervals)/sum(intervals),'P50_ms':percentile(intervals,.50),'P95_ms':percentile(intervals,.95),'P99_ms':percentile(intervals,.99),'max_ms':max(intervals),'scope':'CPU observed actual current swapchain Present; no FG or quality proof'}
if __name__=='__main__':
 p=argparse.ArgumentParser();p.add_argument('run',type=Path);a=p.parse_args();out=measure(a.run);(a.run/'present-metrics.json').write_text(json.dumps(out,indent=2)+'\n');print(json.dumps(out,indent=2))
