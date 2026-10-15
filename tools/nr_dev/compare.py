#!/usr/bin/env python3
"""Decode same-source cold raw/NR720 with one explicit exposure/Reinhard/sRGB transform.

This produces an offline 720p visualization, never a purported SR1080 A/B pair.
"""
import argparse,hashlib,json,math
from pathlib import Path
import numpy as np
from PIL import Image,ImageDraw
from readback import rows
RAW='original-current720.rgba16f';NR='final-guarded720.rgba16f'
def sha(p):return hashlib.sha256(p.read_bytes()).hexdigest()
def need(v,message):
 if not v:raise ValueError(message)
def selection_join(m,text):
 source=str(m['current_source']);session=m['session'];reads=[r for r in rows(text,'NATIVE_ASYNC_PRE_SR_SR_READ') if r.get('source')==source and r.get('session')==session];presents=[r for r in rows(text,'NATIVE_ASYNC_PRE_SR_CURRENT_PRESENT') if r.get('source_game_frame')==source and r.get('session')==session]
 need(len(reads)==len(presents)==1,'exactly one same-source SR read and nonreuse Present required');r,p=reads[0],presents[0];image=int(r['actual_input_image'],0);raw,final=map(int,m['source_image_handles'][:2]);need(raw!=final,'raw and candidate image identities must differ')
 if image==final:
  need(r.get('cache_selected')==p.get('cache_selected')=='1','NR final binding must be actually selected');binding='NR_FINAL_DISPLAYED'
 elif image==raw:
  need(r.get('cache_selected')==p.get('cache_selected')=='0','raw binding must not claim NR display');binding='RAW_DISPLAYED_NR_CANDIDATE_NOT_DISPLAYED'
 else:raise ValueError('actual SR image matches neither captured raw nor final candidate')
 need(r.get('SR_success')==p.get('swapchain_present')=='1','successful SR and actual Present required');need(0<int(m['copy_target_tick'])<=int(r['native_sr_read_tick'])<=int(p['merge_tick'])<=int(p['prepare_tick']) and int(p['ready_tick'])>=int(p['merge_tick']),'Native copy/SR/merge chain mismatch')
 for record in (r,p):
  if 'control_revision' in record:need(int(record['control_revision'])==int(m['control_revision']),'logged control revision mismatch')
 return {'binding':binding,'actual_final_image_SR_Present_joined':image==final,'actual_raw_image_SR_Present_joined':image==raw,'NR_candidate_displayed':image==final,'SR':reads,'Present':presents,'scope':'exact captured selected image and actual nonreuse Present; raw selection is not NR display evidence'}

def decode(manifest,game_log,expected_manifest=None):
 manifest=Path(manifest);digest=sha(manifest)
 if expected_manifest:need(digest==expected_manifest,'readback manifest SHA mismatch')
 m=json.loads(manifest.read_text());need(m.get('schema') in ('persistent-history-cold-v1','persistent-history-cold-v2'),'unsupported cold manifest schema');need(m.get('image_extent')==[1280,720] and m.get('image_formats_vk',[])[:2]==[97,97],'expected original/final RGBA16F720');need(isinstance(m.get('session'),str) and m['session'] and isinstance(m.get('current_source'),int) and m['current_source']>0,'immutable same-source session identity required');need(0<int(m['copy_target_tick'])<=int(m['copy_observed_tick']),'actual copy completion missing');need(len(m['source_image_handles'])>=2 and all(int(x)>0 for x in m['source_image_handles'][:2]),'actual raw/final image identities required')
 assets={};root=manifest.parent.resolve()
 for item in m['assets']:
  name=item['file'];need(name not in assets and Path(name).name==name,'duplicate or escaped asset path');p=manifest.parent/name;need(not p.is_symlink() and p.resolve().parent==root and p.is_file(),'asset must be a direct regular private file');need(p.stat().st_size==item['bytes'],'asset size mismatch: '+name);need(sha(p)==item['sha256'],'asset SHA mismatch: '+name);assets[name]=item
 need(RAW in assets and NR in assets,'same-source original/final assets missing');a=[]
 for name in (RAW,NR):
  need(assets[name]['bytes']==1280*720*8,'half image size mismatch');a.append(np.fromfile(manifest.parent/name,dtype='<f2').reshape(720,1280,4))
 need(all(np.isfinite(x.astype(np.float32)).all() for x in a),'nonfinite raw/final image cannot be visualized as valid');alpha=np.array_equal(a[0].view('<u2')[...,3],a[1].view('<u2')[...,3]);need(alpha,'raw alpha was not preserved bitwise')
 if m['schema']=='persistent-history-cold-v2':
  need(type(m.get('control_revision')) is int and m['control_revision']>=0 and type(m.get('capture_seq')) is int and m['capture_seq']>=0,'capture/control revision metadata required')
  need(m['image_formats_vk']==([97,97,97,109,100,103,97,109] if 'transition_active' in m else [97,97,97,109,100,103]) and len(m['source_image_handles'])==(8 if 'transition_active' in m else 6),'exact v2 raw/final/edit/meta/Depth/MV image identities required')
  sizes={RAW:1280*720*8,NR:1280*720*8,'active-edit-confidence720.rgba16f':1280*720*8,'active-depth-guide-age720.rgba32f':1280*720*16,'current-depth720.r32f':1280*720*4,'current-motion720.rg32f':1280*720*8,'bank-state.u32':32,'return-guard.u32':4}
  if 'transition_active' in m:
   need(type(m['transition_active']) is bool,'typed transition metadata required');sizes.update({'target-edit-confidence720.rgba16f':1280*720*8,'target-depth-guide-age720.rgba32f':1280*720*16})
  need(set(assets)==set(sizes),'exact v2 assets required')
  for name,size in sizes.items():need(assets[name]['bytes']==size,'exact v2 asset size mismatch '+name)
 proof=selection_join(m,game_log.read_text(errors='replace'))
 return m,a,{'manifest_sha256':digest,'manifest_expected_sha_verified':bool(expected_manifest),'assets':assets,'game_log_sha256':sha(game_log),'actual_source_join':proof,'alpha_bitwise_identical':alpha}
