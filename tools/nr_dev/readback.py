#!/usr/bin/env python3
"""Read-only cold persistent-history pixel audit; outputs only to a new requested directory."""
import argparse,hashlib,json,re
from pathlib import Path
import numpy as np
NAMES=['original-current720.rgba16f','final-guarded720.rgba16f','active-edit-confidence720.rgba16f','active-depth-guide-age720.rgba32f','bank-state.u32','return-guard.u32']
def need(value,reason):
 if not value:raise ValueError(reason)
def sha(path):return hashlib.sha256(path.read_bytes()).hexdigest()
def rows(text,event):
 return [dict(re.findall(r'(\w+)=([^\s]+)',line)) for line in text.splitlines() if line.startswith(event+' ')]
def stats(values):
 x=np.asarray(values,dtype=np.float64);x=x[np.isfinite(x)]
 if not x.size:return {'count':0}
 return {'count':int(x.size),'min':float(x.min()),'mean':float(x.mean()),'p50':float(np.percentile(x,50)),'p95':float(np.percentile(x,95)),'p99':float(np.percentile(x,99)),'max':float(x.max())}
def joined(m,text):
 source=str(m['current_source']);session=m['session'];reads=[r for r in rows(text,'NATIVE_ASYNC_PRE_SR_SR_READ') if r.get('source')==source and r.get('session')==session];presents=[r for r in rows(text,'NATIVE_ASYNC_PRE_SR_CURRENT_PRESENT') if r.get('source_game_frame')==source and r.get('session')==session]
 reasons=[]
 if len(reads)!=1:reasons.append('expected exactly one actual same-source SR read')
 if len(presents)!=1:reasons.append('expected exactly one actual same-source nonreuse Present')
 if not reasons:
  r,p=reads[0],presents[0]
  if int(r['actual_input_image'],0)!=int(m['source_image_handles'][1]):reasons.append('SR image differs from cold final-guarded image')
  if r.get('SR_success')!='1':reasons.append('SR did not succeed')
  if r.get('cache_selected')!='1' or p.get('cache_selected')!='1':reasons.append('actual selected binding absent')
  if p.get('swapchain_present')!='1':reasons.append('swapchain Present absent')
  if not (0<int(m['copy_target_tick'])<=int(r['native_sr_read_tick'])<=int(p['merge_tick'])<=int(p['prepare_tick']) and int(p['ready_tick'])>=int(p['merge_tick'])):reasons.append('Native copy/SR/merge/prepare/ready tick chain invalid')
 return {'actual_final_image_SR_Present_joined':not reasons,'reasons':reasons,'SR':reads,'Present':presents,'scope':'source/image exact successful SR and unique current nonreuse Present; not host potential'}
def effective_history(m,edit,meta,state,guard,target_edit=None,target_meta=None):
 """Compose bank participation, independently of whether candidate pixels enter SR."""
 def bank(e,mt,index):
  need(0<=index<4,'invalid bank index')
  good=(int(state[index])&~16)==0 and int(guard[0])==0
  live=np.isfinite(e).all(-1)&np.isfinite(mt[...,[0,2,3]]).all(-1)&(e[...,3]>0)&(e[...,3]>=m.get('min_confidence',0))&(mt[...,2]>=0)&(mt[...,2]<=m.get('ttl_frames',12))&(mt[...,3]>=0)&(mt[...,3]<=m.get('ttl_ms',200))
  # confidence has no upper bound in shader live(); do not invent a different gate.
  return live&good,good
 old,old_good=bank(edit,meta,int(m['active_bank']));age=meta[...,2].copy();ms=meta[...,3].copy();residual=edit[...,:3].copy();ttl_bad=(edit[...,3]>0)&old_good&(~np.isfinite(age)|~np.isfinite(ms)|(age<0)|(age>m.get('ttl_frames',12))|(ms<0)|(ms>m.get('ttl_ms',200)))
 if not m.get('transition_active',False):return old,age,ms,residual,ttl_bad
 need(target_edit is not None and target_meta is not None,'transition target assets missing')
 weight=m['transition_weight'];need(type(weight) in (int,float) and np.isfinite(weight) and 0<=weight<=1,'invalid transition weight')
 target,target_good=bank(target_edit,target_meta,int(m['candidate_bank']));ta=target_meta[...,2];tm=target_meta[...,3]
 ttl_bad|=(target_edit[...,3]>0)&target_good&(~np.isfinite(ta)|~np.isfinite(tm)|(ta<0)|(ta>m.get('ttl_frames',12))|(tm<0)|(tm>m.get('ttl_ms',200)))
 # Fatal target must preserve old; endpoints participate only where that bank is live.
 if not target_good:return old,age,ms,residual,ttl_bad
 both=old&target
 choose_target=target&(~old|(weight>=1))
 if weight<=0:choose_target=target&~old
 residual=np.where(choose_target[...,None],target_edit[...,:3],residual);age=np.where(choose_target,ta,age);ms=np.where(choose_target,tm,ms)
 if 0<weight<1:
  residual=np.where(both[...,None],edit[...,:3]*(1-weight)+target_edit[...,:3]*weight,residual);age=np.where(both,np.maximum(meta[...,2],ta),age);ms=np.where(both,np.maximum(meta[...,3],tm),ms)
 return old|target,age,ms,residual,ttl_bad

