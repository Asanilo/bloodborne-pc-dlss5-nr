#!/usr/bin/env python3
"""Finite Native/Wine GPU mailbox test under the existing global GPU guard."""
import argparse
import fcntl
import json
import os
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile
from build_wine_probe import identity,source_identity
from build_gpu_probe import sources
SOURCE=Path(__file__).resolve().parents[2]
sys.path.insert(0,str(SOURCE/'tools/nr_dev'))
import runner
import peer_retirement
from prepare_game_inputs import prepare


def main():
    p=argparse.ArgumentParser();p.add_argument('session');p.add_argument('--local-inputs',required=True)
    p.add_argument('--model',required=True);p.add_argument('--game-inputs',required=True)
    p.add_argument('--prefix-session',default='m2-sm-1080q-r1');p.add_argument('--frames',type=int,choices=[0,6],default=6)
    a=p.parse_args();run=runner.checked_run(a.session)
    if run.exists():raise ValueError('fresh GPU probe session required')
    binding=runner.runtime_inputs.load(a.local_inputs,runner.ROOT);runtime=binding['config']
    previous=runner.checked_run(a.prefix_session);old=json.loads((previous/'result.json').read_text())
    if old['status']!='PASS_NORMAL_TEARDOWN':raise ValueError('retired prefix required')
    peer_retirement.audit_session(previous);prefix=previous/'profile/wine-prefix'
    if prefix.resolve()!=prefix or not prefix.is_dir():raise ValueError('real private prefix required')
    path=Path(a.game_inputs).resolve(strict=True);inputs=json.loads(path.read_text())
    if inputs!=prepare(inputs['capture_directory']):raise ValueError('captured inputs drift')
    with open('/tmp/nrbridge-poc-gpu.lock','a') as lock:
        fcntl.flock(lock,fcntl.LOCK_EX|fcntl.LOCK_NB)
        helper_build=json.loads((SOURCE/'out/m2-wine-probe-build.json').read_text())
        if helper_build['inputs']!=source_identity():raise ValueError('rebuild Helper source drift')
        for name,record in helper_build['artifacts'].items():
            if identity(SOURCE/'out'/name)!=record:raise ValueError('Helper artifact drift')
        native_build=json.loads((SOURCE/'out/m22-native-gpu-build.json').read_text())
        exe=SOURCE/'out/native-dlssg-gpu-probe'
        if native_build['inputs']!=sources() or native_build['artifact']!=identity(exe):raise ValueError('rebuild Native GPU fixture')
        safety=runner.daily_preflight(lock,binding)
        run.mkdir(mode=0o700);ipc=Path(tempfile.mkdtemp(prefix='bb-dlssg-gpu-',dir='/tmp'))
        config={'schema':'dlssg-native-gpu-fixture-v1','session':a.session,'run':str(run),'ipc':str(ipc),
                'ge_files':runtime['ge_files'],'ngx_dir':runtime['ngx_dir'],'prefix':str(prefix),
                'executable':str(SOURCE/'out/wine-dlssg-probe.exe'),'adapter':str(SOURCE/'out/nvngx.dll_dlssg.dll'),
                'model':str(Path(a.model).resolve(strict=True)),'cuda':str(SOURCE/'out/nvcuda.dll'),
                'game_inputs':str(path),'frames':a.frames}
        config_path=run/'gpu-config.json';runner.atomic(config_path,config)
        artifacts={k:identity(config[k]) for k in ['executable','adapter','model','cuda','game_inputs']}
        artifacts['core']=identity('/usr/lib/nvidia/wine/_nvngx.dll');artifacts['Native']=identity(exe)
        env={k:v for k,v in os.environ.items() if k in {'PATH','HOME','USER','LOGNAME','LANG','DISPLAY','XAUTHORITY','WAYLAND_DISPLAY','XDG_RUNTIME_DIR','DBUS_SESSION_BUS_ADDRESS','XDG_SESSION_TYPE'}}
        env.update(BB_FG_PROFILE='1',NVPRESENT_ENABLE_SMOOTH_MOTION='0',VK_LOADER_LAYERS_DISABLE='~implicit~')
        command=[str(exe),str(config_path)]
        runner.atomic(run/'manifest.json',{'schema':'dlssg-gpu-fixture-run-v1','session':a.session,'command':command,
            'host_safety':safety,'environment':env,'artifacts':artifacts,'native_build':native_build,'helper_build':helper_build,
            'scope':'finite Native/Wine shared GPU images; no game or Native Present','config':config})
        guard=runner.daily_session_guard.SessionGuard();receipt=guard.begin(runner.ROOT,a.session)
        with (run/'native-probe.log').open('w') as log:
            child=subprocess.Popen(command,env=env,cwd=SOURCE,stdout=log,stderr=subprocess.STDOUT,start_new_session=True)
            print(run,flush=True);code=child.wait()
        native_text=(run/'native-probe.log').read_text(errors='replace');helper_text=(run/'dlssg-helper.log').read_text(errors='replace') if (run/'dlssg-helper.log').exists() else ''
        residual=runner.holders()
        for proc in Path('/proc').iterdir():
            if not proc.name.isdigit() or int(proc.name)==os.getpid():continue
            try:
                maps=(proc/'maps').read_text()
                if any(n in maps for n in ['wine-dlssg-probe.exe','nvngx_dlssg.dll','native-dlssg-gpu-probe']):residual.append(runner.live(int(proc.name)))
            except (OSError,ValueError,IndexError):pass
        normal=code==0 and not residual and 'HOLD' not in native_text+helper_text
        report=None;error=None
        try:
            report=json.loads((run/'gpu-probe-result.json').read_text());terminal=report['terminal']
            expected={'schema':'dlssg-gpu-mailbox-v1','session':a.session,'sequence':a.frames,
                      'shared_images_owned':0,'export_raw_fd_owned':0,'native_gpu_readers':0}
            if any(terminal.get(k)!=v for k,v in expected.items()):raise ValueError('exact GPU terminal identity/counts')
            if not all(terminal.get(k) is True for k in ['actual_GPU_complete','feature_released','device_destroyed','native_imports_released']):raise ValueError('Helper actual GPU retirement')
            if report.get('wait_status')!=0 or not all(report.get(k) is True for k in ['Native_shared_GPU_complete','Native_imports_destroyed','Native_device_destroyed']):raise ValueError('Native actual GPU/import/device retirement')
            if len(report['responses'])!=a.frames:raise ValueError('all finite responses required')
            proof='DLSSG_GPU_PROBE_RETIRED Native_GPU_complete=1 imports_destroyed=1 device_destroyed=1 helper_reaped=1'
            if native_text.splitlines().count(proof)!=1:raise ValueError('unique Native teardown proof')
            if not json.loads((run/'dlssg-gpu-import.json').read_text())['raw_payloads_absent']:raise ValueError('actual raw FD audit')
        except (OSError,ValueError,KeyError) as e:normal=False;error=str(e)
        result={'status':'PASS_NORMAL_TEARDOWN' if normal else 'HOLD','returncode':code,'native_lastuse_proof':normal,
                'residuals':residual,'watchdog_expired':False,'supervisor_interrupted':False,'audit_error':error,
                'scope':'Native and Wine GPU mailbox fixture; not live Present proof','requests':a.frames,
                'eligible_outputs':sum(r['eligible'] for r in report['responses']) if report else 0}
        if normal:
            try:
                terminal_dir=run/'ipc-terminal';terminal_dir.mkdir()
                for entry in ipc.iterdir():
                    if entry.is_symlink() or not entry.is_file() or entry.suffix!='.json' or entry.stat().st_size>131072:raise ValueError('unexpected GPU IPC retained')
                    shutil.copyfile(entry,terminal_dir/entry.name)
                shutil.rmtree(ipc);result['control_cleanup']='RETIRED'
            except (OSError,ValueError) as cleanup:
                result['control_cleanup']={'status':'RETAINED','reason':str(cleanup),'directory':str(ipc)}
        runner.atomic(run/'result.json',result)
        if normal:guard.retire(receipt,result)
        else:guard.hold(receipt)
        print(json.dumps(result));return 0 if normal else 3


if __name__=='__main__':raise SystemExit(main())
