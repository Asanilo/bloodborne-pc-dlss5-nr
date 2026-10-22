#!/usr/bin/env python3
"""Offline terminal/tuple/GPU-FD audit extracted from the verified prior audit, no ledger imports."""
import json,hashlib,re,argparse,math,importlib.util
from pathlib import Path
_log_spec=importlib.util.spec_from_file_location('nr_retirement_daily_log',Path(__file__).with_name('daily_log.py'))
daily_log=importlib.util.module_from_spec(_log_spec);_log_spec.loader.exec_module(daily_log)
def load(p):return json.loads(Path(p).read_text())
def need(v,reason):
 if not v:raise ValueError(reason)
def exact_typed(a,b):
 if type(a) is not type(b):return False
 if isinstance(a,dict):return a.keys()==b.keys() and all(exact_typed(a[k],b[k]) for k in a)
 if isinstance(a,list):return len(a)==len(b) and all(exact_typed(x,y) for x,y in zip(a,b))
 return a==b

def audit_model_generations(run,session,total_jobs):
 requests=list(run.glob('runtime-model-request-*.json'));acks=list(run.glob('runtime-model-ack-*.json'))
 need(len(requests)==len(acks),'every runtime model request has actual ACK')
 models=[];previous_generation=0;previous_params=1;previous_revision=0;previous_boundary=0;previous_serial=load(run/'async-meta-g0.json')['initial_release_GPU_serial']
 keys={'schema','session','revision','previous_generation','generation','previous_params','params','style','intensity','finalized_jobs','native_target_tick','native_observed_tick','packet_leases','shared_job_finalized','native_GPU_complete'}
 for generation in range(1,len(requests)+1):
  request=load(run/f'runtime-model-request-{generation}.json');ack=load(run/f'runtime-model-ack-{generation}.json')
  need(set(request)==keys and request['schema']=='nr-runtime-model-v1' and request['session']==session,'runtime request exact schema/session')
  ints=keys-{'schema','session','intensity','shared_job_finalized','native_GPU_complete'}
  need(all(type(request[k]) is int and 0<=request[k]<=2**64-1 for k in ints),'runtime model uint fields')
  need(type(request['intensity']) is float and math.isfinite(request['intensity']) and 0<=request['intensity']<=2 and 0<=request['style']<=2,'model API finite type/range')
  need(request['previous_generation']==previous_generation and request['generation']==generation
       and request['previous_params']==previous_params and request['params']>previous_params
       and request['revision']>previous_revision,'model generation/params/revision exact predecessor')
  need(previous_boundary<=request['finalized_jobs']<=total_jobs
       and request['packet_leases']==0 and request['shared_job_finalized'] is True
       and request['native_GPU_complete'] is True
       and request['native_observed_tick']>=request['native_target_tick'], 'model request actual idle/drain proof')
  extra={'op','status','helper_gpu_serial','helper_GPU_complete','feature_create_calls','feature_release_calls'}
  need(set(ack)==keys|extra and exact_typed({k:ack[k] for k in keys},request),'actual model ACK exact typed request')
  need(ack['op']=='model_ack' and ack['status']=='FEATURE_REBUILT' and ack['helper_GPU_complete'] is True
       and type(ack['helper_gpu_serial']) is int and ack['helper_gpu_serial']>previous_serial
       and type(ack['feature_create_calls']) is int and ack['feature_create_calls']==generation+1
       and type(ack['feature_release_calls']) is int and ack['feature_release_calls']==generation,'model actual Feature creation fence/API counts')
  models.append({'request':request,'ack':ack})
  previous_generation=generation;previous_params=request['params'];previous_revision=request['revision'];previous_boundary=request['finalized_jobs'];previous_serial=ack['helper_gpu_serial']
 return models

