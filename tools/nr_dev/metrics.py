#!/usr/bin/env python3
"""Windowed actual nonreuse Present metrics; cold diagnostic runs are excluded by default."""
import argparse,hashlib,json,re,math
from pathlib import Path
def rows(text,event):
 return [dict(re.findall(r'(\w+)=([^\s]+)',line)) for line in text.splitlines() if line.startswith(event+' ')]
def percentile(a,p):
 if not a:return None
 a=sorted(a);v=(len(a)-1)*p;i=int(v);return a[i]+(a[min(i+1,len(a)-1)]-a[i])*(v-i)
def memory(run,begin,end):
 result={'status':'NOT_MEASURED','scope':'PID-associated compute-app sampling; graphics memory may be absent; missing is not zero','device_total':{'status':'NOT_MEASURED','scope':'per-UUID physical device total includes other apps, driver and shared usage; never summed from PID reports'}}
 identity=run/'game-identity.json';samples=run/'gpu-memory.jsonl'
 if not samples.is_file():return result
 game=json.loads(identity.read_text()) if identity.is_file() else {};pid=game.get('pid');values=[];devices={}
 for line in samples.read_text().splitlines():
  x=json.loads(line)
  if not begin<=int(x['monotonic_ns'])<=end:continue
  for record in x.get('pid_used_MiB_csv','').splitlines():
   fields=[v.strip() for v in record.split(',')]
   if len(fields)==2 and pid is not None and fields[0]==str(pid):
    try:amount=float(fields[1])
    except ValueError:continue
    if math.isfinite(amount) and amount>=0:values.append(amount)
  for record in x.get('device_uuid_used_total_MiB_csv','').splitlines():
   fields=[v.strip() for v in record.split(',')]
   if len(fields)!=3 or not fields[0].startswith('GPU-'):continue
   try:used,total=float(fields[1]),float(fields[2])
   except ValueError:continue
   if all(math.isfinite(v) for v in (used,total)) and 0<=used<=total and total>0:devices.setdefault(fields[0],[]).append((used,total))
 if values:result.update(status='MEASURED_PID_COMPUTE_SAMPLE',pid=pid,game_start_ticks=game.get('start_ticks'),samples=len(values),peak_MiB=max(values),identity_limit='sampler recorded PID only; starttime is game provenance, not per-sample PID-reuse proof')
 if devices:result['device_total'].update(status='MEASURED_DEVICE_TOTAL',devices=[{'uuid':uuid,'samples':len(records),'peak_used_MiB':max(v[0] for v in records),'total_MiB_min':min(v[1] for v in records),'total_MiB_max':max(v[1] for v in records),'peak_used_percent':max(v[0]/v[1]*100 for v in records)} for uuid,records in sorted(devices.items())],attribution='device usage is not solely NRBridge; per-UUID samples do not alone prove which device Native selected')
 return result

