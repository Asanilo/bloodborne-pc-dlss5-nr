#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""Independent CPU oracle. Runtime files are read-only; stdout JSON, no model/GPU calls."""
import argparse
import json
import math
from pathlib import Path
import numpy as np
from common import ROOT, SCHEMA, BRANCHES, W, H, spec, sha, strict_json, load_asset


PARAMETER_APPLICATION = {
    'frame_ms':'HOST_FIXTURE_DECLARATION_NOT_SENT_TO_NR_API',
    'exposure':'HOST_FIXTURE_DECLARATION_NOT_SENT_TO_NR_API',
    'pre_exposure':'HOST_FIXTURE_DECLARATION_NOT_SENT_TO_NR_API',
    'runtime_default_values':'UNVERIFIED',
}


class EvidenceError(ValueError):
    pass


def need(ok, reason):
    if not ok:raise EvidenceError(reason)


def integer(obj, key, expected):
    need(type(obj.get(key)) is int and obj[key]==expected,'integer '+key)


def boolean(obj,key,expected):
    need(type(obj.get(key)) is bool and obj[key] is expected,'boolean '+key)


def validate_parameter_application(obj):
    v=obj.get('parameter_application')
    need(type(v) is dict and v==PARAMETER_APPLICATION,
         'missing/different parameter_application; requested values are not actual NR API arguments')


def validate_evaluate_parameters(frame,case,reset):
    p=frame.get('evaluate_parameters')
    need(type(p) is dict,'actual evaluate_parameters missing')
    gw,gh=case['guide_extent'];sx,sy=case['nr_mvec_scale']
    for k,n in [('width',W),('height',H),('guide_width',gw),('guide_height',gh),
                ('depth_inverted',0),('reset',reset),('style',0),('use_auto_mask',1),('enabled',1)]:
        integer(p,k,n)
    for k,n in [('intensity',1.0),('local_structure',1.0),('local_tone',1.0),
                ('skin_structure',1.0),('mvec_scale_x',sx),('mvec_scale_y',sy)]:
        need(type(p.get(k)) in (int,float) and math.isfinite(p[k]) and p[k]==n,'actual Evaluate '+k)
    need(p.get('jitter_setter')=='NONE','nonexistent jitter setter')
    need(not any(k in p for k in ['frame_ms','exposure','pre_exposure']),
         'declaration falsely recorded as actual Evaluate argument')
    need(p.get('subrects')=={'color':[0,0,W,H],'output':[0,0,W,H],
                           'depth':[0,0,gw,gh],'mvec':[0,0,gw,gh]},'actual Evaluate subrects')
    need(p.get('resources')=={
        'color':{'role':'encoded_proxy','format':109,'extent':[W,H]},
        'depth':{'role':'uploaded_depth','format':100,'extent':[gw,gh]},
        'mvec':{'role':'uploaded_mv','format':103,'extent':[gw,gh]},
        'output':{'role':'model_raw','format':97,'extent':[W,H]}},'actual Evaluate resources')


def validate_case(m):
    s=spec();need(m['schema']==SCHEMA,'case schema')
    matches=[c for c in s['cases'] if c['case_id']==m['case_id']]
    need(len(matches)==1,'unknown case')
    for k,v in matches[0].items():need(m[k]==v,'case changed '+k)
    need(m['model_extent']==[W,H] and m['model_parameters']==s['model_parameters'],'model parameters')
    need(m['plan_sha256']==sha(ROOT/'fixture-spec.json') and m['oracle']['criteria']==s['oracle'],'preregistered oracle')
    need([b['branch_id'] for b in m['branches']]==BRANCHES,'exact five branches')
    for b in m['branches']:
        boolean(b,'fresh_feature',True)
        need(len(b['frames'])==2,'exact two frames')
        for f,row in enumerate(b['frames']):
            integer(row,'frame',f);integer(row,'reset',int(f==0 or b['branch_id']=='reset_second'))
            need(row['source']['extent']==[W,H] and row['source']['format']==109,'source format/extent')
            need(row['depth']['extent']==m['guide_extent'] and row['depth']['format']==100,'depth format/extent')
            need(row['mv']['extent']==m['guide_extent'] and row['mv']['format']==103,'MV format/extent')
            need(row['source']['sha256']==m['branches'][0]['frames'][f]['source']['sha256'],'Color changed between controls')
            need(row['depth']['sha256']==m['branches'][0]['frames'][f]['depth']['sha256'],'Depth changed between controls')
    return s