def audit_origin_jobs_and_retirement(result, run, session, out, expected_jobs=4,
                                     continuous_max_jobs=None, runtime_model=False):
    if continuous_max_jobs is None:
        need(type(expected_jobs) is int and expected_jobs in (4, 120), 'explicit bounded job audit scope')
    else:
        need(type(continuous_max_jobs) is int and 1 <= continuous_max_jobs <= 16384
             and type(expected_jobs) is int and 0 <= expected_jobs <= continuous_max_jobs,
             'new continuous-only actual N under fixed backup cap')
    need(result['actual_helper_starts'] == 1, 'one actual sparse Helper start')
    helper = load(run / 'async-wine-result.json')
    reaped = load(run / 'async-native-helper-exit.json')
    release = load(run / ('async-native-released-' + session + '-g0.json'))
    retired = load(run / ('async-retired-' + session + '-g0.json'))
    audited = load(run / ('async-native-audited-' + session + '-g0.json'))
    need(type(runtime_model) is bool,'explicit runtime model audit mode')
    models=audit_model_generations(run,session,expected_jobs) if runtime_model else []
    expected_features=1+len(models)
    if not runtime_model:
        need(not list(run.glob('runtime-model-request-*.json')) and not list(run.glob('runtime-model-ack-*.json')),'legacy model path cannot contain runtime rebuild evidence')
    need(helper['status'] == 'PASS' and helper['session'] == session and helper['normal_release'] is True
         and helper['normal_shutdown'] is True and helper['feature_create_calls'] == helper['feature_release_calls'] == expected_features,
         'actual model normal shutdown and API create/release')
    need(reaped['normal_exit'] is True and reaped['wait_status'] == 0, 'actual private peer waitpid0')
    countkeys = ('finalized_jobs', 'captured_jobs', 'obsolete_jobs', 'skip_jobs')
    counts = tuple(helper[k] for k in countkeys)
    need(counts[0] == sum(counts[1:]) == expected_jobs, 'all bounded jobs finalized with explicit disposition')
    for j in (release, retired, audited):
        need(j['session'] == session and tuple(j[k] for k in countkeys) == counts
             and j['source_frame_valid'] is (expected_jobs > 0), 'actualN/counts/terminal source identity')
    if release['native_last_use_proof'] == 'ACTUAL_NATIVE_GPU_LAST_USE':
        need(release['native_last_use_tick'] > 0
             and release['native_last_use_observed_tick'] >= release['native_last_use_tick']
             and release['completed_GPU'] is True, 'native actual maximum GPU last use')
    else:
        need(continuous_max_jobs is not None and expected_jobs == 0
             and release['native_last_use_proof'] == 'NOT_REQUIRED_NO_NATIVE_USE'
             and release['native_last_use_tick'] == release['native_last_use_observed_tick'] == 0
             and release['completed_GPU'] is False,
             'new continuous N0 still requires explicit absent-use proof, not count inference')
    need(all(release[k] == 0 for k in ('native_imported_images_owned', 'native_raw_duplicate_fd_owned', 'native_origin_leases_owned', 'native_cache_owners_owned')), 'native owned resources safely released')
    need(retired['normal_shared_retirement'] is True and retired['shared_images_owned'] == retired['export_raw_fd_owned'] == 0, 'real Helper shared resources retirement')
    need(audited['tracked_NR_FD_remaining'] == 0, 'targeted actual FD inventory retired')
    jobs = []
    previous_serial = 2
    previous_origin = 0
    prefix_native=[0];prefix_helper=[0]
    for ordinal in range(expected_jobs):
        prefix = run / 'frames' / f'bucket-{ordinal // 64:06d}'
        inp = load(prefix / f'async-input-{session}-g0-f{ordinal}.json')
        complete = load(prefix / f'async-complete-{session}-g0-f{ordinal}.json')
        consumed = load(prefix / f'async-consumed-{session}-g0-f{ordinal}.json')
        for key in ('session', 'generation', 'resource_set', 'job', 'origin_source', 'origin_epoch', 'origin_history', 'params_version'):
            need(inp[key] == complete[key] == consumed[key], 'immutable origin tuple across actual job')
        need(inp['job'] == ordinal and inp['native_input_GPU_complete'] is True
             and inp['origin_source'] > previous_origin
             and complete['native_input_tick_echo'] == inp['native_input_tick']
             and complete['helper_GPU_complete'] is True and complete['helper_gpu_serial'] > previous_serial
             and consumed['helper_gpu_serial_echo'] == complete['helper_gpu_serial']
             and consumed['native_shared_GPU_complete'] is True and consumed['native_shared_last_use_tick'] > 0,
             'actual distinct native tick and Helper fence proofs, safe consumed')
        if runtime_model:
            effective={'generation':0,'params':1,'style':0,'intensity':1.0}
            for model in models:
                if model['request']['finalized_jobs']<=ordinal:effective=model['request']
            sidecar=load(run/f'runtime-job-model-{ordinal}.json')
            model_ack=load(run/f'runtime-job-model-ack-{ordinal}.json')
            need(exact_typed(sidecar,model_ack),'runtime per-job actual Helper model ACK exact typed identity')
            expected={'schema':'nr-runtime-job-model-v1','session':session,'job':ordinal,
                      'origin_source':inp['origin_source'],'origin_epoch':inp['origin_epoch'],
                      'origin_history':inp['origin_history'],'model_generation':effective['generation'],
                      'params':effective['params'],'style':effective['style'],'intensity':effective['intensity']}
            need(exact_typed(sidecar,expected),'runtime per-job tuple and effective model generation/params/settings')
            if effective['generation']:
                need(complete['helper_gpu_serial']>models[effective['generation']-1]['ack']['helper_gpu_serial'],'job Evaluate follows actual generation creation fence')
            evaluated=complete['evaluate_parameters']
            if evaluated:
                need(evaluated['Style']==effective['style'] and type(evaluated['Style']) is int
                     and type(evaluated['Intensity']) is float and evaluated['Intensity']==effective['intensity'],
                     'actual Evaluate Style/Intensity match acknowledged Feature')
        prefix_native.append(max(prefix_native[-1],consumed['native_shared_last_use_tick']))
        prefix_helper.append(max(prefix_helper[-1],complete['helper_gpu_serial']))
        previous_serial = complete['helper_gpu_serial']
        previous_origin = inp['origin_source']
        jobs.append({'job': ordinal, 'origin_source': inp['origin_source'],
                     'disposition': consumed['disposition'], 'gpu_times': complete['gpu_times']})
    if runtime_model:
        need(type(helper['feature_create_calls']) is int and type(helper['feature_release_calls']) is int,
             'runtime Feature API count types')
        for model in models:
            request,ack=model['request'],model['ack'];boundary=request['finalized_jobs']
            need(prefix_native[boundary]<=request['native_target_tick']
                 and prefix_helper[boundary]<ack['helper_gpu_serial'],
                 'model rebuild proof covers all prior finalized shared uses and Helper completions')
        need(len(list(run.glob('runtime-job-model-*.json')))==2*expected_jobs,
             'runtime model exact request/ACK sidecar count')
        out.update(runtime_model=True,model_generations=models,feature_create_release_count=expected_features)
    out.update(jobs=jobs, terminal_counts=dict(zip(countkeys, counts)),
               retirement='PASS_NATIVE_HELPER_GPU_FD_MODEL_NORMAL_REAP')
    return helper, counts, jobs