def srgb_reinhard(linear,ev):
 need(math.isfinite(ev) and -16<=ev<=16,'exposure EV must be finite in [-16,16]')
 x=np.maximum(linear.astype(np.float64),0)*2.**ev;toned=x/(1.+x)
 encoded=np.where(toned<=.0031308,12.92*toned,1.055*np.power(toned,1./2.4)-.055)
 return np.rint(np.clip(encoded,0,1)*255).astype(np.uint8)
def output_capture(path,raw_manifest,m,text,ev):
 path=Path(path);j=json.loads(path.read_text());need(j.get('schema')=='nr-runtime-output-v1' and j.get('stage') in ('SR1080','Present'),'unsupported runtime output schema/stage');need(j.get('width')==1920 and j.get('height')==1080,'actual output extent must be1080p');need(int(j['source'])==m['current_source'] and int(j['capture_seq'])==m.get('capture_seq') and int(j['control_revision'])==m.get('control_revision'),'output capture/source/control identity mismatch');need(Path(j['raw_NR_manifest']).resolve()==raw_manifest.resolve(),'output linked to another raw/NR manifest');need(j.get('actual_GPU_complete') is True and 0<int(j['recorded_tick'])<=int(j['observed_tick']),'actual output GPU completion missing')
 proof=selection_join(m,text);complete=[r for r in rows(text,'NR_RUNTIME_OUTPUT_COMPLETE') if r.get('stage')==j['stage'] and r.get('source')==str(j['source']) and r.get('seq')==str(j['capture_seq']) and r.get('revision')==str(j['control_revision'])];need(len(complete)==1,'unique actual runtime output completion event required');need(int(complete[0]['target_tick'])==j['recorded_tick'] and int(complete[0]['observed_tick'])>=j['recorded_tick'],'output completion event tick mismatch');need(Path(complete[0]['path']).resolve()==path.parent.resolve(),'output completion path mismatch')
 need(j['file']=='pixels.raw','unexpected output asset path');p=path.parent/j['file'];fmt=int(j['vk_format']);need(fmt in (97,37,43,44,50),'unsupported output format');size=1920*1080*(8 if fmt==97 else 4);need(not p.is_symlink() and p.resolve().parent==path.parent.resolve() and p.is_file() and p.stat().st_size==j['bytes']==size,'output raw size/path mismatch');need(sha(p)==j['sha256'],'output raw SHA mismatch');need(j['linear_HDR'] is (fmt==97),'output HDR/format metadata mismatch')
 if fmt==97:
  a=np.fromfile(p,dtype='<f2').reshape(1080,1920,4);need(np.isfinite(a).all(),'nonfinite actual HDR output');image=srgb_reinhard(a[...,:3],ev);transform='same explicit exposure/Reinhard/IEC sRGB as720; HDR visualization'
 else:
  a=np.fromfile(p,dtype='u1').reshape(1080,1920,4);image=a[...,[2,1,0]] if fmt in (44,50) else a[...,:3];transform='stored postprocess RGB bytes preserved; BGRA reordered when needed; no second tone map/OETF'
 expected_position='postprocess game frame before swapchain letterbox and NR overlay' if j['stage']=='Present' else 'DLSS SR linear HDR output';need(j['capture_position']==expected_position,'unexpected capture position')
 return image,{'manifest':str(path),'manifest_sha256':sha(path),'pixels_sha256':sha(p),'capture_seq':j['capture_seq'],'source':j['source'],'control_revision':j['control_revision'],'stage':j['stage'],'vk_format':fmt,'capture_position':j['capture_position'],'display_transform':transform,'binding':proof['binding'],'scope':'actual single SR history; Present is game postprocess before letterbox/overlay; output record/observed ticks only compared within its own scheduler, never across capture schedulers; no independent SR A/B'}

