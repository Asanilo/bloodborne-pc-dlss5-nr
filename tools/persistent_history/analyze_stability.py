#!/usr/bin/env python3
"""Read-only three-frame stability evidence; reports blur and temporal error separately.

Uses the existing SHA-verified capture loader and exact SR/Present join. Cross-run
captures are descriptive comparisons, never asserted to be the same frame/pixel.
"""
import argparse,hashlib,importlib.util,json
from pathlib import Path
import numpy as np
EPS=np.float32(1/512)
def need(v,why):
 if not v:raise ValueError(why)
def sha(p):return hashlib.sha256(p.read_bytes()).hexdigest()
def module(p,name):
 s=importlib.util.spec_from_file_location(name,p);m=importlib.util.module_from_spec(s);s.loader.exec_module(m);return m
def stats(a):
 a=np.asarray(a,dtype=np.float64);a=a[np.isfinite(a)]
 return dict(count=int(a.size),mean=float(a.mean()),p50=float(np.percentile(a,50)),p95=float(np.percentile(a,95)),p99=float(np.percentile(a,99)),max=float(a.max())) if a.size else {'count':0}
def highpass(a):
 h,w=a.shape[:2];p=np.pad(a,((1,1),(1,1),(0,0)),mode='edge');mean=sum(p[y:y+h,x:x+w] for y in range(3) for x in range(3))/9
 return a-mean
def erode(mask):
 h,w=mask.shape;p=np.pad(mask,1,constant_values=False);return np.logical_and.reduce([p[y:y+h,x:x+w] for y in range(3) for x in range(3)])
def live(m,d):
 e,meta=d['edit'],d['meta'];return np.isfinite(e).all(-1)&np.isfinite(meta[...,[0,2,3]]).all(-1)&(e[...,3]>0)&(e[...,3]>=m['min_confidence'])&(meta[...,2]>=0)&(meta[...,2]<=m['ttl_frames'])&(meta[...,3]>=0)&(meta[...,3]<=m['ttl_ms'])
def origin_neighbourhood(meta):
 h,w=meta.shape[:2];age=meta[...,2:4];p=np.pad(age,((1,1),(1,1),(0,0)),mode='edge')
 return np.logical_and.reduce([np.all(p[y:y+h,x:x+w]==age,-1) for y in range(3) for x in range(3)])
def frame(path,text,motion,readback):
 m,d=motion.load(path);need(m['schema']=='persistent-history-cold-v2','requires depth/MV v2 capture');need(m['performance_excluded'] is True,'cold samples remain performance-excluded')
 assets={a['file']:a for a in m['assets']};need(len(assets)==len(m['assets']),'duplicate asset')
 for name,a in assets.items():
  p=path.parent/name;need(p.parent==path.parent and p.stat().st_size==a['bytes'] and sha(p)==a['sha256'],'asset size/SHA '+name)
 h,w=d['raw'].shape[:2];final=np.fromfile(path.parent/'final-guarded720.rgba16f','<f2').reshape(h,w,4).astype(np.float32);d['final']=final
 bank=np.fromfile(path.parent/'bank-state.u32','<u4');guard=np.fromfile(path.parent/'return-guard.u32','<u4');need(bank.tolist()==m['bank_state_actual'] and int(guard[0])==m['return_guard_actual'],'actual state manifest mismatch')
 valid=live(m,d)&((int(bank[m["active_bank"]])&~16)==0)&(int(guard[0])==0);d['actual_valid']=valid;changed=np.any(np.abs(final[...,:3]-d['raw'][...,:3])>1e-5,-1)
 alpha=np.array_equal(final[...,3].view(np.uint32),d['raw'][...,3].view(np.uint32));finite=all(np.isfinite(d[k]).all() for k in ('raw','final','edit','depth','motion')) and np.isfinite(d['meta'][...,[0,2,3]]).all()
 positive=d['edit'][...,3]>0;ttl_violation=positive&~((d['meta'][...,2]>=0)&(d['meta'][...,2]<=m['ttl_frames'])&(d['meta'][...,3]>=0)&(d['meta'][...,3]<=m['ttl_ms']))
 join=readback.joined(m,text);need(not np.any(positive&(d['meta'][...,2]!=np.floor(d['meta'][...,2]))),'fractional model source-frame age')
 need(alpha and finite and not ttl_violation.any() and join['actual_final_image_SR_Present_joined'],'finite/alpha/TTL/exactSRPresent proof failed')
 summary=dict(source=m['current_source'],manifest=str(path),manifest_sha256=sha(path),true_age_frames=stats(d['meta'][...,2][valid]),true_age_ms=stats(d['meta'][...,3][valid]),valid_fraction=float(valid.mean()),actually_modified_fraction=float(changed.mean()),actually_modified_valid_fraction=float((changed&valid).mean()),changed_without_valid_pixels=int((changed&~valid).sum()),finite_all=True,raw_alpha_identical=True,positive_confidence_TTL_violations=0,exact_final_image_SR_Present_join=True,candidate_recorded=m['candidate_recorded_this_source'],candidate_count=m['candidate_count'],candidate_origin=m['candidate_origin'],copy_target_tick=m['copy_target_tick'],copy_observed_tick=m['copy_observed_tick'],controls={k:m.get(k) for k in ('display_strength','min_confidence','ttl_frames','ttl_ms','depth_tolerance','colour_tolerance','control_revision','params_version','reset_seq')},actual_present_monotonic_ns=int(join['Present'][0]['cpu_monotonic_ns']))
 return m,d,summary,join