def rms(a,b,mask):
    delta=a[...,:3][mask].astype(np.float64)-b[...,:3][mask].astype(np.float64)
    return float(np.sqrt(np.mean(delta*delta)))


def summarize(output,truth,source,mask):
    e=rms(output,truth,mask);base=rms(source,truth,mask)
    return {'pixels':int(mask.sum()),'mse':e*e,'rmse':e,'input_mse':base*base,
            'mse_to_input_ratio':e*e/(base*base) if base else None}


def local_readback(run,relative,shape,dtype):
    need(type(relative) is str,'readback path string')
    p=run/relative
    need(not Path(relative).is_absolute() and p.resolve().is_relative_to(run.resolve()),'readback outside run')
    need(p.is_file() and not p.is_symlink(),'readback missing or symlink '+relative)
    expected=int(np.prod(shape))*np.dtype(dtype).itemsize
    need(p.stat().st_size==expected,'readback size '+relative)
    a=np.fromfile(p,dtype=dtype).reshape(shape)
    return a,{'path':str(p.resolve()),'bytes':expected,'sha256':sha(p)}


def analyze_behavior(outputs,truth,source,masks_,criteria):
    scores={name:{region:summarize(a,truth,source,mask) for region,mask in masks_.items()}
            for name,a in outputs.items()}
    primary=outputs['primary'];repeat=outputs['repeat_primary']
    repeat_rms={region:rms(primary,repeat,mask) for region,mask in masks_.items()}
    bounds={r:max(criteria['rms_floor'],criteria['repeat_noise_multiplier']*v) for r,v in repeat_rms.items()}
    effects={b:{r:rms(primary,outputs[b],mask) for r,mask in masks_.items()}
             for b in ['zero_mv','reverse_mv','reset_second']}
    regions=['all','foreground_interior','foreground_boundary']
    sensitive=all(effects[b]['all']>bounds['all'] for b in effects)
    useful=all(scores['primary'][r]['mse'] <= (1-criteria['minimum_relative_mse_advantage'])*scores[b][r]['mse']
               and scores[b][r]['rmse']-scores['primary'][r]['rmse']>bounds[r]
               for r in regions for b in effects)
    return {'status':'SENSITIVE_AND_USEFUL_ON_FIXED_SYNTHETIC_INPUT' if sensitive and useful else 'NOT_IDENTIFIED',
            'MV_and_history_sensitive':sensitive,'utility_vs_all_negative_controls':useful,
            'repeat_reference_rms':repeat_rms,'repeat_bound':bounds,'negative_control_effect_rms':effects,
            'scores':scores,'inference_scope':'synthetic known phase only, not game k_output or NR parameter universal semantics'}


