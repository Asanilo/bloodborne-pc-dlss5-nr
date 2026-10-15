#!/usr/bin/env python3
"""Isolated NR development runner. No legacy approval, ledger or launch quota imports."""
import argparse,datetime,fcntl,hashlib,json,os,re,shutil,subprocess,time
from pathlib import Path
ROOT=Path('/home/arinp/Code/nrbridge-dev');WT=ROOT/'native';OLD=Path('/home/arinp/Code/nrbridge-design/game-poc-20261011')
GE=Path('/home/arinp/.local/share/Steam/compatibilitytools.d/GE-Proton11-7-x86_64')
def atomic(p,value):
 t=p.with_suffix('.tmp');t.write_text(json.dumps(value,indent=2)+'\n');t.replace(p)
def sha(p):return hashlib.sha256(p.read_bytes()).hexdigest()
def live(pid):
 p=Path('/proc')/str(pid)
 return {'pid':pid,'start_ticks':int((p/'stat').read_text().rsplit(')',1)[1].split()[19]),'exe':str((p/'exe').resolve())}
def holders():
 found=[]
 for p in Path('/proc').iterdir():
  if not p.name.isdigit() or int(p.name)==os.getpid():continue
  try:
   exe=(p/'exe').resolve().name;cmd=(p/'cmdline').read_bytes().replace(b'\0',b' ').decode(errors='replace')
   if exe=='bb-probe' or re.search(r'wine_(?:async|game|joint)',cmd) and ('wine' in exe or exe.endswith('.exe')):found.append(live(int(p.name)))
  except (OSError,ValueError,IndexError):pass
 return found
def preflight():
 h=holders()
 if h:raise ValueError('live Native/Helper GPU holders: '+json.dumps(h))
 for result in (ROOT/'runs').glob('*/result.json'):
  if json.loads(result.read_text()).get('status')=='HOLD':raise ValueError('unresolved new-context HOLD '+str(result))
 for pin in json.loads((ROOT/'inputs/runtime-pins.json').read_text())['pins']:
  if sha(Path(pin['path']))!=pin['sha256']:raise ValueError('runtime pin drift '+pin['path'])
 safety=json.loads((ROOT/'inputs/host-safety.json').read_text())
 boot=Path('/proc/sys/kernel/random/boot_id').read_text().strip()
 if safety.get('boot_id')!=boot or safety.get('unresolved_driver_hang') is not False or not safety.get('evidence'):
  raise ValueError('coordinator must inspect current boot driver-hang evidence before GPU launch')
 return safety
def checked_run(session):
 if not re.fullmatch('[A-Za-z0-9][A-Za-z0-9-]{0,62}',session):raise ValueError('safe session required')
 run=ROOT/'runs'/session
 if run.resolve()!=run or run.is_symlink():raise ValueError('private real run path required')
 return run
def current_game(run):
 m=json.loads((run/'manifest.json').read_text());matches=[]
 for p in Path('/proc').iterdir():
  if not p.name.isdigit():continue
  try:
   if (p/'exe').resolve()==Path(m['native_executable']) and str(run/'profile').encode() in (p/'environ').read_bytes():matches.append(live(int(p.name)))
  except (OSError,ValueError):pass
 if len(matches)!=1:raise ValueError('exact private live game identity required')
 identity=matches[0];record=run/'game-identity.json'
 if record.exists() and json.loads(record.read_text())!=identity:raise ValueError('PID/starttime mismatch')
 atomic(record,identity);return identity

def control(args):
 run=checked_run(args.session);game=current_game(run)
 if args.command=='scene-ready':atomic(run/'scene-ready.json',{'session':args.session,'game':game,'basis':'explicit coordinator/player scene observation','monotonic_ns':time.monotonic_ns()});return
 if args.command in ('start-helper','arm'):
  observed=json.loads((run/'scene-ready.json').read_text())
  if observed['game']!=game:raise ValueError('same live scene identity required')
  name='native-helper-start' if args.command=='start-helper' else 'native-scene-arm'
  if args.command=='arm':
   ready=json.loads((run/('async-fd-retired-'+args.session+'-g0.json')).read_text())
   if not(ready.get('session')==args.session and ready.get('status')=='FD_RETIRED' and ready.get('completed_GPU') is True and ready.get('export_raw_fd_owned')==0):raise ValueError('actual GPU import and FD retirement readiness required')
  with (run/name).open('x') as f:f.write(args.session+'\n')
 if args.command=='stop':
  windows=json.loads(subprocess.check_output(['niri','msg','--json','windows'],text=True,timeout=5))
  own=[w for w in windows if w.get('app_id')=='bb-probe' and w.get('title')=='Bloodborne™']
  if len(own)!=1:raise ValueError('unique private game window required')
  prop=subprocess.check_output(['xprop','-name',own[0]['title'],'_NET_WM_PID','WM_CLASS'],text=True,timeout=5)
  if not re.search(r'_NET_WM_PID\(CARDINAL\) = '+str(game['pid'])+r'\b',prop):raise ValueError('window PID mismatch')
  subprocess.run(['niri','msg','action','close-window','--id',str(own[0]['id'])],check=True,timeout=5)
  atomic(run/'stop-request.json',{'game':game,'monotonic_ns':time.monotonic_ns(),'scope':'normal window close request; not retirement proof'})