def pair(pm,p,m,d,motion,raw_tolerance):
 identity=('visual_generation','params_version','control_revision','reset_generation','reset_seq')
 if m['previous_source']!=pm['current_source'] or m['current_source']!=pm['current_source']+1:return {'unavailable':'not adjacent actual source frames'}
 if any(m.get(k)!=pm.get(k) for k in identity) or m['real_reset']:return {'unavailable':'visual/params/control/reset discontinuity'}
 if m['candidate_recorded_this_source'] or m['candidate_count']!=pm['candidate_count']:return {'unavailable':'candidate update excludes temporal-only attribution'}
 h,w=d['raw'].shape[:2];y,x=np.indices((h,w),dtype=np.float32);xy=np.stack((x,y),-1);extent=np.array((w,h),np.float32);mv=d['motion'];jitter=(np.array(m['previous_jitter'],np.float32)-np.array(m['current_jitter'],np.float32))/extent
 prev=((xy+np.float32(.5))/extent+mv/extent+jitter)*extent;inside=np.isfinite(prev).all(-1)&(prev>=0).all(-1)&(prev<extent).all(-1)
 q=np.floor(np.nan_to_num(prev)).astype(np.int64);qc=np.clip(q,[0,0],[w-1,h-1]);ne=p['edit'][qc[...,1],qc[...,0]];nm=p['meta'][qc[...,1],qc[...,0]]
 old_origin=pm['current_source']-nm[...,2];new_origin=m['current_source']-d['meta'][...,2]
 origin=(old_origin==new_origin)&(d['meta'][...,2]==nm[...,2]+1)&(d['meta'][...,3]>=nm[...,3])
 cp,pp,row=(np.asarray(m[k],np.float32) for k in ('current_projection','previous_projection','previous_view_z'));z=d['depth'][...,0]
 with np.errstate(all='ignore'):
  vz=cp[3]/(z-cp[2]);ndc=(xy+.5-np.array(m['current_jitter'],np.float32))/extent*2-1;pvz=row[0]*ndc[...,0]*vz/cp[0]+row[1]*ndc[...,1]*vz/cp[1]+row[2]*vz+row[3];expected=np.where(z>=1,1,pp[2]+pp[3]/pvz)
 projection_ok=all(np.isfinite(v).all() for v in (cp,pp,row)) and cp[2]>=1 and pp[2]>=1 and cp[3]<0 and pp[3]<0 and abs(cp[0])>1e-8 and abs(cp[1])>1e-8
 geometry=projection_ok&np.isfinite(expected)&((z>=1)|((vz>0)&(pvz>0)))
 edge=np.ones((h,w),bool);pad=np.pad(z,1,mode='edge')
 for yy in range(3):
  for xx in range(3):edge&=motion.same_surface(pad[yy:yy+h,xx:xx+w],z,cp[2],m['depth_tolerance'])
 base=inside&d['actual_valid']&p['actual_valid'][qc[...,1],qc[...,0]]&origin&geometry&edge&motion.same_surface(nm[...,0],expected,pp[2],m['depth_tolerance'])
 phase=prev-.5-np.floor(prev-.5);b=np.floor(prev-.5).astype(np.int64);support=base.copy();warped_edit=np.zeros((h,w,3),np.float32);warped_raw=np.zeros_like(warped_edit);warped_edit_hf=np.zeros((h,w),np.float32);warped_raw_hf=np.zeros_like(warped_edit_hf);warped_final_hf=np.zeros_like(warped_edit_hf)
 hf_support=base.copy();prior_hf_safe=erode(p['actual_valid'])&origin_neighbourhood(p['meta'])
 prev_hf=np.mean(highpass(p['edit'][...,:3])**2,-1);prev_raw_hf=np.mean(highpass(p['raw'][...,:3])**2,-1);prev_final_hf=np.mean(highpass(p['final'][...,:3])**2,-1)
 for yy in (0,1):
  for xx in (0,1):
   weight=(1-phase[...,0] if xx==0 else phase[...,0])*(1-phase[...,1] if yy==0 else phase[...,1]);positive=weight>0;tap=b+(xx,yy);bounds=(tap>=0).all(-1)&(tap<extent).all(-1);t=np.clip(tap,[0,0],[w-1,h-1]);te=p['edit'][t[...,1],t[...,0]];tm=p['meta'][t[...,1],t[...,0]]
   same=(tm[...,2:4]==nm[...,2:4]).all(-1);safe=p['actual_valid'][t[...,1],t[...,0]]&same&motion.same_surface(tm[...,0],expected,pp[2],m['depth_tolerance'])
   support&=~positive|(bounds&safe);hf_support&=~positive|(bounds&safe&prior_hf_safe[t[...,1],t[...,0]])
   warped_edit+=weight[...,None]*te[...,:3];warped_raw+=weight[...,None]*p['raw'][t[...,1],t[...,0],:3];warped_edit_hf+=weight*prev_hf[t[...,1],t[...,0]];warped_raw_hf+=weight*prev_raw_hf[t[...,1],t[...,0]];warped_final_hf+=weight*prev_final_hf[t[...,1],t[...,0]]
 with np.errstate(all='ignore'):raw_delta=np.max(np.abs(np.log2((np.maximum(d['raw'][...,:3],0)+EPS)/(np.maximum(warped_raw,0)+EPS))),-1)
 stable=support&np.isfinite(raw_delta)&(raw_delta<=raw_tolerance);interior=erode(stable)&hf_support&origin_neighbourhood(d['meta']);residual=np.max(np.abs(d['edit'][...,:3]-warped_edit),-1);nearest_residual=np.max(np.abs(d['edit'][...,:3]-ne[...,:3]),-1)
 current_hf=np.mean(highpass(d['edit'][...,:3])**2,-1);current_raw_hf=np.mean(highpass(d['raw'][...,:3])**2,-1);current_final_hf=np.mean(highpass(d['final'][...,:3])**2,-1)
 def energy_ratio(now,before):
  a=now[interior];b0=before[interior];mean=float(b0.mean()) if b0.size else 0.;return {'current_mean_squared_highpass':float(a.mean()) if a.size else None,'prior_highpass_energy_warped_mean':mean if b0.size else None,'retention_ratio':float(a.mean()/mean) if mean>0 else None,'pixels':int(a.size)}
 return dict(previous_source=pm['current_source'],current_source=m['current_source'],nearest_same_origin_valid_pixels=int(base.sum()),all4tap_geometry_origin_support_pixels=int(support.sum()),strict_raw_stable_pixels=int(stable.sum()),strict_raw_stable_fraction=float(stable.mean()),raw_change_limit_stops=raw_tolerance,strict_mask_current_MV_pixels=stats(np.linalg.norm(mv,axis=-1)[stable]),strict_mask_raw_log_change_stops=stats(raw_delta[stable]),strict_mask_current_raw_luma=stats(np.sum(np.maximum(d['raw'][...,:3],0)*np.array((.2126,.7152,.0722),np.float32),-1)[stable]),residual_motion_warp_bilinear_difference_stops=stats(residual[stable]),residual_motion_warp_nearest_difference_stops=stats(nearest_residual[stable]),edit_high_frequency_energy=energy_ratio(current_hf,warped_edit_hf),raw_high_frequency_control=energy_ratio(current_raw_hf,warped_raw_hf),actual_final_high_frequency_energy=energy_ratio(current_final_hf,warped_final_hf),exclusion_scope='all comparisons exclude candidate refresh, new origin, invalid confidence/TTL, source/reset/control discontinuity, out-of-bounds taps, depth occlusion/3x3 edge, and actual reprojected raw RGB changes > stated threshold',interpretation='lower bilinear warp error alone rewards matching the filter; high-frequency retention must be considered jointly, never a standalone quality win')

