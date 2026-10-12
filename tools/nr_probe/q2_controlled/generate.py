#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""Generate bounded canonical CPU assets, NOT GPU shared pixels or model execution."""
import argparse
import json
from pathlib import Path
import numpy as np
from common import ROOT, SCHEMA, BRANCHES, spec, sha, scene, output_grid, noisy_source, guides, masks, asset_ref


def generate(out):
    if out.exists():raise ValueError('refuse existing output directory')
    out.mkdir(parents=True);assets=out/'assets';assets.mkdir()
    s=spec();refs={};x,y=output_grid()
    def write(key, array, fmt):
        p=assets/(key+'.bin');array.tofile(p);refs[key]=asset_ref(p,array,fmt);return refs[key]
    for f in range(2):
        truth=scene(x,y,f)[0]
        write('truth-f'+str(f),truth,109)
        write('source-f'+str(f),noisy_source(truth,f),109)
    maskrefs={}
    for name,m in masks().items():
        a=m.astype('u1');maskrefs[name]=write('mask-'+name,a,0)
    guide_sets={}
    for c in s['cases']:
        gkey=str(c['guide_extent'][0])+'-'+c['phase']
        if gkey in guide_sets:continue
        guide_sets[gkey]=[]
        for f in range(2):
            depth,motion=guides(c['guide_extent'],c['phase'],f)
            records={'depth':write(gkey+'-depth-f'+str(f),depth,100)}
            for kind,a in [('primary',motion),('zero_mv',np.zeros_like(motion)),('reverse_mv',-motion)]:
                records[kind]=write(gkey+'-'+kind+'-mv-f'+str(f),a,103)
            guide_sets[gkey].append(records)
    files=[]
    for c in s['cases']:
        gkey=str(c['guide_extent'][0])+'-'+c['phase']
        branches=[]
        for branch in BRANCHES:
            frames=[]
            for f in range(2):
                gs=guide_sets[gkey][f];kind=branch if branch in ('zero_mv','reverse_mv') else 'primary'
                frames.append({'frame':f,'reset':1 if f==0 or branch=='reset_second' else 0,
                               'source':refs['source-f'+str(f)],'depth':gs['depth'],'mv':gs[kind]})
            branches.append({'branch_id':branch,'fresh_feature':True,'frames':frames})
        m={'schema':SCHEMA,**c,'model_extent':s['model_extent'],'branches':branches,
           'model_parameters':s['model_parameters'],'budgets_ms':s['budgets_ms'],
           'translation_output_pixels':s['translation_output_pixels'],
           'jitter_render_reference_pixels':s['jitter_render_reference_pixels'],
           'known_color_output_phase':[0,0],
           'oracle':{'truth_frames':[refs['truth-f0'],refs['truth-f1']],
                     'masks':maskrefs,'criteria':s['oracle']},
           'plan_sha256':sha(ROOT/'fixture-spec.json'),
           'asset_input_scope':'CPU_generated_canonical_files_then_same_process_GPU_upload_not_shared_transport'}
        p=out/(c['case_id']+'.json');p.write_text(json.dumps(m,indent=2,allow_nan=False)+'\n')
        files.append({'case_id':c['case_id'],'path':str(p.resolve()),'sha256':sha(p)})
    result={'schema':SCHEMA,'status':'PASS_CPU_ASSET_GENERATION_ONLY','GPU':'NOT_RUN',
            'spec_sha256':sha(ROOT/'fixture-spec.json'),'generator_sha256':sha(Path(__file__)),
            'numpy_version':np.__version__,'cases':files,'assets':list(refs.values()),
            'total_asset_bytes':sum(a['bytes'] for a in refs.values()),
            'mask_pixels':{n:int(a.sum()) for n,a in masks().items()},
            'NR_phase':'UNVERIFIED','game_k_output':'NOT_IDENTIFIED'}
    (out/'asset-index.json').write_text(json.dumps(result,indent=2)+'\n')
    print(json.dumps({'status':result['status'],'cases':len(files),'assets':len(refs),
                      'bytes':result['total_asset_bytes'],'index':str(out/'asset-index.json')}))


if __name__=='__main__':
    p=argparse.ArgumentParser();p.add_argument('--out',type=Path,required=True)
    generate(p.parse_args().out.resolve())