def audit(run,session):
 if load(run/'manifest.json').get('daily') is True:return audit_daily(run,session)
 helper=load(run/'async-wine-result.json');count=helper['finalized_jobs'];out={}
 manifest=load(run/'manifest.json');runtime_model=manifest.get('runtime_model',False)
 audit_origin_jobs_and_retirement({'actual_helper_starts':1},run,session,out,expected_jobs=count,continuous_max_jobs=16384,runtime_model=runtime_model)
 return out

def audit_daily(run,session):
 """Terminal ownership audit for the bounded daily transport.

 Per-job files are deliberately retired after live tuple/fence validation. This
 verifies actual terminal ownership evidence, not a reconstructed per-job trace.
 """
 config=load(run/'async-config.json');meta=load(run/'async-meta-g0.json')
 helper=load(run/'async-wine-result.json');reaped=load(run/'async-native-helper-exit.json')
 release=load(run/f'async-native-released-{session}-g0.json')
 retired=load(run/f'async-retired-{session}-g0.json');audited=load(run/f'async-native-audited-{session}-g0.json')
 messages=(config,meta,helper,release,retired,audited)
 for message in messages:
  need(message.get('session')==session and message.get('generation')==0
       and message.get('resource_set')==session+'-g0'
       and type(message.get('protocol_version')) is int and message['protocol_version']==2
       and message.get('transport_layout')=='frame-ring-64-daily-v2','daily terminal protocol/session/resource identity')
 counts=('finalized_jobs','captured_jobs','obsolete_jobs','skip_jobs')
 total=helper['finalized_jobs']
 need(all(type(helper.get(k)) is int and 0<=helper[k]<=9007199254740991 for k in counts)
      and total==sum(helper[k] for k in counts[1:]),'daily actual finalized disposition counters')
 for message in (release,retired,audited):
  need(all(type(message.get(k)) is int and message[k]==helper[k] for k in counts)
       and message.get('source_frame_valid') is (total>0),'daily actual terminal counts agree')
 for message in (helper,release,retired,audited):
  need(message.get('job')==total and message.get('next_job')==total
       and all(message.get(k)==helper.get(k) for k in ('origin_source','origin_epoch','origin_history')),'daily final immutable origin identity')
 need(helper.get('op')=='result' and helper.get('status')=='PASS'
      and helper.get('normal_release') is True and helper.get('normal_shutdown') is True
      and helper.get('completed_GPU') is True,'actual Helper normal GPU shutdown')
 creates=helper.get('feature_create_calls');releases=helper.get('feature_release_calls')
 need(type(creates) is int and type(releases) is int and 1<=creates==releases<=9007199254740991,'daily real Feature create/release counts')
 need(reaped.get('normal_exit') is True and reaped.get('wait_status')==0
      and reaped.get('finalized_jobs')==total,'actual private Helper waitpid0')
 need(release.get('status')=='NATIVE_RELEASED' and release.get('output_valid') is False and release.get('model_valid') is False,'daily Native release stage')
 if release.get('native_last_use_proof')=='ACTUAL_NATIVE_GPU_LAST_USE':
  need(type(release.get('native_last_use_tick')) is int and release['native_last_use_tick']>0
       and type(release.get('native_last_use_observed_tick')) is int
       and release['native_last_use_observed_tick']>=release['native_last_use_tick']
       and release.get('completed_GPU') is True,'daily actual maximum Native GPU final use')
 else:
  need(total==0 and release.get('native_last_use_proof')=='NOT_REQUIRED_NO_NATIVE_USE'
       and release.get('native_last_use_tick')==release.get('native_last_use_observed_tick')==0
       and release.get('completed_GPU') is False,'daily explicit no-use proof')
 need(all(type(release.get(k)) is int and release[k]==0 for k in ('native_imported_images_owned','native_raw_duplicate_fd_owned','native_origin_leases_owned','native_cache_owners_owned')),'daily Native actual owners retired')
 need(retired.get('status')=='RETIRED' and retired.get('normal_shared_retirement') is True
      and retired.get('completed_GPU') is True and retired.get('shared_images_owned')==0
      and retired.get('export_raw_fd_owned')==0,'daily actual Helper shared GPU retirement')
 need(audited.get('status')=='FD_AUDITED' and audited.get('tracked_NR_FD_remaining')==0
      and all(exact_typed(audited.get(k),release.get(k)) for k in ('native_last_use_tick','native_last_use_observed_tick','native_last_use_proof','completed_GPU')),'daily targeted FD audit with same actual final-use proof')
 need(helper.get('evaluate_calls_total')==(total if helper.get('mode')=='resident' else 0)
      and helper.get('resolve_calls_total')==helper.get('evaluate_calls_total'),'daily actual Evaluate/resolve counts')
 need(not list(run.glob('runtime-model-request-*.json')) and not list(run.glob('runtime-model-ack-*.json')),'daily no pending model transition')
 need(not list(run.glob('runtime-job-model-*.json')) and not list((run/'frames').rglob('async-*.json')),'daily consumed transport files retired')
 return {'retirement':'PASS_NATIVE_HELPER_GPU_FD_MODEL_NORMAL_REAP','terminal_counts':{k:helper[k] for k in counts},
         'feature_create_release_count':creates,'daily':True,'job_evidence':'live exact tuple/fence checks; consumed ring files removed; no full forensic job trace claim'}