def analyze(manifest,text):
 m=json.loads(manifest.read_text());need(m.get('schema') in ('persistent-history-cold-v1','persistent-history-cold-v2'),'unsupported diagnostic schema');need(m['image_extent']==[1280,720],'unexpected extent');need(m['performance_excluded'] is True,'cold observation must remain excluded from hot performance');need(0<int(m['copy_target_tick'])<=int(m['copy_observed_tick']),'actual Native copy completion missing')
 w,h=m['image_extent'];pixels=w*h;spec={name:(size,dtype,channels) for name,size,dtype,channels in zip(NAMES,[pixels*8]*3+[pixels*16,32,4],['<f2']*3+['<f4','<u4','<u4'],[4,4,4,4,0,0])}
 if m['schema']=='persistent-history-cold-v2':spec.update({'current-depth720.r32f':(pixels*4,'<f4',1),'current-motion720.rg32f':(pixels*8,'<f4',2)})
 extended='transition_active' in m
 formats=[97,97,97,109]+([100,103] if m['schema'].endswith('v2') else [])
 if extended:
  need(type(m['transition_active']) is bool,'transition flag must be boolean');need(type(m.get('transition_weight')) in (int,float) and np.isfinite(m['transition_weight']) and 0<=m['transition_weight']<=1,'invalid transition weight');spec.update({'target-edit-confidence720.rgba16f':(pixels*8,'<f2',4),'target-depth-guide-age720.rgba32f':(pixels*16,'<f4',4)});formats += [97,109]
 need(m['image_formats_vk']==formats,'unexpected diagnostic formats')
 assets={x['file']:x for x in m['assets']};need(len(assets)==len(m['assets']) and set(assets)==set(spec),'exact unique diagnostic assets required');arrays={}
 for name,(size,dtype,channels) in spec.items():
  p=manifest.parent/name;need(not p.is_symlink() and p.resolve().parent==manifest.parent.resolve(),'asset must be direct private file');need(p.is_file() and p.stat().st_size==assets[name]['bytes']==size,'asset size mismatch '+name);need(sha(p)==assets[name]['sha256'],'asset SHA mismatch '+name);a=np.fromfile(p,dtype=dtype);arrays[name]=a.reshape(h,w,channels) if channels else a
 raw,final,edit,meta,state,guard=[arrays[name] for name in NAMES];need(state.tolist()==m['bank_state_actual'] and int(guard[0])==m['return_guard_actual'],'manifest state/guard differs from raw readback')
 rawf=raw.astype(np.float32);finalf=final.astype(np.float32);editf=edit.astype(np.float32)
 finite=np.isfinite(rawf).all(-1)&np.isfinite(finalf).all(-1);alpha=np.equal(raw.view('<u2')[...,3],final.view('<u2')[...,3]);changed_bits=np.any(raw.view('<u2')[...,:3]!=final.view('<u2')[...,:3],axis=-1);delta=finalf[...,:3]-rawf[...,:3];changed=finite&np.any(delta!=0,axis=-1)
 # meta.G is packed guide bits, not an independent scalar with numeric semantics.
 target_edit=arrays.get('target-edit-confidence720.rgba16f');target_meta=arrays.get('target-depth-guide-age720.rgba32f')
 valid,age,ms,residual,ttl_bad=effective_history(m,editf,meta,state,guard,None if target_edit is None else target_edit.astype(np.float32),target_meta)
 old=valid&(age>4);applied=changed&valid;old_applied=changed&old
 epsilon=np.float32(1/512);strength=np.float32(m.get('display_strength',1));
 with np.errstate(over='ignore',invalid='ignore'):
  reconstructed=np.where(valid[...,None],np.maximum((np.maximum(rawf[...,:3],0)+epsilon)*np.exp2(residual)-epsilon,0),rawf[...,:3]);reconstructed=rawf[...,:3]*(1-strength)+reconstructed*strength
 reconstruction_error=np.max(np.abs(reconstructed-finalf[...,:3]),axis=-1)
 eps=1e-6;log_delta=np.log2(np.maximum(finalf[...,:3],0)+eps)-np.log2(np.maximum(rawf[...,:3],0)+eps);luma=lambda a:a[...,:3]@np.array([.2126,.7152,.0722],dtype=np.float32)
 join=joined(m,text);invalid_changed=changed&~valid;integrity=bool(finite.all() and alpha.all() and not ttl_bad.any() and not invalid_changed.any() and (guard[0]==0 or not changed_bits.any()));observed=bool(join['actual_final_image_SR_Present_joined'] and integrity)
 out={'manifest':str(manifest),'manifest_sha256':sha(manifest),'session':m['session'],'source':m['current_source'],'sample_index':m['sample_index'],'capture_source_gap':m['capture_source_gap'],'copy_target_tick':m['copy_target_tick'],'copy_observed_tick':m['copy_observed_tick'],'integrity_pass':integrity,'host_history_potential_only':m['host_history_potential_only'],'transition_active':m.get('transition_active',False),'transition_weight':m.get('transition_weight',1),'bank_state':state.tolist(),'bank_state_semantics':'bit16=local log-range rejection; other bits=fatal candidate rejection','reconstructed_compose_abs_error':stats(reconstruction_error),'reconstruction_scope':'float32 bank-aware log residual compose versus captured guarded half; GPU/half rounding differences descriptive, not exact bitwise proof','return_guard':int(guard[0]),'pixels':pixels,'raw_final_all_finite':bool(finite.all()),'edit_all_finite':bool(np.isfinite(editf).all()),'alpha_bitwise_identical':bool(alpha.all()),'alpha_mismatch_pixels':int((~alpha).sum()),'RGB_changed_bitwise_fraction':float(changed_bits.mean()),'RGB_changed_numeric_fraction':float(changed.mean()),'RGB_changed_gt_1e_minus5_fraction':float((finite&np.any(np.abs(delta)>1e-5,axis=-1)).mean()),'changed_without_valid_history_pixels':int(invalid_changed.sum()),'RGB_max_abs_delta':stats(np.max(np.abs(delta),axis=-1)),'RGB_log2_delta':stats(log_delta[finite]),'luma_delta':stats((luma(finalf)-luma(rawf))[finite]),'valid_history_fraction':float(valid.mean()),'valid_age_over4_fraction':float(old.mean()),'actual_changed_valid_fraction':float(applied.mean()),'actual_changed_valid_age_over4_fraction':float(old_applied.mean()),'actual_changed_age_over4_fraction_of_changed':float(old_applied.sum()/changed.sum()) if changed.any() else 0.,'positive_confidence_TTL_violation_pixels':int(ttl_bad.sum()),'model_age_frames_valid':stats(age[valid]),'model_age_ms_valid':stats(ms[valid]),'join':join,'observed_SR_present_changed_fraction':float(changed.mean()) if observed else None,'observed_SR_present_changed_valid_age_over4_fraction':float(old_applied.mean()) if observed else None,'scope':'cold observed pixels; model admission age4 remains separate from active visual TTL; no dynamic quality or hot FPS acceptance'}
 return out,(rawf,finalf,delta)
