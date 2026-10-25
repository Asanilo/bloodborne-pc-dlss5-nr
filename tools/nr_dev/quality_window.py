#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Wait for actual NR candidates, run bounded inputs, optionally request normal close.

Starts no game/Helper/GPU context. Start this watcher before confirming scene-ready
in play.py so its short trace budget is not consumed by manual preparation.
"""
import argparse,hashlib,json,subprocess,sys,time,re
from pathlib import Path
import runner,quality_scenario,metrics


def tail(run):
    with (run/'game.log').open('rb') as f:
        f.seek(0,2);f.seek(max(0,f.tell()-262144));return f.read().decode(errors='replace')


def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('session');p.add_argument('routine',choices=quality_scenario.PATTERNS)
    p.add_argument('--label',required=True)
    p.add_argument('--close',action='store_true');p.add_argument('--min-fps',type=float,default=0)
    a=p.parse_args();run=runner.checked_run(a.session)
    if not re.fullmatch('[a-zA-Z0-9-]{1,50}',a.label) or not 0<=a.min_fps<=240:p.error('safe label and min-fps 0..240 required')
    manifest=json.loads((run/'manifest.json').read_text());runner.validate_daily_manifest(run,manifest)
    guard=runner.daily_session_guard.SessionGuard().read()
    if not guard or guard['state']!='pending' or guard['session']!=a.session or guard['root']!=str(runner.ROOT):raise ValueError('exact live managed session required')
    identity=runner.current_game(run);path=run/f'quality-window-{a.label}.json'
    record=dict(session=a.session,routine=a.routine,identity=identity,created_ns=time.monotonic_ns(),
                native_build=manifest['native_build_provenance'],procedure_status='WAITING_NR',
                tool_sha256={f:hashlib.sha256(Path(__file__).with_name(f).read_bytes()).hexdigest()
                    for f in ('quality_window.py','quality_scenario.py')})
    with path.open('x') as f:json.dump(record,f,indent=2)
    def save():
        temp=path.with_suffix('.tmp');temp.write_text(json.dumps(record,indent=2)+'\n');temp.replace(path)
    try:
        deadline=time.monotonic()+45
        while time.monotonic()<deadline:
            if runner.live(identity['pid'])!=identity:raise ValueError('game identity changed while waiting')
            text=tail(run)
            candidates=text.count('PERSISTENT_HISTORY_CANDIDATE_RECORDED ')
            samples=metrics.rows(text,'NR_HISTORY_GPU_SAMPLE')
            if candidates>=3 and any(float(x['valid'])>0 for x in samples):break
            time.sleep(.1)
        else:raise TimeoutError('no actual NR candidate and completed positive-valid sample')
        record['nr_ready_observed_ns']=time.monotonic_ns();time.sleep(1.5)
        text=tail(run);rows=metrics.rows(text,'MEMORY_PRESENT_V1')[-60:]
        if a.min_fps and len(rows)<30:raise ValueError('min-fps requires BB_MEMORY_PROFILE=1 actual Present ledger')
        if len(rows)>=30:
            times=[int(x['cpu_monotonic_ns']) for x in rows]
            if any(b<=a for a,b in zip(times,times[1:])):raise ValueError('nonmonotonic actual Present ledger')
            record['pre_input_fps']=(len(times)-1)*1e9/(times[-1]-times[0])
            if record['pre_input_fps']<a.min_fps:raise ValueError('normal-load FPS condition not met')
        runner.control(argparse.Namespace(command='focus',session=a.session));time.sleep(.2)
        record['procedure_status']='INPUT_RUNNING';save()
        subprocess.run([sys.executable,str(Path(__file__).with_name('quality_scenario.py')),a.session,a.routine,'--label',a.label],check=True,timeout=30)
        record['procedure_status']='INPUT_COMPLETED';record['completed_ns']=time.monotonic_ns()
    except Exception as e:
        record['procedure_status']='FAILED';record['error']=str(e);raise
    finally:
        try:
            if a.close and runner.live(identity['pid'])==identity:
                runner.control(argparse.Namespace(command='stop',session=a.session))
                record['normal_close_requested_ns']=time.monotonic_ns()
        finally:
            record['retirement_scope']='Close request only; result.json/actual owner proof remains authoritative'
            save()


if __name__=='__main__':main()