def rows(text,event):
 return [dict(re.findall(r'(\w+)=([^\s]+)',line)) for line in text.splitlines() if line.startswith(event+' ')]
def audit_session(run,termination=None):
 run=Path(run);manifest=load(run/'manifest.json');session=manifest['session']
 result=termination if termination is not None else load(run/'result.json')
 need(result.get('returncode')==0 and result.get('residuals')==[] and result.get('watchdog_expired') is False,'normal actual process exit, no recorded residual or watchdog expiry')
 log_evidence=None
 if manifest.get('daily') is True:
  text,log_evidence=daily_log.read_completed(run)
  log_hashes=log_evidence['file_sha256']
 else:
  raw=(run/'game.log').read_bytes();text=raw.decode(errors='replace')
  log_hashes={'game.log':hashlib.sha256(raw).hexdigest()}
 need(not re.search(r'NATIVE_\S*(?:HOLD|QUARANTINE|ASSOCIATION_REJECTED)|functional=FAIL',text),'no failed native resource/association proof')
 proof=rows(text,'NATIVE_ASYNC_PRE_SR_EXIT_PROOF')
 need(len(proof)==1 and proof[0].get('actual_source_cache_SR_GPU_lastuse_complete')=='1' and proof[0].get('managed_owner_joined')=='1' and proof[0].get('peer_retirement')=='OWNER_PROOF','exact PreSR last-use and joined-owner exit proof')
 nopeer=rows(text,'NATIVE_ASYNC_PRE_SR_EXIT_NO_PEER')
 if (run/'async-native-launch.json').exists():
  need(not nopeer,'peer launch contradicts no-peer proof')
  out=audit(run,session);out['path']='PEER_NORMAL_RETIREMENT'
 else:
  need(len(nopeer)==1 and nopeer[0].get('session')==session and nopeer[0].get('maximum_native_use_tick')=='0' and nopeer[0].get('scope')=='native_async_only' and all(nopeer[0].get(k)=='1' for k in ('actual_no_owner','actual_no_snapshot','actual_no_pending','preparation_joined','no_peer_created','no_NR_claim')),'explicit no-peer source/cache ownership and preparation-join proof')
  need(not any((run/p).exists() for p in ('async-config.json','native-scene-arm','async-meta-g0.json','async-wine-result.json','async-native-helper-exit.json')),'no-peer exit contradicts actual peer/config/arm evidence')
  need(not list(run.glob('runtime-model-*.json')) and not list(run.glob('runtime-job-model-*.json')),'no-peer exit contradicts model rebuild or job model evidence')
  need(not (run/'frames').exists() or not any((run/'frames').rglob('async-input-*.json')),'no-peer exit contradicts job input evidence')
  out={'retirement':'PASS_NO_PEER_NO_SNAPSHOT_NORMAL_EXIT','path':'NO_PEER_NORMAL_EXIT','nopeer':nopeer[0]}
 out.update(session=session,native_exit_proof=proof[0],quality='NOT_ACCEPTED',scope='offline recorded normal teardown evidence; current host safety remains independently checked')
 if log_evidence is not None:
  out['daily_game_log']=log_evidence
  out['game-log-state.json_sha256']=log_evidence['state_sha256']
 for name,digest in log_hashes.items():out[name+'_sha256']=digest
 for name in ('manifest.json','result.json'):
  if (run/name).is_file():out[name+'_sha256']=hashlib.sha256((run/name).read_bytes()).hexdigest()
 return out

def main():
 parser=argparse.ArgumentParser(description='Read-only session retirement audit; prints evidence and never changes prior result or writes resolution.')
 parser.add_argument('--run',type=Path,required=True);args=parser.parse_args()
 try:out=audit_session(args.run);code=0
 except (OSError,ValueError,KeyError,TypeError) as error:out={'retirement':'NOT_PROVED','error':str(error)};code=3
 print(json.dumps(out,indent=2));return code
if __name__=='__main__':raise SystemExit(main())
