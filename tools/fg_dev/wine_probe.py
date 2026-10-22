#!/usr/bin/env python3
"""Finite Wine FG entry PoC under the real project admission/retirement guard."""
import argparse,fcntl,json,os,subprocess,sys,time,tempfile,shutil
from pathlib import Path
SOURCE=Path(__file__).resolve().parents[2]
sys.path.insert(0,str(SOURCE/'tools/nr_dev'))
import runner
import peer_retirement
from build_wine_probe import identity, source_identity
from wine_probe_result import evaluate_status

def main():
 p=argparse.ArgumentParser();p.add_argument('session');p.add_argument('--local-inputs',required=True);p.add_argument('--snippet',action='store_true');p.add_argument('--cuda-interop',action='store_true');p.add_argument('--evaluate-fixture',action='store_true');p.add_argument('--game-inputs');p.add_argument('--reuse-inputs',action='store_true');p.add_argument('--stream-fixture',action='store_true');p.add_argument('--profile',action='store_true');p.add_argument('--transport',choices=['files-v1','mapped-v1'],default='files-v1');p.add_argument('--model',default='/usr/lib/nvidia/wine/nvngx_dlssg.dll');p.add_argument('--prefix-session',default='m2-sm-1080q-r1');a=p.parse_args()
 r=runner.checked_run(a.session);assert not r.exists()
 binding=runner.runtime_inputs.load(a.local_inputs,runner.ROOT);config=binding['config'];ge=Path(config['ge_files'])
 previous=runner.checked_run(a.prefix_session);old=json.loads((previous/'result.json').read_text());assert old['status']=='PASS_NORMAL_TEARDOWN';peer_retirement.audit_session(previous)
 prefix=previous/'profile/wine-prefix';assert prefix.resolve()==prefix and prefix.is_dir()
 exe=SOURCE/'out/wine-dlssg-probe.exe';assert exe.is_file()
 with open('/tmp/nrbridge-poc-gpu.lock','a') as lock:
  fcntl.flock(lock,fcntl.LOCK_EX|fcntl.LOCK_NB);safety=runner.daily_preflight(lock,binding);r.mkdir(mode=0o700)
  base={'PATH','HOME','USER','LOGNAME','LANG','DISPLAY','XAUTHORITY','WAYLAND_DISPLAY','XDG_RUNTIME_DIR','DBUS_SESSION_BUS_ADDRESS','XDG_SESSION_TYPE'};env={k:v for k,v in os.environ.items() if k in base}
  env.update(WINEPREFIX=str(prefix),WINELOADERNOEXEC='1',WINEDLLOVERRIDES='mscoree,mshtml=;nvapi64=n;dxgi=n',WINEDLLPATH=str(ge/'lib/vkd3d')+':'+str(ge/'lib/wine'),DXVK_ENABLE_NVAPI='1',NVIDIA_WINE_DLL_DIR=config['ngx_dir'],WINEDEBUG='-all,warn+vulkan,err+vulkan',VK_LOADER_LAYERS_DISABLE='~implicit~',NVPRESENT_ENABLE_SMOOTH_MOTION='0',DLSSNR_DISABLE='1',DLSSNR_AUTO_DOWNLOAD='0')
  ipc=Path(tempfile.mkdtemp(prefix='bb-dlssg-',dir='/tmp')) if a.stream_fixture else r
  command=[str(ge/'lib/wine/x86_64-unix/wine-preloader'),str(ge/'lib/wine/x86_64-unix/wine'),str(exe),'Z:'+str(ipc).replace('/','\\'),str(ge)]
  if a.profile:env['BB_FG_PROFILE']='1'
  if a.cuda_interop:
   assert a.snippet
   cuda=SOURCE/'out/nvcuda.dll';assert cuda.is_file()
   env['FG_POC_NVCUDA']=str(cuda);env['WINEDLLOVERRIDES']+=';nvcuda=n,b'
  if a.evaluate_fixture:
   assert a.snippet and a.cuda_interop
   env['FG_POC_EVALUATE']='1'
  if a.reuse_inputs:
   assert a.evaluate_fixture
   env['FG_POC_REUSE_INPUTS']='1'
  if a.stream_fixture:
   assert a.game_inputs and a.evaluate_fixture
   env['FG_POC_STREAM']='1'
   if a.transport=='mapped-v1':
    import cpu_slot
    with cpu_slot.mapping(ipc,create=True):pass
   runner.atomic(ipc/'stream-config.json',{'schema':'dlssg-cpu-stream-v1','session':a.session,'transport':a.transport})
  artifacts={'executable':identity(exe),'core':identity('/usr/lib/nvidia/wine/_nvngx.dll'),'wine':identity(ge/'lib/wine/x86_64-unix/wine')}
  if a.game_inputs:
   assert a.evaluate_fixture
   from prepare_game_inputs import prepare
   path=Path(a.game_inputs).resolve(strict=True);game_record=json.loads(path.read_text())
   assert game_record==prepare(game_record['capture_directory']),'captured inputs changed'
   artifacts['game_inputs']=identity(path);env['FG_POC_INPUT_MANIFEST']=str(path)
  if a.snippet:
   model=Path(a.model).resolve(strict=True);env['FG_POC_MODEL']=str(model)
   command.append(str(SOURCE/'out/nvngx.dll_dlssg.dll'))
   artifacts.update(adapter=identity(SOURCE/'out/nvngx.dll_dlssg.dll'),model=identity(model))
  if a.cuda_interop:artifacts['cuda_interop']=identity(cuda)
  build=json.loads((SOURCE/'out/m2-wine-probe-build.json').read_text())
  assert build['schema']=='dlssg-wine-probe-build-v1' and build['inputs']==source_identity(),'rebuild changed probe sources'
  for name,artifact_record in build['artifacts'].items():assert identity(SOURCE/'out'/name)==artifact_record,'probe artifact changed after build'
  runner.atomic(r/'manifest.json',{'schema':'fg-wine-entry-poc-v2','session':a.session,'scope':'standalone no game or Native readers; no generated-frame success claim','command':command,'environment':env,'artifacts':artifacts,'build':build,'host_safety':safety,'reused_retired_prefix':a.prefix_session,'cpu_transport_directory':str(ipc)})
  guard=runner.daily_session_guard.SessionGuard();receipt=guard.begin(runner.ROOT,a.session)
  with (r/'probe.log').open('w') as log:
   child=subprocess.Popen(command,env=env,cwd=SOURCE,stdout=log,stderr=subprocess.STDOUT,start_new_session=True)
   print(r,flush=True)
   responses=None;stream_terminal=None
   if a.stream_fixture:
    import stream_fixture
    try:responses=stream_fixture.drive(ipc,game_record,a.session,child,a.transport)
    except BaseException as error:
     guard.hold(receipt)
     runner.atomic(r/'control-hold.json',{'error':str(error),'forced_GPU_retirement':False,'child_pid':child.pid})
     raise
   rc=child.wait() # no forced timeout/kill masquerading as retirement; unknown completion stays pending
  text=(r/'probe.log').read_text(errors='replace');proof='FG_POC_RETIRED actual_GPU_complete=1 feature_released=1 core_shutdown=1 device_destroyed=1 native_readers=0'
  residual=runner.holders()
  for q in Path('/proc').iterdir():
   if not q.name.isdigit() or int(q.name)==os.getpid():continue
   try:
    maps=(q/'maps').read_text()
    if 'wine-dlssg-probe.exe' in maps or 'nvngx_dlssg.dll' in maps:residual.append(runner.live(int(q.name)))
   except (OSError,ValueError,IndexError):pass
  normal=rc==0 and text.splitlines().count(proof)==1 and not residual and 'FG_POC_HOLD' not in text
  if a.stream_fixture:
   try:stream_terminal=stream_fixture.terminal(ipc,a.session,responses)
   except (ValueError,OSError,KeyError):normal=False
  result={'status':'PASS_NORMAL_TEARDOWN' if normal else 'HOLD','returncode':rc,'residuals':residual,'watchdog_expired':False,'supervisor_interrupted':False,'native_lastuse_proof':normal,'proof_scope':'standalone actual device teardown, zero Native readers; not NR source-cache evidence','evaluate':evaluate_status(text),'functional':'unavailable' if 'FG_POC_UNAVAILABLE' in text else 'see actual stage returns'}
  if a.stream_fixture:
   result.update(stream_responses=responses,stream_terminal=stream_terminal)
   result['evaluate']={'status':'OUTPUT_REQUIRES_PIXEL_VALIDATION' if any(x['eligible'] for x in responses) else 'NO_ELIGIBLE_OUTPUT','eligible_outputs':sum(x['eligible'] for x in responses),'actual_gpu_complete_requests':len(responses),'native_presents':0,'scope':'captured Bloodborne CPU stream fixture; no live Native output'}
  if a.stream_fixture and normal:
   retained=[]
   for path in [*ipc.glob('*.json'),*ipc.glob('stream-*-interp-*.raw')]:
    shutil.copyfile(path,r/path.name);retained.append(identity(r/path.name))
   shutil.rmtree(ipc) # Only after consumed CPU slot, actual private device teardown, no readers.
   result['cpu_transport_cleanup']={'status':'RETIRED','directory':str(ipc),'retained':retained}
  runner.atomic(r/'result.json',result)
  if normal:guard.retire(receipt,result)
  else:guard.hold(receipt)
  print(json.dumps(result));return 0 if normal else 3

if __name__=='__main__':raise SystemExit(main())