def main():
 p=argparse.ArgumentParser(description=__doc__);p.add_argument('--readback',type=Path,required=True);p.add_argument('--manifest-sha256',help='expected external manifest hash, checked if supplied');p.add_argument('--game-log',type=Path,required=True);p.add_argument('--output',type=Path,required=True,help='new output directory');p.add_argument('--exposure-ev',type=float,default=0.);p.add_argument('--config',type=Path,help='optional requested config provenance, not a claim it was applied to this frame');p.add_argument('--config-sha256');p.add_argument('--output-capture',type=Path,nargs='*',default=[],help='output.json files or runtime-output-capture-SEQ directory');args=p.parse_args();need(not args.output.exists(),'output exists; refusing overwrite');m,images,proof=decode(args.readback,args.game_log,args.manifest_sha256);raw,nr=images;rgb=[srgb_reinhard(x[...,:3],args.exposure_ev) for x in images]
 config=None
 if args.config:
  digest=sha(args.config)
  if args.config_sha256:need(digest==args.config_sha256,'configuration SHA mismatch')
  config={'path':str(args.config),'sha256':digest,'value':json.loads(args.config.read_text()),'scope':'requested config file only; actual applied configuration requires frame-bound renderer acknowledgment'}
 elif args.config_sha256:p.error('--config-sha256 requires --config')
 captures=[]
 for item in args.output_capture:
  if item.is_dir():
   matches=[]
   for candidate in sorted(item.rglob('output.json')):
    identity=json.loads(candidate.read_text())
    if (identity.get('capture_seq'),identity.get('source'),identity.get('control_revision'))==(m.get('capture_seq'),m['current_source'],m.get('control_revision')):matches.append(candidate)
   need(matches,'no output manifests match this capture/source/revision in '+str(item));captures.extend(matches)
  else:captures.append(item)
 captured=[];keys=set();text=args.game_log.read_text(errors='replace')
 for path in captures:
  image,detail=output_capture(path,args.readback,m,text,args.exposure_ev);key=detail['stage'];need(key not in keys,'duplicate source/stage output capture');keys.add(key);captured.append((image,detail))
 args.output.mkdir(mode=0o700,parents=True)
 for image,detail in captured:Image.fromarray(image).save(args.output/('actual-'+detail['stage']+'1080.png' if detail['stage']=='Present' else 'actual-SR1080.png'))
 for name,data in zip(('raw720-reinhard-srgb.png','nr720-reinhard-srgb.png'),rgb):Image.fromarray(data).save(args.output/name)
 canvas=Image.new('RGB',(2560,748));draw=ImageDraw.Draw(canvas)
 for i,(data,label) in enumerate(zip(rgb,('Current unedited raw720','NR candidate NOT DISPLAYED (SR chose raw)' if not proof['actual_source_join']['NR_candidate_displayed'] else 'Same-source final guarded NR720 (displayed)'))):canvas.paste(Image.fromarray(data),(i*1280,28));draw.text((i*1280+8,7),label+' | source '+str(m['current_source']),fill='white')
 canvas.save(args.output/'compare720.png');delta=nr.astype(np.float32)[...,:3]-raw.astype(np.float32)[...,:3]
 out={'schema':'nr-offline-common-transform-compare-v1','session':m['session'],'source':m['current_source'],'capture_seq':m.get('capture_seq'),'control_revision':m.get('control_revision'),'NR_candidate_displayed':proof['actual_source_join']['NR_candidate_displayed'],'actual_outputs':[detail for image,detail in captured],'extent':[1280,720],'linear_input':'RGBA16F render-domain values; no inferred NR/SR output sampling-phase correction','transform':{'exposure_ev_shared':args.exposure_ev,'exposure_multiplier_shared':2.**args.exposure_ev,'negative_RGB':'clamp to zero for display only','tonemap':'Reinhard x/(1+x), independently per RGB channel','transfer':'IEC sRGB OETF threshold .0031308; round to 8-bit','automatic_or_independent_exposure':False},'negative_raw_channels':int((raw[...,:3]<0).sum()),'negative_nr_channels':int((nr[...,:3]<0).sum()),'actual_RGB_numeric_changed_fraction':float(np.any(delta!=0,axis=-1).mean()),'max_abs_linear_delta':float(np.abs(delta).max()),'requested_config':config,'proof':proof,'output_sha256':{x.name:sha(x) for x in args.output.glob('*.png')},'scope':'offline same-source720 visualization only; no fake SR1080 A/B, no strict dual-history same-frame game comparison, no hot-performance or dynamic-quality acceptance'}
 (args.output/'compare.json').write_text(json.dumps(out,indent=2,allow_nan=False)+'\n');print(json.dumps({'source':out['source'],'output':str(args.output),'same_source_SR_Present_joined':True,'transform':out['transform']},indent=2));return 0
if __name__=='__main__':raise SystemExit(main())