def png(path,images):
 from PIL import Image,ImageDraw
 raw,final,delta=images;scale=max(float(np.percentile(np.maximum(raw[...,:3],0),99)),1e-4)
 def view(rgb):return (np.power(np.clip(np.nan_to_num(rgb)/scale,0,1),1/2.2)*255).astype(np.uint8)
 panels=[view(raw[...,:3]),view(final[...,:3]),view(np.abs(delta)*8)];h,w=raw.shape[:2];canvas=Image.new('RGB',(w*3,h+24));draw=ImageDraw.Draw(canvas)
 for i,(panel,label) in enumerate(zip(panels,['current raw720','actual final guarded720','absolute delta x8 (display only)'])):canvas.paste(Image.fromarray(panel),(w*i,24));draw.text((w*i+8,4),label,fill='white')
 canvas.save(path)
def main():
 parser=argparse.ArgumentParser(description=__doc__);parser.add_argument('--readback',type=Path,nargs='+',required=True,help='readback.json files or diagnostic directory');parser.add_argument('--game-log',type=Path,required=True);parser.add_argument('--output',type=Path,required=True,help='new output directory; existing paths refused');parser.add_argument('--png',action='store_true');args=parser.parse_args();need(not args.output.exists(),'output already exists');files=[]
 for p in args.readback:files.extend(sorted(q for q in p.rglob('readback.json') if q.parent.name!='candidate-once') if p.is_dir() else [p])
 need(files and len(files)==len(set(p.resolve() for p in files)),'nonempty unique readback manifests required');text=args.game_log.read_text(errors='replace');results=[];args.output.mkdir(parents=True,mode=0o700)
 for i,p in enumerate(files):
  out,images=analyze(p,text);results.append(out)
  if args.png:png(args.output/f'source-{out["source"]}-comparison.png',images)
 sources=[r['source'] for r in results];need(len(set(sources))==len(sources),'duplicate source samples');need(len({r['session'] for r in results})==1,'mixed sessions forbidden')
 summary={'schema':'persistent-cold-analysis-v1','game_log':str(args.game_log),'game_log_sha256':sha(args.game_log),'samples':results,'all_integrity_pass':all(r['integrity_pass'] for r in results),'all_actual_final_SR_Present_joined':all(r['join']['actual_final_image_SR_Present_joined'] for r in results),'samples_actual_changed_age_over4_observed':sum((r['observed_SR_present_changed_valid_age_over4_fraction'] or 0)>0 for r in results),'performance_excluded':True,'player_dynamic_quality':'NOT_ACCEPTED'}
 (args.output/'analysis.json').write_text(json.dumps(summary,indent=2,allow_nan=False)+'\n');print(json.dumps({k:v for k,v in summary.items() if k!='samples'},indent=2));return 0 if summary['all_integrity_pass'] and summary['all_actual_final_SR_Present_joined'] else 3
if __name__=='__main__':raise SystemExit(main())