def audit(case,run):
    m=strict_json(case);s=validate_case(m);rp=run/'run-result.json'
    need(rp.is_file(),'run-result missing; runtime NOT_RUN/BLOCKED')
    d=strict_json(rp);need(d['schema']==SCHEMA and d['case_id']==m['case_id'],'result case')
    need(d['status']=='PASS','fixture did not finish PASS; preserve raw failure')
    validate_parameter_application(d)
    for k in ['model_extent','guide_extent','phase','nr_mvec_scale','model_parameters']:
        need(d[k]==m[k],'actual fixture differs '+k)
    for k in ['device_uuid','driver_uuid']:
        v=d[k];need(type(v) is str and len(v)==32 and all(c in '0123456789abcdef' for c in v),'UUID '+k)
    for k,n in [('feature_create_calls',5),('feature_release_calls',5),('evaluate_calls',10),('resolve_calls',10)]:integer(d,k,n)
    boolean(d,'normal_shutdown',True)
    need([b['branch_id'] for b in d['branches']]==BRANCHES,'result five branches')
    truth=load_asset(m['oracle']['truth_frames'][1]);source=load_asset(m['branches'][0]['frames'][1]['source'])
    masks_={k:load_asset(v).astype(bool) for k,v in m['oracle']['masks'].items()}
    for k,v in masks_.items():need(v.any(),'empty mask '+k)
    evidence=[];frame_checks=[];outputs={};asset_cache={}
    def asset(a):
        if a['sha256'] not in asset_cache:asset_cache[a['sha256']]=load_asset(a)
        return asset_cache[a['sha256']]
    for ordinal,(request,result) in enumerate(zip(m['branches'],d['branches']),1):
        integer(result,'feature_ordinal',ordinal)
        for k in ['fresh_feature_created','creation_GPU_complete','last_GPU_complete_before_release','feature_released']:boolean(result,k,True)
        need(len(result['frames'])==2,'result exactly two frames')
        for f,(q,v) in enumerate(zip(request['frames'],result['frames'])):
            integer(v,'frame',f);integer(v,'reset',q['reset'])
            validate_parameter_application(v);validate_evaluate_parameters(v,m,q['reset'])
            for k,n in [('evaluate_calls',1),('evaluate_result',1),('resolve_calls',1)]:integer(v,k,n)
            boolean(v,'completed_GPU',True);boolean(v,'upload_readback_equal',True)
            for role,channels in [('source',4),('depth',1),('mv',2)]:
                a=asset(q[role]);g=v[role+'_readback'];observed,ref=local_readback(run,g,a.shape,'<f4');evidence.append(ref)
                need(observed.tobytes()==a.tobytes(),'GPU '+role+' upload/readback mismatch')
                if role=='mv':
                    primary=asset(m['branches'][0]['frames'][f]['mv'])
                    expected=np.zeros_like(primary) if request['branch_id']=='zero_mv' else -primary if request['branch_id']=='reverse_mv' else primary
                    need(a.tobytes()==expected.tobytes(),'MV control asset is not exact intended mutation')
            raw,rref=local_readback(run,v['raw_readback'],(H,W,4),'<f2');evidence.append(rref)
            out,oref=local_readback(run,v['output_readback'],(H,W,4),'<f4');evidence.append(oref)
            need(np.isfinite(raw[...,:3]).all(),'raw RGB NaN poison not fully overwritten')
            need(np.isfinite(out).all() and not np.any(out==np.float32(-1234)),'resolved invalid/sentinel')
            need(np.array_equal(out[...,3].view('<u4'),asset(q['source'])[...,3].view('<u4')),'resolved source alpha bits')
            frame_checks.append({'branch':request['branch_id'],'frame':f,'reset':q['reset'],
                                 'real_API_and_GPU_recorded':True,'source_depth_mv_GPU_byte_equal':True,
                                 'raw_RGB_finite_written':W*H*3,'resolved_RGBA_finite_written':W*H*4,
                                 'source_alpha_bits_equal':W*H})
            if f==1:outputs[request['branch_id']]=out
    behavior=analyze_behavior(outputs,truth,source,masks_,s['oracle'])
    return {'schema':SCHEMA,'status':'PASS_INDEPENDENT_FUNCTIONAL_AUDIT','case_id':m['case_id'],
            'behavior':behavior,'frame_checks':frame_checks,'local_GPU_readback_evidence':evidence,
            'device_uuid':d['device_uuid'],'driver_uuid':d['driver_uuid'],
            'model_parameters':d['model_parameters'],'model_parameters_scope':'REQUEST_ECHO_ONLY',
            'parameter_application':d['parameter_application'],
            'case_variables':{k:d[k] for k in ['guide_extent','phase','nr_mvec_scale']},
            'input_provenance':{'Color_sha256':[m['branches'][0]['frames'][f]['source']['sha256'] for f in range(2)],
                                'truth_sha256':[a['sha256'] for a in m['oracle']['truth_frames']]},
            'case_manifest':{'path':str(case.resolve()),'sha256':sha(case)},
            'run_result':{'path':str(rp.resolve()),'sha256':sha(rp)},
            'plan_sha256':sha(ROOT/'fixture-spec.json'),'oracle_sha256':sha(Path(__file__)),
            'GPU':'NOT_RUN_BY_AUDITOR','game_k_output':'NOT_IDENTIFIED','game_NR_phase':'UNVERIFIED'}


