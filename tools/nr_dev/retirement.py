#!/usr/bin/env python3
"""Offline terminal/tuple/GPU-FD audit extracted from the verified prior audit, no ledger imports."""
import json
from pathlib import Path
def load(p):return json.loads(Path(p).read_text())
def need(v,reason):
 if not v:raise ValueError(reason)
def audit_origin_jobs_and_retirement(result, run, session, out, expected_jobs=4,
                                     continuous_max_jobs=None):
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
    need(helper['status'] == 'PASS' and helper['session'] == session and helper['normal_release'] is True
         and helper['normal_shutdown'] is True and helper['feature_create_calls'] == helper['feature_release_calls'] == 1,
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
        previous_serial = complete['helper_gpu_serial']
        previous_origin = inp['origin_source']
        jobs.append({'job': ordinal, 'origin_source': inp['origin_source'],
                     'disposition': consumed['disposition'], 'gpu_times': complete['gpu_times']})
    out.update(jobs=jobs, terminal_counts=dict(zip(countkeys, counts)),
               retirement='PASS_NATIVE_HELPER_GPU_FD_MODEL_NORMAL_REAP')
    return helper, counts, jobs


def audit(run,session):
 helper=load(run/'async-wine-result.json');count=helper['finalized_jobs'];out={}
 audit_origin_jobs_and_retirement({'actual_helper_starts':1},run,session,out,expected_jobs=count,continuous_max_jobs=16384)
 return out