def analyze(run,motion,readback,tolerance):
 text=(run/'game.log').read_text(errors='replace');events=[json.loads(line) for line in (run/'scenario-events.jsonl').read_text().splitlines()] if (run/'scenario-events.jsonl').exists() else [];result=json.loads((run/'result.json').read_text());groups=[]
 paths=sorted(run.glob('persistent-history-diagnostic-*-capture-*'),key=lambda p:int(p.name.rsplit('-',1)[1]))
 need(paths,'no capture groups')
 for directory in paths:
  manifests=sorted(directory.glob('frame-*/readback.json'),key=lambda p:json.loads(p.read_text())['current_source']);need(len(manifests)==3,'requires exactly three samples per group');items=[];pairs=[];previous=None
  for path in manifests:
   m,d,summary,join=frame(path,text,motion,readback);summary['scripted_input_at_present']=[{'value':e.get('value'),'label':e.get('label'),'monotonic_ns':e['monotonic_ns']} for e in events if e.get('kind')=='input' and e['monotonic_ns']<=summary['actual_present_monotonic_ns']<e['monotonic_ns']+int(e.get('duration',0)*1e9)];items.append(summary)
   if previous:pairs.append(pair(*previous,m,d,motion,tolerance))
   previous=m,d
  groups.append({'capture_seq':int(directory.name.rsplit('-',1)[1]),'samples':items,'pairs':pairs})
 return {'session':run.name,'run':str(run),'game_log_sha256':sha(run/'game.log'),'result':{k:result.get(k) for k in ('returncode','watchdog_expired','residuals')},'groups':groups}