def compare_pairs(results):
    need(bool(results),'no matched comparison inputs')
    ids=[r['case_id'] for r in results]
    need(len(set(ids))==len(ids),'duplicate case_id; never overwrite evidence')
    plan=spec();expected={c['case_id']:c for c in plan['cases']}
    keys=['device_uuid','driver_uuid','model_parameters','parameter_application','plan_sha256','oracle_sha256','input_provenance']
    validate_parameter_application(results[0])
    baseline={k:results[0][k] for k in keys}
    for r in results:
        validate_parameter_application(r)
        need(r['case_id'] in expected,'unexpected comparison case')
        need(r['status']=='PASS_INDEPENDENT_FUNCTIONAL_AUDIT','functional evidence not PASS')
        need(r['plan_sha256']==sha(ROOT/'fixture-spec.json') and r['oracle_sha256']==sha(Path(__file__)),'different plan/oracle version')
        need(r['model_parameters']==plan['model_parameters'],'different requested model parameters')
        variables={k:expected[r['case_id']][k] for k in ['guide_extent','phase','nr_mvec_scale']}
        need(r['case_variables']==variables,'case variables inconsistent with preregistration')
        for k in ['device_uuid','driver_uuid']:
            v=r[k];need(type(v) is str and len(v)==32 and all(c in '0123456789abcdef' for c in v),'comparison UUID')
        for k in ['Color_sha256','truth_sha256']:
            v=r['input_provenance'][k]
            need(type(v) is list and len(v)==2 and all(type(h) is str and len(h)==64 and all(c in '0123456789abcdef' for c in h) for h in v),'comparison input SHA')
        need({k:r[k] for k in keys}==baseline,'unmatched device/driver/model/application/plan/oracle/Color/truth provenance')
    by={r['case_id']:r for r in results}
    pairs=[('1080_scale','ref1080-s1','ref1080-s15'),('720_raw_scale','g720-raw-s1','g720-raw-s15'),
           ('720_corrected_scale','g720-corrected-s1','g720-corrected-s15'),
           ('720_scale1_phase','g720-raw-s1','g720-corrected-s1'),('720_scale1.5_phase','g720-raw-s15','g720-corrected-s15')]
    scored=[];criteria=spec()['oracle']
    for label,a,b in pairs:
        if a not in by or b not in by:
            scored.append({'comparison':label,'status':'NOT_RUN_MISSING_CASE'});continue
        x,y=by[a]['behavior'],by[b]['behavior']
        def wins(first,second):
            if not first['MV_and_history_sensitive'] or not first['utility_vs_all_negative_controls']:return False
            regions=['all','foreground_interior','foreground_boundary','fixed_even_tiles','fixed_odd_tiles']
            for r in regions:
                f,s=first['scores']['primary'][r],second['scores']['primary'][r]
                bound=max(first['repeat_bound'][r],second['repeat_bound'][r])
                if f['mse']>(1-criteria['minimum_relative_mse_advantage'])*s['mse'] or s['rmse']-f['rmse']<=bound:return False
            return True
        aw,bw=wins(x,y),wins(y,x)
        scored.append({'comparison':label,'status':'IDENTIFIED_ON_SYNTHETIC_FIXTURE_ONLY' if aw!=bw else 'NOT_IDENTIFIED',
                       'preferred_case':a if aw and not bw else b if bw and not aw else None})
    return {'status':'CONTROLLED_BEHAVIOR_COMPARISONS_ONLY','matched_provenance':baseline,'case_ids':ids,'comparisons':scored,
            'game_k_output':'NOT_IDENTIFIED','game_NR_phase':'UNVERIFIED','NR_universal_scale_semantics':'UNVERIFIED'}


def main():
    p=argparse.ArgumentParser();p.add_argument('--case',type=Path);p.add_argument('--run',type=Path)
    p.add_argument('--compare',nargs='+',type=Path)
    a=p.parse_args()
    try:
        if a.compare:
            need(not a.case and not a.run,'choose compare or case/run')
            rs=[strict_json(q) for q in a.compare]
            need(all(r['status']=='PASS_INDEPENDENT_FUNCTIONAL_AUDIT' for r in rs),'pair functional subgate not PASS')
            result=compare_pairs(rs)
        else:
            need(a.case is not None and a.run is not None,'case/run required')
            result=audit(a.case.resolve(),a.run.resolve())
        print(json.dumps(result,indent=2,allow_nan=False));return 0
    except (EvidenceError,ValueError,KeyError,OSError) as e:
        print(json.dumps({'status':'FAIL_OR_BLOCKED_INDEPENDENT_EVIDENCE_CHECK','reason':str(e),
                          'GPU':'NOT_RUN_BY_AUDITOR','behavior':'NOT_IDENTIFIED'},indent=2));return 2


if __name__=='__main__':raise SystemExit(main())