def launch(args):
 run=checked_run(args.session);build=ROOT/'build/native';assets=OLD/'playtest/assets/CUSA03173';seed=OLD/'integration/private-NRTest-seed';helper=ROOT/'build/helper/wine_async_presr_play.exe'
 with open('/tmp/nrbridge-poc-gpu.lock','a') as lock:
  fcntl.flock(lock,fcntl.LOCK_EX|fcntl.LOCK_NB);safety=preflight()
  for p in (build/'bb-probe',build/'gpu/libbbgpu.so',build/'libbbport_dlss.so',assets/'eboot.bin',helper):
   if not p.is_file():raise ValueError('missing input '+str(p))
  run.mkdir(mode=0o700);profile=run/'profile';profile.mkdir();shutil.copytree(seed/'savedata',profile/'user/savedata')
  for item in json.loads((seed/'manifest.json').read_text())['files']:
   p=profile/'user/savedata'/item['relative']
   if p.is_symlink() or sha(p)!=item['sha256']:raise ValueError('private seed SHA mismatch')
  for name in ('logs','wine-prefix','cache','config','data','state'):(profile/name).mkdir()
  (profile/'bbport.ini').write_text('upscaler=dlss\npreset=1\noutput_res=1920x1080\njitter=1\nobject_motion=1\nsharpen=0\nshow_fps=1\n');(run/'pad.txt').write_text('')
  env={k:v for k,v in os.environ.items() if not k.startswith(('BB_','DLSSNR_','VK_','VKD3D_','DXVK_','WINE','PROTON','STEAM_COMPAT')) and k not in ('LD_PRELOAD','LD_AUDIT')}
  env.update(BB_GAME_DIR=str(assets),BB_DATA_DIR=str(profile),BB_CONFIG=str(profile/'bbport.ini'),BB_USER_DIR=str(profile/'user'),BB_GPU_USER_DIR=str(profile/'user'),BB_PREBUILT='1',BB_PROBE=str(build/'bb-probe'),BB_STARTUP_CHECK='0',BB_MODS_ENABLED='0',BB_UPSCALER='dlss',BB_UPSCALE_PRESET='1',BB_DLSS='1',BB_JITTER='1',BB_FPS='uncap',BB_FPS_LIMIT='90',BB_FRAME_STATS='1',BB_FRAME_LOG=str(profile/'logs/frames.csv'),BB_FULLSCREEN='0',BB_SKIP_NETWORK_CHOICE='1',BB_LANGUAGE='1',BB_PC_MODEL='0',BB_SAVE_LOG='1',BB_IME_TEXT='NRTest',BB_PAD_FILE=str(run/'pad.txt'),BB_DUMP_DIR=str(run/'dump'),BB_PRESENT_DUMP_TRIGGER=str(run/'present-dump-trigger'),BB_PRESENT_DUMP_COUNT='0',LD_LIBRARY_PATH='/home/arinp/Code/bloodborne_pc/out/deps/root/usr/lib',DLSSNR_DISABLE='1',DLSSNR_AUTO_DOWNLOAD='0',VK_LOADER_LAYERS_DISABLE='~implicit~',MANGOHUD='0',DISABLE_MANGOHUD='1')
  for name in ('config','cache','data','state'):env['XDG_'+name.upper()+'_HOME']=str(profile/name)
  nr={'BB_NR_NATIVE_ASYNC':'0','BB_NR_NATIVE_ASYNC_PRE_SR':'1','BB_NR_NATIVE_ASYNC_SCENE_FIRST':'1','BB_NR_NATIVE_ASYNC_PRE_SR_PLAY':'1','BB_NR_NATIVE_ASYNC_PRE_SR_SKY':args.sky,'BB_NR_NATIVE_ASYNC_PRE_SR_SR_HISTORY':'game_only','BB_NR_NATIVE_ASYNC_PRE_SR_SR_INPUT':'switching','BB_NR_NATIVE_ASYNC_SESSION':args.session,'BB_NR_NATIVE_ASYNC_MODE':args.mode,'BB_NR_NATIVE_ASYNC_RUN_DIR':str(run),'BB_NR_NATIVE_ASYNC_SCENE_ARM':str(run/'native-scene-arm'),'BB_NR_NATIVE_ASYNC_SOURCE_LIMIT':str(args.source_limit),'BB_NR_NATIVE_ORIGIN_RUN_DIR':str(run),'BB_NR_NATIVE_ORIGIN_HELPER':str(helper),'BB_NR_NATIVE_ORIGIN_WINE':str(GE/'files/lib/wine/x86_64-unix/wine'),'BB_NR_NATIVE_ORIGIN_PRELOADER':str(GE/'files/lib/wine/x86_64-unix/wine-preloader'),'BB_NR_NATIVE_ORIGIN_PREFIX':str(profile/'wine-prefix'),'BB_NR_NATIVE_ORIGIN_WINEDEBUG':'-all,warn+vulkan,err+vulkan','BB_NR_NATIVE_ORIGIN_MODE':args.mode}
  env.update(nr);command=['bash',str(WT/'run.sh')]
  atomic(run/'manifest.json',{'schema':'nr-dev-session-v1','session':args.session,'native_executable':str(build/'bb-probe'),'artifacts':{str(p):sha(p) for p in (build/'bb-probe',build/'gpu/libbbgpu.so',helper)},'environment':env,'command':command,'host_safety':safety,'authorization_basis':'user full isolated persistent visual history development stage 2026-10-15; no legacy launch quota','mode':args.mode,'sky':args.sky,'watchdog_seconds':args.seconds})
  with (run/'game.log').open('w') as log:
   process=subprocess.Popen(command,cwd=WT,env=env,stdout=log,stderr=subprocess.STDOUT,start_new_session=True)
   atomic(run/'runner.json',{'runner':live(os.getpid()),'launcher':live(process.pid)});deadline=time.monotonic()+args.seconds;held=False;sample_at=0
   print(str(run),flush=True)
   while process.poll() is None:
    if time.monotonic()>=sample_at:
     sample_at=time.monotonic()+1
     try:
      sample=subprocess.check_output(['nvidia-smi','--query-compute-apps=pid,used_gpu_memory','--format=csv,noheader,nounits'],text=True,timeout=3)
      with (run/'gpu-memory.jsonl').open('a') as memory:memory.write(json.dumps({'monotonic_ns':time.monotonic_ns(),'pid_used_MiB_csv':sample})+'\n')
     except (OSError,subprocess.SubprocessError):pass
    if time.monotonic()>deadline and not held:
     held=True;atomic(run/'result.json',{'status':'HOLD','reason':'watchdog expired; normal close requested; no kill or GPU completion claim'})
     try:control(argparse.Namespace(command='stop',session=args.session))
     except Exception as e:atomic(run/'watchdog-close-error.json',{'error':str(e)})
    time.sleep(.25)
   text=(run/'game.log').read_text(errors='replace');residual=holders()
   proof='NATIVE_ASYNC_EXIT_PROOF actual_snapshot_GPU_lastuse_complete=1 managed_owner_joined=1' in text
   bad=bool(re.search(r'NATIVE_(?:ASYNC_HOLD|ORIGIN_HOLD|ORIGIN_QUARANTINE|ASYNC_EXIT_HELD)|functional=FAIL',text))
   normal=process.returncode==0 and proof and not bad and not residual
   if (run/'async-native-launch.json').exists():
    peer=json.loads((run/'async-native-helper-exit.json').read_text()) if (run/'async-native-helper-exit.json').exists() else {}
    normal=normal and peer.get('normal_exit') is True and peer.get('wait_status')==0
   atomic(run/'result.json',{'status':'PASS_NORMAL_TEARDOWN' if normal and not held else 'HOLD','returncode':process.returncode,'native_lastuse_proof':proof,'residuals':residual,'watchdog_expired':held,'quality':'NOT_ACCEPTED','performance':'separate actual Present analysis required'})
   from metrics import measure
   atomic(run/'present-metrics.json',measure(run))
   return 0 if normal and not held else 3

def main():
 p=argparse.ArgumentParser(description=__doc__);p.add_argument('command',choices=['preflight','start','scene-ready','start-helper','arm','stop']);p.add_argument('session',nargs='?');p.add_argument('--mode',choices=['resident','skipEvaluate'],default='skipEvaluate');p.add_argument('--sky',choices=['off','persistent'],default='off');p.add_argument('--seconds',type=int,default=600);p.add_argument('--source-limit',type=int,default=36000);a=p.parse_args()
 if a.command=='preflight':print(json.dumps(preflight(),indent=2));return 0
 if not a.session:p.error('session required')
 if not 1<=a.seconds<=3600 or not 1<=a.source_limit<=216000:p.error('finite current-stage resource budget required')
 return launch(a) if a.command=='start' else control(a)
if __name__=='__main__':raise SystemExit(main())