def main():
 p=argparse.ArgumentParser(description=__doc__);p.add_argument('--run',type=Path,nargs='+',required=True);p.add_argument('--reference-root',type=Path,default=Path(__file__).resolve().parents[2]);p.add_argument('--raw-change-limit-stops',type=float,default=.02);p.add_argument('--output',type=Path);a=p.parse_args();need(0<a.raw_change_limit_stops<=.1,'explicit strict raw-change bound')
 motion_path=a.reference_root/'tools/persistent_history/analyze_motion.py';join_path=a.reference_root/'tools/nr_dev/readback.py';motion=module(motion_path,'stability_motion');readback=module(join_path,'stability_readback')
 out={'schema':'nr-cold-stability-comparison-v1','runs':[analyze(r,motion,readback,a.raw_change_limit_stops) for r in a.run],'analysis_script_sha256':sha(Path(__file__)),'verification_code_sha256':{'analyze_motion.py':sha(motion_path),'readback.py':sha(join_path)},'limits':['Only three captured frames/two adjacent pairs per group; not a long dynamic quality sample.','Same scripted seed/trajectory does not make cross-run captures the same source, camera pose, raw frame, or pixel. Compare descriptive groups with their different strict-mask coverage and motion, not paired causality.','Warp error depends on the reconstruction filter. A lower error with lower high-frequency energy can indicate blur rather than a quality gain.','Cold captures are excluded from hot FPS; no new GPU work performed.'],'cross_run_comparison':'DESCRIPTIVE_ONLY_NOT_EXACT_FRAME_ALIGNED'}
 if a.output:need(not a.output.exists(),'output already exists');a.output.write_text(json.dumps(out,indent=2,allow_nan=False)+'\n')
 print(json.dumps({'sessions':[r['session'] for r in out['runs']],'groups':[len(r['groups']) for r in out['runs']],'output':str(a.output) if a.output else None,'cross_run':out['cross_run_comparison']},indent=2))
 if not a.output:print(json.dumps(out,indent=2,allow_nan=False))
if __name__=='__main__':main()
