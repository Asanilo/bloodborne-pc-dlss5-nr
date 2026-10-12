#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""CPU regression only, never simulates a real model/GPU PASS."""
import argparse
import copy
import json
from pathlib import Path
import numpy as np
from common import ROOT, spec, sha, strict_json, load_asset, scene, output_grid, noisy_source, guides, masks
from audit import validate_case, analyze_behavior, compare_pairs, EvidenceError, PARAMETER_APPLICATION, validate_parameter_application, validate_evaluate_parameters


def check(out):
    plan=spec();index=strict_json(out/'asset-index.json')
    assert index['spec_sha256']==sha(ROOT/'fixture-spec.json') and len(index['cases'])==6
    verified=0
    for a in index['assets']:
        v=load_asset(a);assert v.nbytes==a['bytes'];verified+=1
    cases=[]
    for entry in index['cases']:
        assert sha(entry['path'])==entry['sha256'];m=strict_json(entry['path']);validate_case(m);cases.append(m)
    source_pairs=[[(f['source']['sha256'],f['source']['bytes']) for f in m['branches'][0]['frames']] for m in cases]
    assert all(p==source_pairs[0] for p in source_pairs)
    x,y=output_grid();true0=scene(x,y,0)[0];true1=scene(x,y,1)[0];mm=masks()
    shifted,shifted_z,shifted_fg,_=scene(x-12,y+6,0)
    _,current_z,current_fg,_=scene(x,y,1)
    assert np.array_equal(current_fg,shifted_fg)
    assert np.array_equal(true1[current_fg],shifted[current_fg])
    assert np.array_equal(current_z[current_fg],shifted_z[current_fg])
    assert np.array_equal(true1[mm['background_interior']],true0[mm['background_interior']])
    assert np.array_equal(noisy_source(true1,1),noisy_source(true1,1))
    phase_counts=[]
    for f in range(2):
        dr,mr=guides([1280,720],'raw',f);dc,mc=guides([1280,720],'corrected',f)
        n=int(np.any(mr!=mc,axis=-1).sum());assert n>0
        assert (mr==0).any() and (mr!=0).any()
        assert np.all((dr>=0)&(dr<=1)) and np.all((dc>=0)&(dc<=1))
        assert set(np.unique(mr[...,0]).tolist())=={-8.0,0.0}
        assert set(np.unique(mr[...,1]).tolist())=={0.0,4.0}
        d1080,m1080=guides([1920,1080],'corrected',f)
        assert set(np.unique(m1080[...,0]).tolist())=={-12.0,0.0}
        assert set(np.unique(m1080[...,1]).tolist())=={0.0,6.0}
        phase_counts.append({'frame':f,'raw_vs_corrected_MV_boundary_pixels':n,'depth_float_bits_differ':int((dr.view('<u4')!=dc.view('<u4')).sum())})
    # Tiny exact oracle fixtures test thresholds; these are NOT model output.
    tiny=np.zeros((16,16,4),np.float32);tiny[...,3]=1
    noisy=tiny.copy();noisy[...,:3]=.1
    mask=np.ones((16,16),bool)
    tm={k:mask.copy() for k in mm}
    controls={k:noisy.copy() for k in plan['branches']};controls['primary']=tiny.copy();controls['repeat_primary']=tiny.copy()
    good=analyze_behavior(controls,tiny,noisy,tm,plan['oracle'])
    assert good['MV_and_history_sensitive'] and good['utility_vs_all_negative_controls']
    inert=analyze_behavior({k:noisy.copy() for k in plan['branches']},tiny,noisy,tm,plan['oracle'])
    assert inert['status']=='NOT_IDENTIFIED' and not inert['MV_and_history_sensitive']
    noisyrepeat=copy.deepcopy(controls);noisyrepeat['repeat_primary'][...,:3]=.03
    uncertain=analyze_behavior(noisyrepeat,tiny,noisy,tm,plan['oracle'])
    assert uncertain['status']=='NOT_IDENTIFIED' and not uncertain['MV_and_history_sensitive']
    def result(case_id,behavior):
        case=next(c for c in cases if c['case_id']==case_id)
        return {'case_id':case_id,'status':'PASS_INDEPENDENT_FUNCTIONAL_AUDIT','behavior':behavior,
                'device_uuid':'0'*32,'driver_uuid':'1'*32,'model_parameters':plan['model_parameters'],
                'parameter_application':copy.deepcopy(PARAMETER_APPLICATION),
                'plan_sha256':sha(ROOT/'fixture-spec.json'),'oracle_sha256':sha(ROOT/'audit.py'),
                'case_variables':{k:case[k] for k in ['guide_extent','phase','nr_mvec_scale']},
                'input_provenance':{'Color_sha256':[f['source']['sha256'] for f in case['branches'][0]['frames']],
                                    'truth_sha256':[f['sha256'] for f in case['oracle']['truth_frames']]}}
    pair=[result('ref1080-s1',good),result('ref1080-s15',inert)]
    paired=compare_pairs(pair);assert paired['comparisons'][0]['preferred_case']=='ref1080-s1'
    ambiguous=compare_pairs([result('ref1080-s1',inert),result('ref1080-s15',inert)])
    assert ambiguous['comparisons'][0]['status']=='NOT_IDENTIFIED'
    provenance_rejected=[]
    for label,mutate in [('duplicate_case',lambda rs:rs.append(copy.deepcopy(rs[0]))),
                         ('different_device',lambda rs:rs[1].update(device_uuid='2'*32)),
                         ('different_driver',lambda rs:rs[1].update(driver_uuid='2'*32)),
                         ('different_Color',lambda rs:rs[1]['input_provenance']['Color_sha256'].__setitem__(1,'0'*64)),
                         ('different_truth',lambda rs:rs[1]['input_provenance']['truth_sha256'].__setitem__(1,'0'*64)),
                         ('different_model',lambda rs:rs[1].update(model_parameters={})),
                         ('missing_application',lambda rs:rs[1].pop('parameter_application')),
                         ('false_application',lambda rs:rs[1]['parameter_application'].update(frame_ms='SENT_TO_NR_API')),
                         ('verified_default_claim',lambda rs:rs[1]['parameter_application'].update(runtime_default_values='VERIFIED')),
                         ('extra_application_key',lambda rs:rs[1]['parameter_application'].update(other='UNVERIFIED')),
                         ('different_oracle',lambda rs:rs[1].update(oracle_sha256='0'*64)),
                         ('different_plan',lambda rs:rs[1].update(plan_sha256='0'*64))]:
        rs=copy.deepcopy(pair);mutate(rs)
        try:compare_pairs(rs)
        except (EvidenceError,KeyError,ValueError):provenance_rejected.append(label)
        else:raise AssertionError(label+' accepted')
    application_rejected=[]
    validate_parameter_application({'parameter_application':copy.deepcopy(PARAMETER_APPLICATION)})
    for label,v in [('missing',{}),('null',{'parameter_application':None}),
                    ('wrong_type',{'parameter_application':list(PARAMETER_APPLICATION)}),
                    ('wrong_state',{'parameter_application':dict(PARAMETER_APPLICATION,exposure=1.0)})]:
        try:validate_parameter_application(v)
        except EvidenceError:application_rejected.append(label)
        else:raise AssertionError(label+' application accepted')
    # Actual scalar/resource recording remains separate from the request echo.
    c=cases[0];gw,gh=c['guide_extent'];sx,sy=c['nr_mvec_scale']
    ep={'width':1920,'height':1080,'guide_width':gw,'guide_height':gh,'depth_inverted':0,
        'reset':1,'intensity':1.0,'style':0,'local_structure':1.0,'local_tone':1.0,
        'skin_structure':1.0,'use_auto_mask':1,'mvec_scale_x':sx,'mvec_scale_y':sy,
        'enabled':1,'jitter_setter':'NONE','subrects':{'color':[0,0,1920,1080],
        'output':[0,0,1920,1080],'depth':[0,0,gw,gh],'mvec':[0,0,gw,gh]},
        'resources':{'color':{'role':'encoded_proxy','format':109,'extent':[1920,1080]},
        'depth':{'role':'uploaded_depth','format':100,'extent':[gw,gh]},
        'mvec':{'role':'uploaded_mv','format':103,'extent':[gw,gh]},
        'output':{'role':'model_raw','format':97,'extent':[1920,1080]}}}
    validate_evaluate_parameters({'evaluate_parameters':ep},c,1)
    evaluate_rejected=[]
    for label,mutate in [('false_frame_ms',lambda p:p.update(frame_ms=1000/60)),
                         ('false_exposure',lambda p:p.update(exposure=1.0)),
                         ('false_pre_exposure',lambda p:p.update(pre_exposure=1.0)),
                         ('different_scale',lambda p:p.update(mvec_scale_x=2.0)),
                         ('wrong_guide_extent',lambda p:p['resources']['depth'].update(extent=[1920,1080] if gw==1280 else [1280,720]))]:
        v=copy.deepcopy(ep);mutate(v)
        try:validate_evaluate_parameters({'evaluate_parameters':v},c,1)
        except EvidenceError:evaluate_rejected.append(label)
        else:raise AssertionError(label+' Evaluate accepted')
    # Fail-closed schema/candidate/history tests, no asset/data mutation.
    rejected=[]
    for label,mutate in [('missing_branch',lambda m:m['branches'].pop()),
                         ('history_reset',lambda m:m['branches'][0]['frames'][1].update(reset=1)),
                         ('changed_color',lambda m:m['branches'][2]['frames'][1]['source'].update(sha256='0'*64)),
                         ('changed_plan',lambda m:m.update(plan_sha256='0'*64))]:
        c=copy.deepcopy(cases[0]);mutate(c)
        try:validate_case(c)
        except (EvidenceError,KeyError,ValueError):rejected.append(label)
        else:raise AssertionError(label+' accepted')
    return {'status':'PASS_CPU_REGRESSION_ONLY','GPU':'NOT_RUN','Wine':'NOT_RUN','model_calls':0,
            'assets_verified':verified,'cases_verified':len(cases),'canonical_Color_across_cases':'BYTE_SHA_IDENTICAL',
            'physical_foreground_translation':'EXACT_F32_SCENE_EQUAL','stationary_background':'EXACT_F32_SCENE_EQUAL',
            'phase_boundary_sanity':phase_counts,'masks_pixels':{k:int(v.sum()) for k,v in mm.items()},
            'oracle_tests':['known_ideal_vs_bad_controls','inert_controls_NOT_IDENTIFIED','repeat_noise_suppresses_claim','matched_pair','ambiguous_pair_NOT_IDENTIFIED'],
            'parameter_application_rejection_tests':application_rejected,'actual_evaluate_rejection_tests':evaluate_rejected,
            'fail_closed_tests':rejected,'matched_provenance_rejection_tests':provenance_rejected,'synthetic_oracle_test_not_GPU_evidence':True}


if __name__=='__main__':
    p=argparse.ArgumentParser();p.add_argument('--assets',type=Path,required=True)
    print(json.dumps(check(p.parse_args().assets.resolve()),indent=2,allow_nan=False))
