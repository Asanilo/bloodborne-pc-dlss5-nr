#!/usr/bin/env python3
"""Decode same-source cold raw/NR720 with one explicit exposure/Reinhard/sRGB transform.

This produces an offline 720p visualization, never a purported SR1080 A/B pair.
"""
import argparse,hashlib,json,math
from pathlib import Path
import numpy as np
from PIL import Image,ImageDraw
from readback import joined
RAW='original-current720.rgba16f';NR='final-guarded720.rgba16f'
def sha(p):return hashlib.sha256(p.read_bytes()).hexdigest()
def need(v,message):
 if not v:raise ValueError(message)
def decode(manifest,game_log,expected_manifest=None):
 manifest=Path(manifest);digest=sha(manifest)
 if expected_manifest:need(digest==expected_manifest,'readback manifest SHA mismatch')
 m=json.loads(manifest.read_text());need(m.get('schema')=='persistent-history-cold-v1','unsupported cold manifest schema');need(m.get('image_extent')==[1280,720] and m.get('image_formats_vk',[])[:2]==[97,97],'expected original/final RGBA16F720');need(isinstance(m.get('session'),str) and m['session'] and isinstance(m.get('current_source'),int) and m['current_source']>0,'immutable same-source session identity required');need(0<int(m['copy_target_tick'])<=int(m['copy_observed_tick']),'actual copy completion missing');need(len(m['source_image_handles'])>=2 and all(int(x)>0 for x in m['source_image_handles'][:2]),'actual raw/final image identities required')
 assets={};root=manifest.parent.resolve()
 for item in m['assets']:
  name=item['file'];need(name not in assets and Path(name).name==name,'duplicate or escaped asset path');p=manifest.parent/name;need(not p.is_symlink() and p.resolve().parent==root and p.is_file(),'asset must be a direct regular private file');need(p.stat().st_size==item['bytes'],'asset size mismatch: '+name);need(sha(p)==item['sha256'],'asset SHA mismatch: '+name);assets[name]=item
 need(RAW in assets and NR in assets,'same-source original/final assets missing');a=[]
 for name in (RAW,NR):
  need(assets[name]['bytes']==1280*720*8,'half image size mismatch');a.append(np.fromfile(manifest.parent/name,dtype='<f2').reshape(720,1280,4))
 need(all(np.isfinite(x.astype(np.float32)).all() for x in a),'nonfinite raw/final image cannot be visualized as valid');alpha=np.array_equal(a[0].view('<u2')[...,3],a[1].view('<u2')[...,3]);need(alpha,'raw alpha was not preserved bitwise')
 proof=joined(m,game_log.read_text(errors='replace'));need(proof['actual_final_image_SR_Present_joined'],'same-source actual SR/Present join missing: '+'; '.join(proof['reasons']))
 return m,a,{'manifest_sha256':digest,'manifest_expected_sha_verified':bool(expected_manifest),'assets':assets,'game_log_sha256':sha(game_log),'actual_source_join':proof,'alpha_bitwise_identical':alpha}
def srgb_reinhard(linear,ev):
 need(math.isfinite(ev) and -16<=ev<=16,'exposure EV must be finite in [-16,16]')
 x=np.maximum(linear.astype(np.float64),0)*2.**ev;toned=x/(1.+x)
 encoded=np.where(toned<=.0031308,12.92*toned,1.055*np.power(toned,1./2.4)-.055)
 return np.rint(np.clip(encoded,0,1)*255).astype(np.uint8)
def main():
 p=argparse.ArgumentParser(description=__doc__);p.add_argument('--readback',type=Path,required=True);p.add_argument('--manifest-sha256',help='expected external manifest hash, checked if supplied');p.add_argument('--game-log',type=Path,required=True);p.add_argument('--output',type=Path,required=True,help='new output directory');p.add_argument('--exposure-ev',type=float,default=0.);p.add_argument('--config',type=Path,help='optional requested config provenance, not a claim it was applied to this frame');p.add_argument('--config-sha256');args=p.parse_args();need(not args.output.exists(),'output exists; refusing overwrite');m,images,proof=decode(args.readback,args.game_log,args.manifest_sha256);raw,nr=images;rgb=[srgb_reinhard(x[...,:3],args.exposure_ev) for x in images]
 config=None
 if args.config:
  digest=sha(args.config)
  if args.config_sha256:need(digest==args.config_sha256,'configuration SHA mismatch')
  config={'path':str(args.config),'sha256':digest,'value':json.loads(args.config.read_text()),'scope':'requested config file only; actual applied configuration requires frame-bound renderer acknowledgment'}
 elif args.config_sha256:p.error('--config-sha256 requires --config')
 args.output.mkdir(mode=0o700,parents=True)
 for name,data in zip(('raw720-reinhard-srgb.png','nr720-reinhard-srgb.png'),rgb):Image.fromarray(data).save(args.output/name)
 canvas=Image.new('RGB',(2560,748));draw=ImageDraw.Draw(canvas)
 for i,(data,label) in enumerate(zip(rgb,('Current unedited raw720','Same-source final guarded NR720'))):canvas.paste(Image.fromarray(data),(i*1280,28));draw.text((i*1280+8,7),label+' | source '+str(m['current_source']),fill='white')
 canvas.save(args.output/'compare720.png');delta=nr.astype(np.float32)[...,:3]-raw.astype(np.float32)[...,:3]
 out={'schema':'nr-offline-common-transform-compare-v1','session':m['session'],'source':m['current_source'],'extent':[1280,720],'linear_input':'RGBA16F render-domain values; no inferred NR/SR output sampling-phase correction','transform':{'exposure_ev_shared':args.exposure_ev,'exposure_multiplier_shared':2.**args.exposure_ev,'negative_RGB':'clamp to zero for display only','tonemap':'Reinhard x/(1+x), independently per RGB channel','transfer':'IEC sRGB OETF threshold .0031308; round to 8-bit','automatic_or_independent_exposure':False},'negative_raw_channels':int((raw[...,:3]<0).sum()),'negative_nr_channels':int((nr[...,:3]<0).sum()),'actual_RGB_numeric_changed_fraction':float(np.any(delta!=0,axis=-1).mean()),'max_abs_linear_delta':float(np.abs(delta).max()),'requested_config':config,'proof':proof,'output_sha256':{x.name:sha(x) for x in args.output.glob('*.png')},'scope':'offline same-source720 visualization only; no fake SR1080 A/B, no strict dual-history same-frame game comparison, no hot-performance or dynamic-quality acceptance'}
 (args.output/'compare.json').write_text(json.dumps(out,indent=2,allow_nan=False)+'\n');print(json.dumps({'source':out['source'],'output':str(args.output),'same_source_SR_Present_joined':True,'transform':out['transform']},indent=2));return 0
if __name__=='__main__':raise SystemExit(main())