def measure(run,after_source=None,before_source=None,after_ns=None,before_ns=None,include_cold=False):
 run=Path(run)
 for lower,upper in ((after_source,before_source),(after_ns,before_ns)):
  if lower is not None and upper is not None and lower>=upper:raise ValueError('after bound must be smaller than before bound')
 text=(run/'game.log').read_text(errors='replace');r=rows(text,'NATIVE_ASYNC_PRE_SR_CURRENT_PRESENT');failed=sum(x.get('swapchain_present')!='1' for x in r);r=[x for x in r if x.get('swapchain_present')=='1']
 required=('source_game_frame','present_frame_number','cpu_monotonic_ns','session')
 if any(not all(k in x for k in required) for x in r):return {'status':'MALFORMED_PRESENT_REJECTED'}
 try:sources=[int(x['source_game_frame']) for x in r];frames=[int(x['present_frame_number']) for x in r];times=[int(x['cpu_monotonic_ns']) for x in r]
 except ValueError:return {'status':'MALFORMED_PRESENT_REJECTED'}
 if len(set(sources))!=len(r) or len(set(frames))!=len(r):return {'status':'DUPLICATE_PRESENT_REJECTED'}
 if any(any(b<=a for a,b in zip(values,values[1:])) for values in (times,sources,frames)):return {'status':'NONMONOTONIC_PRESENT_REJECTED'}
 if len({x['session'] for x in r})>1:return {'status':'MIXED_SESSION_REJECTED'}
 manifest=json.loads((run/'manifest.json').read_text()) if (run/'manifest.json').is_file() else {}
 if r and manifest.get('session') is not None and r[0]['session']!=manifest['session']:return {'status':'FOREIGN_SESSION_REJECTED'}
 environment=manifest.get('environment',{});cold=bool(re.search(r'^PERSISTENT_HISTORY_DIAGNOSTIC_(?:RECORDED|COMPLETE|SUMMARY) ',text,re.M)) or environment.get('BB_NATIVE_HISTORY_DIAGNOSTIC')=='1' or environment.get('BB_NR_NATIVE_ASYNC_PRE_SR_DIAGNOSTIC_ONCE')=='1' or environment.get('BB_NR_NATIVE_ASYNC_EDIT_DIAGNOSTIC_ONCE')=='1'
 # Staging allocation, readback, writer and diagnostic synchronization affect the
 # surrounding run, not just captured sources. Never silently reclassify its tail as hot.
 base={'cold_diagnostic_run':cold,'performance_excluded':cold,'hot_acceptance_eligible':not cold,'bounds_exclusive':{'after_source':after_source,'before_source':before_source,'after_monotonic_ns':after_ns,'before_monotonic_ns':before_ns},'failed_swapchain_present_rows_excluded':failed,'game_log_sha256':hashlib.sha256((run/'game.log').read_bytes()).hexdigest()}
 if cold and not include_cold:return dict(base,status='COLD_DIAGNOSTIC_EXCLUDED',frames=0,recorded_actual_Presents=len(r),scope='entire diagnostic run excluded; use --include-cold for descriptive timing only')
 chosen=[(x,s,t) for x,s,t in zip(r,sources,times) if (after_source is None or s>after_source) and (before_source is None or s<before_source) and (after_ns is None or t>after_ns) and (before_ns is None or t<before_ns)]
 if len(chosen)<2:return dict(base,status='NO_VALID_ACTUAL_PRESENT_WINDOW',frames=len(chosen))
 r=[x[0] for x in chosen];sources=[x[1] for x in chosen];times=[x[2] for x in chosen];intervals=[(b-a)/1e6 for a,b in zip(times,times[1:])];gaps=[(a,b) for a,b in zip(sources,sources[1:]) if b!=a+1]
 return dict(base,status='MEASURED_ACTUAL_NONREUSE_PRESENT',frames=len(times),intervals=len(intervals),source_first=sources[0],source_last=sources[-1],source_gap_events=len(gaps),missing_source_count=sum(b-a-1 for a,b in gaps),source_gap_pairs=gaps[:32],gap_pairs_truncated=len(gaps)>32,monotonic_ns_begin=times[0],monotonic_ns_end=times[-1],window_seconds=(times[-1]-times[0])/1e9,FPS=1000*len(intervals)/sum(intervals),P50_ms=percentile(intervals,.50),P95_ms=percentile(intervals,.95),P99_ms=percentile(intervals,.99),max_ms=max(intervals),gpu_memory=memory(run,times[0],times[-1]),scope='actual current nonreuse swapchain Present CPU intervals, no FG/photon or quality claim; gaps retained as elapsed intervals')

def main():
 p=argparse.ArgumentParser(description=__doc__);p.add_argument('run',type=Path);p.add_argument('--after-source',type=int);p.add_argument('--before-source',type=int);p.add_argument('--after-ns',type=int);p.add_argument('--before-ns',type=int);p.add_argument('--include-cold',action='store_true',help='descriptive only; never hot acceptance');p.add_argument('--output',type=Path,required=True,help='new output file; original present-metrics.json is never overwritten');a=p.parse_args()
 if a.output.exists():p.error('output exists; refuse overwrite')
 out=measure(a.run,a.after_source,a.before_source,a.after_ns,a.before_ns,a.include_cold)
 with a.output.open('x') as f:f.write(json.dumps(out,indent=2,allow_nan=False)+'\n')
 print(json.dumps(out,indent=2));return 0 if out['status']=='MEASURED_ACTUAL_NONREUSE_PRESENT' else 3
if __name__=='__main__':raise SystemExit(main())
