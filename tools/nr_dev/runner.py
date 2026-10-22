#!/usr/bin/env python3
"""Isolated NR development runner. No legacy approval, ledger or launch quota imports."""
import argparse,datetime,fcntl,hashlib,json,os,re,shutil,subprocess,time,signal,math,uuid,importlib.util,stat,contextlib
from pathlib import Path
def workspace_root():
 value=os.environ.get('NRBRIDGE_HOME',str(Path.home()/'Code/nrbridge-dev'));path=Path(value)
 if not value or any(c in value for c in ('\x00','\n',':')) or not path.is_absolute() or str(path)!=value or path.resolve()!=path or path in (Path('/'),Path.home()):raise ValueError('NRBRIDGE_HOME requires a dedicated canonical absolute directory')
 return path
ROOT=workspace_root();WT=Path(__file__).resolve().parents[2];ASSETS=ROOT/'assets/CUSA03173';SEED=ROOT/'seed/private-NRTest-seed'
_sm_spec=importlib.util.spec_from_file_location('nr_dev_smooth_motion',Path(__file__).with_name('smooth_motion.py'))
smooth_motion=importlib.util.module_from_spec(_sm_spec);_sm_spec.loader.exec_module(smooth_motion)
_dlssg_spec=importlib.util.spec_from_file_location('nr_dev_dlssg_backend',Path(__file__).with_name('dlssg_backend.py'))
dlssg_backend=importlib.util.module_from_spec(_dlssg_spec);_dlssg_spec.loader.exec_module(dlssg_backend)
GE=Path.home()/'.local/share/Steam/compatibilitytools.d/GE-Proton11-7-x86_64'
_runtime_spec=importlib.util.spec_from_file_location('nr_dev_runtime_inputs',Path(__file__).with_name('runtime_inputs.py'))
runtime_inputs=importlib.util.module_from_spec(_runtime_spec);_runtime_spec.loader.exec_module(runtime_inputs)
_online_spec=importlib.util.spec_from_file_location('nr_dev_daily_online',Path(__file__).with_name('daily_online.py'))
daily_online=importlib.util.module_from_spec(_online_spec);_online_spec.loader.exec_module(daily_online)
_guard_spec=importlib.util.spec_from_file_location('nr_dev_daily_session_guard',Path(__file__).with_name('daily_session_guard.py'))
daily_session_guard=importlib.util.module_from_spec(_guard_spec);_guard_spec.loader.exec_module(daily_session_guard)
_player_spec=importlib.util.spec_from_file_location('nr_dev_player_settings',Path(__file__).with_name('player_settings.py'))
player_settings=importlib.util.module_from_spec(_player_spec);_player_spec.loader.exec_module(player_settings)
_ui_spec=importlib.util.spec_from_file_location('nr_dev_ui_start',Path(__file__).with_name('ui_start.py'))
ui_start=importlib.util.module_from_spec(_ui_spec);_ui_spec.loader.exec_module(ui_start)
def atomic(p,value):
 t=p.with_suffix('.tmp');t.write_text(json.dumps(value,indent=2)+'\n');t.replace(p)
def sha(p):return hashlib.sha256(p.read_bytes()).hexdigest()
def retire_generated_inputs(run,result):
 """Discard only reproducible guest images after actual normal retirement, not saves/evidence."""
 removed=[]
 if not (result.get('status')=='PASS_NORMAL_TEARDOWN' and type(result.get('returncode')) is int
         and result['returncode']==0 and result.get('native_lastuse_proof') is True
         and result.get('residuals')==[] and result.get('watchdog_expired') is False
         and result.get('supervisor_interrupted') is False):
  return {'status':'SKIPPED_NOT_NORMAL_TEARDOWN','removed':removed}
 try:
  if run.parent!=ROOT/'runs' or run.resolve()!=run:raise ValueError('noncanonical private run')
  profile=run/'profile';out=profile/'out'
  if profile.is_symlink() or out.is_symlink():raise ValueError('generated directory symlink')
  if not out.exists():return {'status':'NOTHING_TO_RETIRE','removed':removed}
  pending=[]
  for name in ('boot.bin','eboot.elf','entry.bin','boot-libc.bin','boot-linked.bin','content.bin','patches.bin'):
   path=out/name
   try:info=path.lstat()
   except FileNotFoundError:continue
   if not stat.S_ISREG(info.st_mode) or info.st_nlink!=1:raise ValueError('nonprivate generated input: '+name)
   with path.open('rb') as f:digest=hashlib.file_digest(f,'sha256').hexdigest()
   pending.append((path,info,{'name':name,'bytes':info.st_size,'sha256':digest}))
  # Validate the entire allowlist before deleting anything. No recursive directory removal.
  for path,info,_ in pending:
   now=path.lstat()
   if (now.st_dev,now.st_ino,now.st_size,now.st_mtime_ns,now.st_nlink)!=(info.st_dev,info.st_ino,info.st_size,info.st_mtime_ns,1):
    raise ValueError('generated input changed during retirement')
  for path,_,record in pending:path.unlink();removed.append(record)
  return {'status':'RETIRED','removed':removed,'bytes':sum(r['bytes'] for r in removed),
          'retained':'generation JSON, logs, saves, diagnostics and Native/Helper builds'}
 except (OSError,ValueError) as error:
  # Cleanup failure is separate from already-proven GPU retirement.
  return {'status':'PARTIAL' if removed else 'RETAINED','removed':removed,'error':str(error)}
def live(pid):
 p=Path('/proc')/str(pid)
 return {'pid':pid,'start_ticks':int((p/'stat').read_text().rsplit(')',1)[1].split()[19]),'exe':str((p/'exe').resolve())}
def gpu_holders():
 found=[]
 for p in Path('/proc').iterdir():
  if not p.name.isdigit() or int(p.name)==os.getpid():continue
  try:
   devices=[]
   for fd in (p/'fd').iterdir():
    try:
     target=os.readlink(fd)
     if target.startswith(('/dev/nvidia','/dev/dri/')):devices.append(target)
    except OSError:pass
   if not devices:continue
   entry=live(int(p.name));entry['gpu_devices']=sorted(set(devices));found.append(entry)
  except (OSError,ValueError,IndexError):pass
 return found
def holders():
 found=[];gpu={x['pid']:x for x in gpu_holders()}
 for p in Path('/proc').iterdir():
  if not p.name.isdigit() or int(p.name)==os.getpid():continue
  try:
   exe=(p/'exe').resolve().name;cmd=(p/'cmdline').read_bytes().replace(b'\0',b' ').decode(errors='replace')
   try:maps=(p/'maps').read_text()
   except OSError:maps=''
   project=('/nrbridge-' in cmd or 'wine_async' in cmd or 'wine_joint' in cmd or 'wine_game' in cmd)
   mapped=('libbbgpu.so' in maps or 'nvngx.dll_dlssnr.dll' in maps)
   wine=exe.startswith(('wine','wineserver')) or exe.endswith('.exe')
   if exe=='bb-probe' or mapped or (project and (int(p.name) in gpu or wine)):
    entry=live(int(p.name));entry['gpu_devices']=gpu.get(int(p.name),{}).get('gpu_devices',[]);found.append(entry)
  except (OSError,ValueError,IndexError):pass
 return found
def audit_pre_nr_absence(run, provenance):
 """Cold proof of no NR entry; explicitly NOT GPU retirement or save continuity."""
 manifest=json.loads((run/'manifest.json').read_text());result=json.loads((run/'result.json').read_text())
 env=manifest['environment'];session=run.name
 if manifest.get('session')!=session or manifest.get('schema')!='nr-dev-session-v1' or env.get('BB_NR_NATIVE_ASYNC_SCENE_FIRST')!='1' or env.get('BB_NR_NATIVE_ASYNC')!='0' or env.get('BB_NR_NATIVE_ASYNC_PRE_SR')!='1' or env.get('BB_NR_NATIVE_ASYNC_RUN_DIR')!=str(run) or env.get('BB_NR_NATIVE_ASYNC_SCENE_ARM')!=str(run/'native-scene-arm'):raise ValueError('not exact isolated scene-first context')
 if result.get('status')!='HOLD' or result.get('returncode')!=23 or result.get('watchdog_expired') is not False or result.get('supervisor_interrupted') is not False or result.get('received_signals')!=0 or result.get('residuals')!=[]:raise ValueError('not unambiguous assertion exit23')
 allowed={'manifest.json','result.json','runner.json','game-identity.json','game.log','gpu-memory.jsonl','native-loaded-libraries.json','pad.txt','present-metrics.json','retirement-audit.json','profile','hold-resolution.json','build-provenance','scene.png','scene-ready.png'}
 if any(p.name not in allowed for p in run.iterdir()):raise ValueError('unexpected control/protocol evidence; no pre-NR exception')
 text=(run/'game.log').read_text(errors='strict')
 expected='NATIVE_ASYNC_PRE_SR_FRONTEND_READY session='+session+' scene_arm=REQUIRED availability_only=1 no_GPU_proof=1'
 nr=[line for line in text.splitlines() if re.search(r'NATIVE_ASYNC_|NATIVE_ORIGIN_|HELPER_|PERSISTENT_|ASYNC_CONFIG|INPUT_PUBLISHED',line)]
 if nr!=[expected] or not text.rstrip().endswith('STOP: GPU library assertion failed (see GPU log above)') or not ('Unimplemented PM4' in text or re.search(r'^Unknown PM4 type 3 opcode 0x[0-9a-f]+ with count [0-9]+$',text,re.M)):raise ValueError('NR touched or assertion log incomplete')
 # Coordinator attests an observed build, not reconstructed or invented disk history.
 if provenance.get('artifacts')!=manifest.get('artifacts') or provenance.get('observed_clean_build') is not True or not provenance.get('observation'):raise ValueError('missing observed build provenance')
 head=provenance.get('source_commit','')
 if not re.fullmatch('[0-9a-f]{7,40}',head):raise ValueError('source commit required')
 def source(path):return subprocess.check_output(['git','show',head+':'+path],cwd=WT,text=True)
 assertion=source('gpu/shim/common/assert.cpp')
 if not re.search(r'std::fflush\(stdout\).*std::_Exit\(23\)',assertion,re.S):raise ValueError('assertion stdout flush not established')
 frontend=source('gpu/shadps4/video_core/renderer_vulkan/native_async_pre_sr_frontend.h')
 native=source('gpu/shadps4/video_core/renderer_vulkan/native_async_pre_sr.cpp')
 if 'exact_token(run+"/native-helper-start")' not in frontend or 'frontend&&frontend->HelperStartRequested()' not in native or 'if(!peer&&can_start)' not in native:raise ValueError('unrecognized scene-first gate')
 observers=[]
 for name in ('scene.png','scene-ready.png'):
  image=run/name
  if image.exists():
   if image.is_symlink() or not image.is_file() or image.stat().st_size>32*1024*1024 or image.read_bytes()[:8]!=b'\x89PNG\r\n\x1a\n':raise ValueError('unexpected scene observation')
   observers.append(name)
 identity_names=('game-identity.json','native-loaded-libraries.json')
 observed=[(run/name).is_file() for name in identity_names]
 evidence={name:sha(run/name) for name in ('manifest.json','result.json','runner.json','game.log',*observers)}
 if all(observed):return evidence|{name:sha(run/name) for name in identity_names}
 if any(observed):raise ValueError('partial live identity observation')
 # A player can hit a loading assertion before the first control request observes
 # PID/maps. Never invent either record; bind the captured clean build instead.
 build=manifest.get('native_build_provenance',{});files=build.get('files',{})
 if not re.fullmatch('[0-9a-f]{40}',head) or build.get('source_commit')!=head or set(files)!={'source-head.txt','source-diff.patch','artifacts.sha256'}:raise ValueError('unobserved game requires captured exact build provenance')
 for name,digest in files.items():
  path=run/'build-provenance'/name
  if path.is_symlink() or not path.is_file() or sha(path)!=digest:raise ValueError('captured build provenance changed')
 if (run/'build-provenance/source-head.txt').read_text().strip()!=head or (run/'build-provenance/source-diff.patch').read_bytes():raise ValueError('unobserved game requires clean source')
 recorded={}
 for line in (run/'build-provenance/artifacts.sha256').read_text().splitlines():
  match=re.fullmatch(r'([0-9a-f]{64})  (.+)',line)
  if not match or match[2] in recorded:raise ValueError('malformed captured artifact identity')
  recorded[match[2]]=match[1]
 native_artifacts={p:d for p,d in manifest.get('artifacts',{}).items() if Path(p).is_relative_to(Path(manifest['native_build']))}
 if len(recorded)!=4 or recorded!=native_artifacts:raise ValueError('unobserved game artifact mismatch')
 if any((run/name).exists() or (run/name).is_symlink() for name in identity_names):raise ValueError('unexpected identity file type')
 return evidence|{name:'ABSENT_BEFORE_CONTROL_OBSERVATION' for name in identity_names}|{'build-provenance/'+name:digest for name,digest in files.items()}


TERMINATED_PRE_NR_HEAD='b701356f2580886380d21a4a31003e672aa7815f'
def audit_terminated_pre_nr_freeze(run, provenance):
 """Exact frozen run exception; proves no NR ownership, NEVER guest GPU retirement."""
 if run!=ROOT/'runs/dev-transition-cold-01' or run.resolve()!=run:raise ValueError('not exact terminated pre-NR context')
 manifest=json.loads((run/'manifest.json').read_text());result=json.loads((run/'result.json').read_text());env=manifest.get('environment',{})
 if manifest.get('schema')!='nr-dev-session-v1' or manifest.get('session')!=run.name or env.get('BB_NR_NATIVE_ASYNC_SCENE_FIRST')!='1' or env.get('BB_NR_NATIVE_ASYNC')!='0' or env.get('BB_NR_NATIVE_ASYNC_PRE_SR')!='1' or env.get('BB_NR_NATIVE_ASYNC_RUN_DIR')!=str(run) or env.get('BB_NR_NATIVE_ASYNC_SCENE_ARM')!=str(run/'native-scene-arm'):raise ValueError('not exact scene-first environment')
 if result.get('status')!='HOLD' or result.get('returncode')!=137 or result.get('watchdog_expired') is not True or result.get('supervisor_interrupted') is not False or result.get('received_signals')!=0 or result.get('residuals')!=[] or result.get('native_lastuse_proof') is not False:raise ValueError('not exact abnormal terminated HOLD')
 allowed={'manifest.json','result.json','runner.json','game-identity.json','game.log','native-loaded-libraries.json','pad.txt','present-metrics.json','retirement-audit.json','profile','build-provenance','authorized-abnormal-termination.json','exit-thread-stacks-user.txt','exit-thread-stacks-privileged.txt','exit-thread-stacks.txt','frozen-registers.txt','read-frozen-registers.gdb','native-helper-start','planned-close.json','stop-request.json','scene-ready.json','scene-ready.png','scene.png','gpu-memory.jsonl','hold-resolution.json','post-exit-host-observation.json','post-termination-kernel.jsonl'}
 if any(p.name not in allowed or p.is_symlink() for p in run.iterdir()):raise ValueError('unexpected artifact, arm, config, frames or NR ownership evidence')
 build=manifest.get('native_build_provenance',{});files=build.get('files',{})
 if build.get('source_commit')!=TERMINATED_PRE_NR_HEAD or set(files)!={'source-head.txt','source-diff.patch','artifacts.sha256'}:raise ValueError('unreviewed source provenance')
 for name,digest in files.items():
  path=run/'build-provenance'/name
  if not path.is_file() or path.is_symlink() or sha(path)!=digest:raise ValueError('source provenance drift')
 if (run/'build-provenance/source-head.txt').read_text().strip()!=TERMINATED_PRE_NR_HEAD or (run/'build-provenance/source-diff.patch').read_bytes()!=b'':raise ValueError('nonclean reviewed source')
 recorded={}
 for line in (run/'build-provenance/artifacts.sha256').read_text().splitlines():
  digest,path=line.split(None,1)
  if not re.fullmatch('[0-9a-f]{64}',digest) or path in recorded:raise ValueError('malformed artifact provenance')
  recorded[path]=digest
 native={p:d for p,d in manifest.get('artifacts',{}).items() if Path(p).is_relative_to(Path(manifest['native_build']))}
 if not native or recorded!=native or provenance.get('artifacts')!=manifest.get('artifacts') or provenance.get('source_commit')!=TERMINATED_PRE_NR_HEAD or provenance.get('observed_clean_build') is not True or not provenance.get('observation'):raise ValueError('missing observed exact artifacts')
 # Bind the reviewed source boundary by exact commit and source content, not
 # current mutable checkout or a generic log-absence rule.
 def source(path):return subprocess.check_output(['git','show',TERMINATED_PRE_NR_HEAD+':'+path],cwd=WT,text=True)
 owner=source('gpu/shadps4/video_core/renderer_vulkan/native_async_pre_sr_origin_owner.cpp')
 native_source=source('gpu/shadps4/video_core/renderer_vulkan/native_async_pre_sr.cpp')
 if not owner.index('create_directories(run+"/frames/bucket-000000")')<owner.index('getProperties2(&props)')<owner.index('Atomic(run+"/async-config.json"')<owner.index('NativePreSrSpawnLog::Spawn('):raise ValueError('unrecognized bootstrap boundary')
 if 'if(!Enabled()||!Armed())return false;' not in native_source or 'if(!Enabled()||!Armed()||!actual_supported' not in native_source:raise ValueError('unrecognized NR resource gate')
 identity=json.loads((run/'game-identity.json').read_text());auth=json.loads((run/'authorized-abnormal-termination.json').read_text())
 if auth.get('identity')!=identity or auth.get('scope')!='explicit user-authorized termination of one frozen private test; NOT normal GPU retirement' or auth.get('signals')!=['SIGTERM','SIGKILL'] or auth.get('identity_remaining') is not False or auth.get('original_hold_sha256')!='7be3c9c44e16262b34d181b093970aaa0f9c04a6acdf76188cd80b3bba717345' or not 0<auth.get('monotonic_ns',0)<auth.get('completed_monotonic_ns',0):raise ValueError('missing exact termination authority')
 diagnostics=auth.get('diagnostic_sha256',{})
 if set(diagnostics)!={'exit-thread-stacks-user.txt','frozen-registers.txt'}:raise ValueError('missing frozen diagnostics')
 for name,digest in diagnostics.items():
  if sha(run/name)!=digest:raise ValueError('frozen diagnostic drift')
 text=(run/'game.log').read_text(errors='strict');frontend=0;pending=0
 for line in text.splitlines():
  if not re.search(r'NATIVE_ASYNC_|NATIVE_ORIGIN_|NATIVE_PRE_SR_|HELPER_|PERSISTENT_|NR_RUNTIME_|ASYNC_CONFIG|INPUT_PUBLISHED',line):continue
  name=line.split(' ',1)[0];fields=dict(re.findall(r'(\w+)=(\S+)',line))
  if name=='NATIVE_ASYNC_PRE_SR_FRONTEND_READY':
   if line!='NATIVE_ASYNC_PRE_SR_FRONTEND_READY session='+run.name+' scene_arm=REQUIRED availability_only=1 no_GPU_proof=1':raise ValueError('unknown frontend evidence')
   frontend+=1;continue
  if name=='NATIVE_ASYNC_HELPER_START_REQUEST':
   if line!='NATIVE_ASYNC_HELPER_START_REQUEST session='+run.name+' scene_first=1 scene_proof=REQUIRES_ROOT GPU_proof=NONE':raise ValueError('unknown helper-start evidence')
   continue
  if name=='NATIVE_ASYNC_EXIT_PENDING':
   if line!='NATIVE_ASYNC_EXIT_PENDING actual_owner_completion=WAITING forced_free=0':raise ValueError('unknown pending evidence')
   pending+=1;continue
  raw_checks={
   'NATIVE_PRE_SR_SR_EXPERIMENT':{'fixed_bound':'0','cache_content_selected':'0'},
   'NATIVE_ASYNC_PRE_SR_INPUT_DOMAIN':{'cache_selected':'0','input_domain':'raw','request_origin':'0','candidate_job':'0','request_age':'0'},
   'NATIVE_ASYNC_PRE_SR_SR_READ':{'cache_selected':'0','request_origin':'0','candidate_job':'0'},
   'NATIVE_ASYNC_PRE_SR_CURRENT_PRESENT':{'cache_selected':'0','job':'0','current_NR_inference_claim':'0'},
  }
  if name not in raw_checks or fields.get('session')!=run.name or any(fields.get(k)!=v for k,v in raw_checks[name].items()):raise ValueError('NR-owning or unrecognized log evidence')
 if frontend!=1 or pending!=1:raise ValueError('incomplete pre-NR freeze log')
 names=['manifest.json','result.json','runner.json','game-identity.json','game.log','native-loaded-libraries.json','authorized-abnormal-termination.json',*diagnostics]
 return {name:sha(run/name) for name in names}|{'build-provenance/'+name:digest for name,digest in files.items()}

def verify_fresh_context_resolution(run, resolution, lock):
 if lock is None:raise ValueError('fresh-context exception requires held GPU lock')
 if Path(os.readlink('/proc/self/fd/'+str(lock.fileno())))!=Path('/tmp/nrbridge-poc-gpu.lock'):raise ValueError('wrong GPU lock')
 # Reacquiring on the supplied open file description is safe; verify exclusion independently.
 fcntl.flock(lock,fcntl.LOCK_EX|fcntl.LOCK_NB)
 with open('/tmp/nrbridge-poc-gpu.lock','a') as probe:
  try:fcntl.flock(probe,fcntl.LOCK_EX|fcntl.LOCK_NB)
  except BlockingIOError:pass
  else:raise ValueError('GPU lock not exclusive')
 if resolution.get('kind') not in ('pre-nr-absence','terminated-pre-nr-freeze') or resolution.get('scope')!='fresh-context-only' or resolution.get('normal_retirement_proved') is not False:raise ValueError('wrong resolution scope')
 audit=audit_pre_nr_absence if resolution['kind']=='pre-nr-absence' else audit_terminated_pre_nr_freeze
 evidence=audit(run,resolution.get('build_observation',{}))
 if resolution.get('evidence_sha256')!=evidence:raise ValueError('pre-NR evidence changed')
 boot=Path('/proc/sys/kernel/random/boot_id').read_text().strip()
 manifest=json.loads((run/'manifest.json').read_text())
 if resolution.get('boot_id')!=boot or manifest['host_safety']['boot_id']!=boot:raise ValueError('same boot required')
 identities=list(json.loads((run/'runner.json').read_text()).values())
 game_identity=run/'game-identity.json'
 if game_identity.exists():identities.append(json.loads(game_identity.read_text()))
 elif resolution['kind']!='pre-nr-absence' or evidence.get('game-identity.json')!='ABSENT_BEFORE_CONTROL_OBSERVATION':raise ValueError('game identity absence not proved')
 for identity in identities:
  try:current=live(identity['pid'])
  except FileNotFoundError:continue
  if current['start_ticks']==identity['start_ticks']:raise ValueError('original process still live')
 if holders():raise ValueError('project GPU holder remains')
 observation=resolution.get('host_observation',{});path=Path(observation.get('path',''))
 if not path.is_file() or sha(path)!=observation.get('sha256'):raise ValueError('host observation missing')
 host=json.loads(path.read_text());age=time.monotonic_ns()-host.get('monotonic_ns',0)
 if host.get('boot_id')!=boot or age<0 or host.get('project_holders')!=[] or host.get('driver_errors')!=[] or host.get('unresolved_driver_hang') is not False or host.get('covered_session')!=run.name or host.get('covers_run_start_through_observation') is not True:raise ValueError('signed post-exit driver coverage required')

 if resolution['kind']=='terminated-pre-nr-freeze':
  saved_journal=run/'post-termination-kernel.jsonl'
  if not saved_journal.is_file() or saved_journal.is_symlink() or sha(saved_journal)!=host.get('kernel_journal_sha256'):raise ValueError('post-termination kernel evidence changed')
  check_kernel_journal(saved_journal.read_text(),boot)

 # Read current-boot kernel records on every admission; unavailable/empty/malformed output fails closed.
 journal=subprocess.check_output(['journalctl','-k','-b','0','--no-pager','--output=json'],text=True,stderr=subprocess.PIPE,timeout=20)
 check_kernel_journal(journal,boot)

def check_kernel_journal(journal,boot):
 records=[json.loads(line) for line in journal.splitlines() if line.strip()]
 if not records:raise ValueError('current boot kernel journal unavailable')
 for record in records:
  if record.get('_BOOT_ID','').replace('-','')!=boot.replace('-',''):raise ValueError('kernel journal boot mismatch')
  message=record.get('MESSAGE','')
  if not isinstance(message,str):raise ValueError('unreadable kernel journal message')
  if re.search(r'NVRM:.*(?:Xid|fallen off|GPU.*(?:error|timeout))|GPU HANG|GPU reset|amdgpu.*(?:timeout|fault|reset)|i915.*(?:hang|reset)|nouveau.*(?:fault|timeout)',message,re.I):raise ValueError('current boot GPU driver error: '+message)

def held_run_snapshot():
 out={}
 for p in (ROOT/'runs').glob('*/result.json'):
  if json.loads(p.read_text()).get('status')!='HOLD':continue
  names=('result.json','manifest.json','hold-resolution.json','game.log','kernel-gpu-faults.jsonl','gpu-fault-observation.json')
  out[p.parent.name]={'boot_id':json.loads((p.parent/'manifest.json').read_text())['host_safety']['boot_id'],'files':{n:sha(p.parent/n) for n in names if (p.parent/n).is_file()}}
 return out

def validate_pm4_producer_context(args):
 enabled=getattr(args,'pm4_producer_context',False)
 if type(enabled) is not bool or (enabled and (type(getattr(args,'pm4_cold_trace',False)) is not bool or not getattr(args,'pm4_cold_trace',False) or sum(bool(getattr(args,k,None)) for k in ('reboot_diagnostic','context_recovery'))!=1)):raise ValueError('producer context requires cold trace and exactly one isolated diagnostic scope')
 return enabled


_mouse_spec=importlib.util.spec_from_file_location('nr_native_mouse_scope',Path(__file__).with_name('native_mouse_scope.py'))
native_mouse_scope=importlib.util.module_from_spec(_mouse_spec);_mouse_spec.loader.exec_module(native_mouse_scope)

def validate_reboot_scope(record,args,boot,snapshot,provenance,artifacts):
 native_mouse_scope.check_record(record,native_mouse_scope.requested(args))
 schema=record.get('schema')
 if schema not in ('nr-reboot-sr-diagnostic-v1','nr-reboot-sr-diagnostic-v2') or record.get('normal_retirement_proved') is not False or record.get('historical_faults_resolved') is not False:raise ValueError('diagnostic is not historical retirement/recovery proof')
 def boot_uuid(value):
  if not isinstance(value,str) or not value:raise ValueError('valid boot UUID required')
  try:parsed=uuid.UUID(value)
  except (ValueError,AttributeError):raise ValueError('valid boot UUID required') from None
  if str(parsed)!=value:raise ValueError('canonical boot UUID required')
  return value
 boot_uuid(boot);boot_uuid(record.get('boot_id'))
 if record.get('boot_id')!=boot:raise ValueError('actual new boot required')
 if record.get('held_runs')!=snapshot or not snapshot:raise ValueError('new or changed HOLD; diagnostic refused')
 historical=sorted({boot_uuid(x.get('boot_id')) for x in snapshot.values()})
 if boot in historical:raise ValueError('actual new boot required')
 previous=record.get('previous_boot_id')
 if previous is not None:
  boot_uuid(previous)
  if previous==boot:raise ValueError('actual new boot required')
 if schema=='nr-reboot-sr-diagnostic-v1':
  if not previous or historical!=[previous]:raise ValueError('new or changed HOLD; diagnostic refused')
 else:
  if record.get('historical_boot_ids')!=historical:raise ValueError('exact sorted unique historical boots required')
 producer=validate_pm4_producer_context(args)
 if schema=='nr-reboot-sr-diagnostic-v1':
  if producer or record.get('producer_context',False) is not False:raise ValueError('v1 forbids producer context')
 elif type(record.get('producer_context')) is not bool or record['producer_context'] is not producer:raise ValueError('producer context request drift')
 if record.get('session')!=args.session or args.mode!='skipEvaluate' or args.sky!='off' or not 1<=args.seconds<=180 or args.watchdog_seconds>300 or args.runtime_model_helper or args.history_diagnostic or getattr(args,'continue_session',None) or not args.pm4_cold_trace:raise ValueError('one bounded SR-only diagnostic required')
 if record.get('source_commit')!=provenance['source_commit'] or record.get('native_artifacts')!=artifacts:raise ValueError('diagnostic build drift')
 if record.get('project_holders')!=[] or record.get('driver_errors')!=[] or not record.get('authorization_basis'):raise ValueError('current observation required')

def reboot_diagnostic_preflight(lock,args):
 if lock is None or Path(os.readlink('/proc/self/fd/'+str(lock.fileno())))!=Path('/tmp/nrbridge-poc-gpu.lock'):raise ValueError('GPU lock required')
 fcntl.flock(lock,fcntl.LOCK_EX|fcntl.LOCK_NB)
 with open('/tmp/nrbridge-poc-gpu.lock','a') as probe:
  try:fcntl.flock(probe,fcntl.LOCK_EX|fcntl.LOCK_NB)
  except BlockingIOError:pass
  else:raise ValueError('GPU lock not exclusive')
 path=Path(args.reboot_diagnostic)
 if path.parent!=ROOT/'inputs' or path.resolve()!=path or not path.is_file() or path.stat().st_size>131072:raise ValueError('bounded private diagnostic observation required')
 record=json.loads(path.read_text());boot=Path('/proc/sys/kernel/random/boot_id').read_text().strip()
 _,assets,provenance=checked_native_build(args.native_build or ROOT/'build/native')
 validate_reboot_scope(record,args,boot,held_run_snapshot(),provenance,{str(p):sha(p) for p in assets})
 if checked_run(args.session).exists():raise ValueError('diagnostic already consumed')
 journal=subprocess.check_output(['journalctl','-k','-b','0','--no-pager','--output=json'],text=True,stderr=subprocess.PIPE,timeout=20);check_kernel_journal(journal,boot)
 if type(record.get('monotonic_ns')) is not int or not 0<record['monotonic_ns']<=time.monotonic_ns():raise ValueError('positive current-boot observation timestamp required')
 return {'boot_id':boot,'unresolved_driver_hang':False,'scope':'one reboot-isolated SR diagnostic only; old GPU fault unresolved','reboot_diagnostic':True,'historical_faults_resolved':False,'evidence':[{'path':str(path),'sha256':sha(path)}]}

def daily_preflight(lock,runtime_binding):
 if lock is None:raise ValueError('daily mode requires exclusive GPU lock')
 if Path(os.readlink('/proc/self/fd/'+str(lock.fileno())))!=Path('/tmp/nrbridge-poc-gpu.lock'):raise ValueError('wrong GPU lock')
 fcntl.flock(lock,fcntl.LOCK_EX|fcntl.LOCK_NB)
 with open('/tmp/nrbridge-poc-gpu.lock','a') as probe:
  try:fcntl.flock(probe,fcntl.LOCK_EX|fcntl.LOCK_NB)
  except BlockingIOError:pass
  else:raise ValueError('GPU lock not exclusive')
 if runtime_binding is None:raise ValueError('daily mode requires explicit versioned runtime inputs')
 guard=daily_session_guard.SessionGuard();guard.admit()
 if holders():raise ValueError('live Native/Helper holders')
 boot=Path('/proc/sys/kernel/random/boot_id').read_text().strip()
 # Historical failures remain evidence. A current-boot unknown GPU/resource state
 # blocks daily play; reboot isolation does not turn old failures into retirement.
 for session,record in held_run_snapshot().items():
  if record['boot_id']==boot:raise ValueError('current-boot HOLD requires resolution: '+session)
 for path in (ROOT/'runs').glob('*/manifest.json'):
  manifest=json.loads(path.read_text())
  if manifest.get('daily') is not True or manifest.get('host_safety',{}).get('boot_id')!=boot:continue
  result=path.parent/'result.json'
  if not result.is_file() or json.loads(result.read_text()).get('status')!='PASS_NORMAL_TEARDOWN':
   raise ValueError('current-boot daily retirement not proved: '+path.parent.name)
 verify_runtime_inputs(runtime_binding)
 journal=subprocess.check_output(['journalctl','-k','-b','0','--no-pager','--output=json'],text=True,stderr=subprocess.PIPE,timeout=20)
 check_kernel_journal(journal,boot)
 return {'boot_id':boot,'daily':True,'boot_session_receipt':str(guard.path),'unresolved_driver_hang':False,'historical_faults_resolved':False,'evidence':['live current-boot kernel check and no current-boot HOLD; exclusive project GPU lock; per-UID boot receipt admits all homes']}

def record_pending_daily_retirement(run):
 # Persist before spawn so a supervisor crash cannot erase this admission gate.
 atomic(run/'result.json',{'status':'HOLD','reason':'daily session pending actual normal retirement','native_lastuse_proof':False,'returncode':None})

def daily_failure_reason(run,capture):
 if (run/'native.abort').exists():return 'Native quarantined shared GPU ownership'
 if capture.state.get('failure_latched'):return 'recorded resource/GPU failure'
 if capture.state.get('error') is not None:return 'daily log capture failed; evidence incomplete'
 return None

def preflight(lock=None,recovery=None,runtime_binding=None):
 # All canonical callers hold the project GPU lock, including finite/recovery launches.
 # A diagnostic scope cannot override another home's same-boot unknown ownership.
 daily_session_guard.SessionGuard().admit()
 h=holders()
 if h:raise ValueError('live Native/Helper GPU holders: '+json.dumps(h))
 if recovery is not None:
  if getattr(recovery,'context_recovery',None):
   from context_recovery import check_launch
   return check_launch(argparse.Namespace(**globals()),recovery,lock)
  verify_runtime_inputs(runtime_binding)
  return reboot_diagnostic_preflight(lock,recovery)
 for result in (ROOT/'runs').glob('*/result.json'):
  if json.loads(result.read_text()).get('status')=='HOLD':
   resolution=result.parent/'hold-resolution.json'
   if not resolution.is_file():raise ValueError('unresolved new-context HOLD '+str(result))
   resolved=json.loads(resolution.read_text())
   if resolved.get('original_result_sha256')!=sha(result):raise ValueError('HOLD result changed')
   if resolved.get('kind') in ('pre-nr-absence','terminated-pre-nr-freeze'):verify_fresh_context_resolution(result.parent,resolved,lock)
   elif resolved.get('normal_retirement_proved') is not True or not resolved.get('evidence'):raise ValueError('unproved HOLD resolution '+str(resolution))
 verify_runtime_inputs(runtime_binding)
 safety=json.loads((ROOT/'inputs/host-safety.json').read_text())
 boot=Path('/proc/sys/kernel/random/boot_id').read_text().strip()
 if safety.get('boot_id')!=boot or safety.get('unresolved_driver_hang') is not False or not safety.get('evidence'):
  raise ValueError('coordinator must inspect current boot driver-hang evidence before GPU launch')
 return safety
def verify_runtime_inputs(binding=None):
 if binding is not None:return runtime_inputs.recheck(binding,ROOT)
 for pin in json.loads((ROOT/'inputs/runtime-pins.json').read_text())['pins']:
  if sha(Path(pin['path']))!=pin['sha256']:raise ValueError('runtime pin drift '+pin['path'])

def checked_run(session):
 if not re.fullmatch('[A-Za-z0-9][A-Za-z0-9-]{0,62}',session):raise ValueError('safe session required')
 run=ROOT/'runs'/session
 if run.resolve()!=run or run.is_symlink():raise ValueError('private real run path required')
 return run
def checked_native_build(directory):
 """Only the mutable dev build or a clean commit-addressed four-artifact baseline."""
 build=Path(directory).absolute()
 if build.resolve()!=build or build.is_symlink():raise ValueError('Native build must use a real canonical directory')
 baseline=build.parent==ROOT/'build/baselines' and re.fullmatch('[0-9a-f]{40}',build.name)
 if build not in (ROOT/'build/native',WT/'out') and not baseline:raise ValueError('Native build must be this checkout out/, build/native or build/baselines/<full commit>')
 def regular(relative):
  path=build/relative
  if path.resolve()!=path or not path.is_file() or path.is_symlink():raise ValueError('regular in-tree build input required: '+str(path))
  return path
 provenance={name:regular(name) for name in ('source-head.txt','source-diff.patch','artifacts.sha256')}
 head=provenance['source-head.txt'].read_text().strip()
 if not re.fullmatch('[0-9a-f]{40}',head):raise ValueError('full Native source commit required')
 if baseline and (head!=build.name or provenance['source-diff.patch'].stat().st_size):raise ValueError('baseline must match clean source commit')
 ngx=list(build.glob('libnvidia-ngx-dlss.so.*'))
 if len(ngx)!=1:raise ValueError('one pinned Native NGX library required')
 relatives=['bb-probe','gpu/libbbgpu.so','libbbport_dlss.so',ngx[0].name]
 artifacts=[regular(name) for name in relatives];recorded={}
 for line in provenance['artifacts.sha256'].read_text().splitlines():
  match=re.fullmatch(r'([0-9a-f]{64})  (.+)',line)
  if not match:raise ValueError('invalid Native artifacts manifest')
  digest,name=match.groups();path=Path(name)
  # Original build/native paths remain valid provenance after a read-only snapshot.
  if path.is_absolute():
   if path.is_relative_to(build):relative=str(path.relative_to(build))
   elif baseline and path.is_relative_to(ROOT/'build/native'):relative=str(path.relative_to(ROOT/'build/native'))
   else:raise ValueError('unexpected artifact source path')
  else:relative=name
  if relative not in relatives or relative in recorded:raise ValueError('unexpected/duplicate Native artifact')
  recorded[relative]=digest
 if set(recorded)!=set(relatives) or any(sha(build/name)!=recorded[name] for name in relatives):raise ValueError('Native artifact SHA mismatch')
 return build,artifacts,{'source_commit':head,'files':{name:sha(path) for name,path in provenance.items()}}

def checked_runtime_helper(path,expected_build_sha=None):
 helper=Path(path).resolve()
 if not helper.is_relative_to(ROOT/'build/helper-runtime-model') or helper.name!='wine_runtime_model.exe':raise ValueError('isolated runtime model artifact required')
 manifest_path=helper.parent/'build.json';digest=sha(manifest_path)
 if expected_build_sha and digest!=expected_build_sha:raise ValueError('runtime Helper build manifest SHA drift')
 manifest=json.loads(manifest_path.read_text())
 if manifest.get('status')!='BUILD_ONLY_PASS' or manifest.get('artifact_sha256')!=sha(helper) or Path(manifest.get('artifact','')).resolve()!=helper:raise ValueError('runtime Helper build/artifact identity drift')
 return helper,{'path':str(manifest_path),'sha256':digest,'artifact_sha256':sha(helper)}

def save_inventory(root):
 if root.is_symlink() or root.resolve()!=root or not root.is_dir():raise ValueError('private real savedata directory required')
 inventory={}
 for p in sorted(root.rglob('*')):
  if p.is_symlink() or p.resolve()!=p or not(p.is_dir() or p.is_file()):raise ValueError('save symlink or special file refused')
  if p.is_file():inventory[str(p.relative_to(root))]={'bytes':p.stat().st_size,'sha256':sha(p)}
 if not inventory:raise ValueError('empty private continuation savedata')
 return inventory

def continuation_source(session):
 source=checked_run(session);manifest=json.loads((source/'manifest.json').read_text());result=json.loads((source/'result.json').read_text())
 if manifest.get('schema')!='nr-dev-session-v1' or manifest.get('session')!=session:raise ValueError('only a named new dev session can be continued')
 if result.get('status')!='PASS_NORMAL_TEARDOWN' or result.get('returncode')!=0 or result.get('supervisor_interrupted',False) or result.get('watchdog_expired') is not False or result.get('residuals')!=[]:raise ValueError('continuation requires unambiguous normal retired session, not HOLD/resolution')
 environment=manifest.get('environment',{})
 if environment.get('BB_USER_DIR')!=str(source/'profile/user') or environment.get('BB_DATA_DIR')!=str(source/'profile'):raise ValueError('source must use this new session private save/profile paths')
 from peer_retirement import audit_session
 proof=audit_session(source)
 saves=source/'profile/user/savedata';inventory=save_inventory(saves);config=source/'profile/nrbridge.json'
 if config.is_symlink() or (config.exists() and (config.resolve()!=config or not config.is_file() or config.stat().st_size>65536)):raise ValueError('private regular bounded NR config required')
 return source,{'session':session,'source_result_sha256':sha(source/'result.json'),'source_manifest_sha256':sha(source/'manifest.json'),'retirement':proof['retirement'],'files':inventory,'nrbridge_json_sha256':sha(config) if config.exists() else None}

def copy_continuation(session,profile):
 source,proof=continuation_source(session);shutil.copytree(source/'profile/user/savedata',profile/'user/savedata')
 if save_inventory(profile/'user/savedata')!=proof['files'] or save_inventory(source/'profile/user/savedata')!=proof['files']:raise ValueError('private save changed during continuation copy')
 if proof['nrbridge_json_sha256']:
  with (profile/'nrbridge.json').open('xb') as out:out.write((source/'profile/nrbridge.json').read_bytes())
  if sha(profile/'nrbridge.json')!=proof['nrbridge_json_sha256']:raise ValueError('NR config continuation SHA mismatch')
 return proof

class SessionSchedule:
 """Planned end requests ordinary close; only independent grace/watchdog expiry is HOLD."""
 def __init__(self,start,seconds,watchdog_seconds,close_grace,daily=False):
  if daily:
   if seconds!=0 or watchdog_seconds!=0 or not 1<=close_grace<=600:raise ValueError('daily mode has no total budget; positive close grace required')
   self.planned=self.watchdog=math.inf;self.grace=close_grace;self.close_started=None;return
  if not 1<=seconds<watchdog_seconds<=600 or not 1<=close_grace<=600:raise ValueError('require 1 <= planned seconds < watchdog <=600 and positive close grace')
  self.planned=start+seconds;self.watchdog=start+watchdog_seconds;self.grace=close_grace;self.close_started=None
 def close_due(self,now):return now>=self.planned
 def note_close(self,now):
  if self.close_started is None:self.close_started=now
 def expired(self,now):return now>=min(self.watchdog,self.close_started+self.grace if self.close_started is not None else self.watchdog)

def btrfs_fsid(path):
 # Linux BTRFS_IOC_FS_INFO = _IOR(0x94, 31, struct btrfs_ioctl_fs_info_args[1024]).
 fd=os.open(path,os.O_RDONLY)
 try:
  data=bytearray(1024);fcntl.ioctl(fd,0x8400941f,data,True)
  fsid=bytes(data[16:32])
  if not any(fsid):raise ValueError('empty Btrfs filesystem identity')
  return fsid.hex()
 finally:os.close(fd)
def mapped_file_identity(source,fields,pid=None):
 st=Path(source).stat();major,minor=(int(v,16) for v in fields[3].split(':'))
 if st.st_ino!=int(fields[4]):raise ValueError('mapped inode mismatch '+source)
 if (os.major(st.st_dev),os.minor(st.st_dev))==(major,minor):return {'device_proof':'direct stat device and inode'}
 if pid is not None:
  try:
   backing=(Path('/proc')/str(pid)/'map_files'/fields[0]).stat()
   if (backing.st_dev,backing.st_ino)==(st.st_dev,st.st_ino):return {'device_proof':'actual map_files backing stat device and inode'}
   raise ValueError('actual map_files backing mismatch '+source)
  except PermissionError:pass
 # Btrfs maps uses the superblock device while stat may expose a subvolume's
 # anonymous st_dev. Accept only exact mountinfo superblock device + inode and
 # same FS_INFO fsid for source and its most-specific Btrfs mount, never just inode.
 mounts=(Path('/proc')/str(pid or 'self')/'mountinfo').read_text().splitlines();candidates=[]
 def decode(text):return re.sub(r'\\([0-7]{3})',lambda m:chr(int(m[1],8)),text)
 for line in mounts:
  before,after=line.split(' - ',1);left=before.split();right=after.split();mount=decode(left[4])
  if Path(source).is_relative_to(mount):candidates.append((len(mount),left,right,mount))
 if not candidates:raise ValueError('no source mount identity '+source)
 _,left,right,mount=max(candidates)
 if right[0]!='btrfs' or tuple(int(v) for v in left[2].split(':'))!=(major,minor):raise ValueError('mapped device is not source Btrfs superblock '+source)
 fsid=btrfs_fsid(source)
 if fsid!=btrfs_fsid(mount):raise ValueError('Btrfs filesystem identity mismatch '+source)
 return {'device_proof':'Btrfs mountinfo superblock device + source inode + equal FS_INFO fsid','superblock_device':left[2],'stat_device':str(os.major(st.st_dev))+':'+str(os.minor(st.st_dev)),'mount':mount,'mount_root':decode(left[3]),'filesystem_id':fsid}

def verify_native_mappings(maps,artifacts,pid=None):
 """NGX's loader uses source-named deleted memfds; this checks provenance, not equal backing bytes.

 vk_dlss dlopens the bridge from /proc/self/exe's directory and Configure receives
 that same directory. The pinned bridge/SDK internally loads NGX into memfds.
 A source-named memfd may contain unpacked/patched bytes, so no original-byte
 backing hash is asserted. Only the complete absolute embedded source is accepted.
 """
 expected={str(Path(p)):h for p,h in artifacts.items() if re.search(r'/lib(?:bbgpu|bbnet(?:20)?|bbport_dlss|nvidia-ngx-dlss)\.so',p)}
 for path,digest in artifacts.items():
  if sha(Path(path))!=digest:raise ValueError('launch artifact drift '+path)
 out=[]
 for line in maps.splitlines():
  fields=line.split(None,5)
  if len(fields)!=6:continue
  pathname=fields[5]
  if not re.search(r'/lib(?:bbgpu|bbnet(?:20)?|bbport_dlss|nvidia-ngx-dlss)\.so',pathname):continue
  memfd=pathname.startswith('/memfd:')
  source=pathname[len('/memfd:'):] if memfd else pathname
  deleted=source.endswith(' (deleted)')
  if deleted:source=source[:-len(' (deleted)')]
  if source not in expected:raise ValueError('unapproved Native mapped source '+pathname)
  device_proof={}
  if memfd:
   if not Path(source).name.startswith('libnvidia-ngx-dlss.so.') or not deleted:raise ValueError('unexpected Native memfd '+pathname)
  else:
   if deleted:raise ValueError('deleted ordinary runtime library '+pathname)
   device_proof=mapped_file_identity(source,fields,pid)
  out.append({'range':fields[0],'permissions':fields[1],'offset':fields[2],'device':fields[3],'inode':int(fields[4]),'pathname':pathname,'source':source,'source_sha256':expected[source],'identity_scope':'exact pinned loader source provenance; memfd backing bytes not asserted' if memfd else 'mapped device/inode and pinned source SHA','memfd':memfd,'mapped_file_identity':device_proof})
 return out

def current_game(run):
 m=json.loads((run/'manifest.json').read_text());matches=[]
 if 'native_build' in m:
  build,artifacts,provenance=checked_native_build(m['native_build'])
  if m.get('native_build_provenance')!=provenance or m['native_executable']!=str(build/'bb-probe') or any(m['artifacts'].get(str(p))!=sha(p) for p in artifacts):raise ValueError('session Native build provenance drift')
 for p in Path('/proc').iterdir():
  if not p.name.isdigit():continue
  try:
   if (p/'exe').resolve()==Path(m['native_executable']) and str(run/'profile').encode() in (p/'environ').read_bytes():matches.append(live(int(p.name)))
  except (OSError,ValueError):pass
 if len(matches)!=1:raise ValueError('exact private live game identity required')
 identity=matches[0];record=run/'game-identity.json'
 if record.exists() and json.loads(record.read_text())!=identity:raise ValueError('PID/starttime mismatch')
 maps=(Path('/proc')/str(identity['pid'])/'maps').read_text()
 libs=verify_native_mappings(maps,m['artifacts'],identity['pid'])
 if m.get('online',{}).get('pool','offline')!='offline':
  module=Path(m['native_build'])/'gpu/libbbnet20.so'
  if m.get('environment',{}).get('BB_NET_MODULE')!=str(module) or not any(item['source']==str(module) for item in libs):raise ValueError('selected online module is not actually mapped')
 atomic(run/'native-loaded-libraries.json',{'game':identity,'mapped_libraries':libs})
 atomic(record,identity);return identity

def control(args):
 run=checked_run(args.session)
 if args.command in ('start-helper','arm') and json.loads((run/'manifest.json').read_text()).get('host_safety',{}).get('reboot_diagnostic'):raise ValueError('reboot diagnostic forbids Helper and NR')
 game=current_game(run)
 if args.command in ('start-helper','arm'):
  session_manifest=json.loads((run/'manifest.json').read_text());binding=session_manifest.get('runtime_inputs')
  daily=session_manifest.get('daily',False)
  if type(daily) is not bool:raise ValueError('daily manifest requires strict boolean')
  if daily:validate_daily_manifest(run,session_manifest)
  if session_manifest.get('nr_enabled') is False:
   raise ValueError('selected SR-only session does not permit Helper/NR activation')
  if binding:
   runtime_inputs.recheck(binding,ROOT)
   if daily:runtime_inputs.daily_capability(Path(session_manifest['native_build']),binding)
   helper_config=run/'helper-runtime.json'
   if helper_config.resolve()!=helper_config or session_manifest.get('helper_runtime_config')!={'path':str(helper_config),'sha256':sha(helper_config)} or runtime_inputs.read(helper_config)!=runtime_inputs.helper_config(binding):raise ValueError('session Helper runtime config drift')
  if not daily:
   from context_recovery import check_control
   check_control(argparse.Namespace(**globals()),run,game,args.command)
 if args.command=='dump':
  (run/'dump').mkdir(exist_ok=True)
  with (run/'present-dump-trigger').open('x') as trigger:trigger.write(args.session+'\n')
  return
 if args.command=='input':
  allowed={'cross','circle','square','triangle','up','down','left','right','options','l1','r1','l2','r2','l3','r3'}
  if not set(args.buttons)<=allowed or not math.isfinite(args.hold) or not 0<=args.hold<=5 or any(not 0<=getattr(args,k)<=255 for k in ('lx','ly','rx','ry')):raise ValueError('bounded recognized pad input required')
  pad=run/'pad.txt'
  if pad.is_symlink() or not pad.is_file():raise ValueError('private regular pad file required')
  def interrupt(signum,frame):raise InterruptedError('bounded input interrupted')
  for sig in (signal.SIGTERM,signal.SIGINT):signal.signal(sig,interrupt)
  try:
   pad.write_text(' '.join(args.buttons+[k+'='+str(getattr(args,k)) for k in ('lx','ly','rx','ry')]))
   time.sleep(args.hold)
  finally:pad.write_text('')
  return
 if args.command=='scene-ready':atomic(run/'scene-ready.json',{'session':args.session,'game':game,'basis':'explicit coordinator/player scene observation','monotonic_ns':time.monotonic_ns()});return
 if args.command in ('start-helper','arm'):
  observed=json.loads((run/'scene-ready.json').read_text())
  if observed['game']!=game:raise ValueError('same live scene identity required')
  name='native-helper-start' if args.command=='start-helper' else 'native-scene-arm'
  if args.command=='arm':
   ready=json.loads((run/('async-fd-retired-'+args.session+'-g0.json')).read_text())
   if not(ready.get('session')==args.session and ready.get('status')=='FD_RETIRED' and ready.get('completed_GPU') is True and ready.get('export_raw_fd_owned')==0):raise ValueError('actual GPU import and FD retirement readiness required')
  with (run/name).open('x') as f:f.write(args.session+'\n')
 if args.command in ('stop','focus'):
  windows=json.loads(subprocess.check_output(['niri','msg','--json','windows'],text=True,timeout=5))
  own=[w for w in windows if w.get('app_id')=='bb-probe' and w.get('title')=='Bloodborne™']
  if len(own)!=1:raise ValueError('unique private game window required')
  prop=subprocess.check_output(['xprop','-name',own[0]['title'],'_NET_WM_PID','WM_CLASS'],text=True,timeout=5)
  if not re.search(r'_NET_WM_PID\(CARDINAL\) = '+str(game['pid'])+r'\b',prop):raise ValueError('window PID mismatch')
  if args.command=='focus':
   subprocess.run(['niri','msg','action','focus-window','--id',str(own[0]['id'])],check=True,timeout=5);return
  subprocess.run(['niri','msg','action','close-window','--id',str(own[0]['id'])],check=True,timeout=5)
  atomic(run/'stop-request.json',{'game':game,'monotonic_ns':time.monotonic_ns(),'scope':'normal window close request; not retirement proof'})

def validate_daily_manifest(run,manifest):
 smooth_motion.validate_manifest(run,manifest)
 dlssg_backend.validate_manifest(run,manifest)
 environment=manifest.get('environment',{})
 for directory in (run/'profile',run/'profile/user'):
  if directory.resolve()!=directory or directory.is_symlink() or not directory.is_dir():raise ValueError('daily private profile directory drift')
 if (manifest.get('schema')!='nr-dev-session-v1' or manifest.get('session')!=run.name
     or manifest.get('daily') is not True or not manifest.get('runtime_inputs')
     or manifest.get('host_safety',{}).get('daily') is not True
     or any(type(manifest.get(k)) is not int or manifest[k]!=0 for k in ('planned_seconds','watchdog_seconds'))):raise ValueError('invalid daily session binding')
 required={'BB_NR_DAILY':'1','NRBRIDGE_HOME':str(ROOT),'BB_NR_NATIVE_ASYNC_SOURCE_LIMIT':'0',
           'BB_DATA_DIR':str(run/'profile'),'BB_USER_DIR':str(run/'profile/user'),
           'BB_GPU_USER_DIR':str(run/'profile/user')}
 if (any(environment.get(k)!=v for k,v in required.items())
     or environment.get('BB_NATIVE_MOUSE') not in ('0','1')
     or any(k in environment for k in ('BB_FRAME_LOG','BB_NR_PM4_COLD_TRACE','BB_NR_PM4_PRODUCER_CONTEXT','BB_NATIVE_HISTORY_DIAGNOSTIC','BB_RE_CAMERA','BB_RE_ENTRY'))):raise ValueError('daily private environment drift')
 if manifest.get('player_settings'):
  player_settings.verify_snapshot(run,manifest['player_settings'],environment)
 elif environment.get('BB_FRAME_STATS')!='0':raise ValueError('diagnostic daily frame statistics drift')
 daily_online.validate_manifest(run,manifest)
 if ui_start.ENVIRONMENT in environment:ui_start.validate(run,manifest)

def check_fg_capability(args, build):
 smooth_motion.check_capability(args,build)
 dlssg_backend.check(args,build)
 # Fail before creating a session when a launcher selects FG on an older installed runtime.
 if not getattr(args, 'experimental_fg', None):return
 data=(Path(build)/'gpu/libbbgpu.so').read_bytes()
 if b'FG_EXIT_PROOF' not in data or (getattr(args,'fg_native',False) and b'BB_FRAME_GEN_NATIVE' not in data):
  raise ValueError('Experimental FG requires a rebuilt M1 Native runtime; selected build is unsupported')
 if args.experimental_fg=='dlssg-capture' and b'DLSSG_INPUT_CAPTURE' not in data:
  raise ValueError('DLSSG input capture requires its rebuilt Native diagnostic')

def validate_daily_args(args):
 smooth_motion.validate_args(args)
 dlssg_backend.validate_args(args)
 daily_online.validate_args(args)
 daily=getattr(args,'daily',False)
 if getattr(args,'experimental_fg',None) and not (daily and (getattr(args,'sr_only',False) or getattr(args,'fg_native',False))):
  raise ValueError('experimental FG requires daily SR-only or explicit --fg-native integration')
 if getattr(args,'fg_native',False) and not (daily and getattr(args,'experimental_fg',None)):
  raise ValueError('--fg-native requires daily experimental FG')
 if getattr(args,'fg_validation',False) and not getattr(args,'experimental_fg',None):
  raise ValueError('FG validation requires an explicit FG experiment')
 if getattr(args,'sr_only',False) and not daily:raise ValueError('SR-only selection requires daily mode')
 if type(daily) is not bool:raise ValueError('daily mode requires strict boolean')
 if not daily:return
 if any(getattr(args,k,None) for k in ('context_recovery','reboot_diagnostic','pm4_cold_trace','pm4_producer_context','history_diagnostic')):raise ValueError('daily mode and cold diagnostic scopes are separate')
 if not getattr(args,'local_inputs',None):raise ValueError('daily mode requires explicit versioned runtime inputs')
 if any(getattr(args,k,None) for k in ('runtime_model_helper','runtime_model_build_sha256')):raise ValueError('daily mode selects Helper from local inputs')
 if any(type(getattr(args,k,None)) is not int or getattr(args,k)!=0 for k in ('seconds','watchdog_seconds','source_limit')):raise ValueError('daily mode has no total time/source budget')

def configure_daily_args(args):
 if getattr(args,'joystick_mouse',False) and getattr(args,'native_mouse',False):raise ValueError('choose one mouse route')
 if getattr(args,'daily',False):
  args.seconds=args.watchdog_seconds=args.source_limit=0;args.native_mouse=not args.joystick_mouse
 validate_daily_args(args)

def sample_gpu_memory():
 """Two independently observed NVML CLI views, never sum PID ownership into device usage."""
 result={'monotonic_ns':time.monotonic_ns(),'pid_used_MiB_csv':''}
 for field,query in (('pid_used_MiB_csv','--query-compute-apps=pid,used_gpu_memory'),('device_uuid_used_total_MiB_csv','--query-gpu=uuid,memory.used,memory.total')):
  try:
   result[field]=subprocess.check_output(['nvidia-smi',query,'--format=csv,noheader,nounits'],text=True,timeout=3)
   result[field+'_observed_monotonic_ns']=time.monotonic_ns()
  except (OSError,subprocess.SubprocessError) as error:result[field+'_status']='NOT_MEASURED: '+type(error).__name__
 return result

class SupervisorSignals:
 """Queue in Python's signal callback; append evidence and request close only on the main loop."""
 def __init__(self):
  self.pending=[];self.received=0;self.interrupted=False;self.close_requested=False;self.run=None;self.old={}
 def receive(self,signum,_frame):
  self.received+=1;self.interrupted=True
  self.pending.append({'event':'SUPERVISOR_SIGNAL_RECEIVED','sequence':self.received,'signal':signum,'signal_name':signal.Signals(signum).name,'monotonic_ns':time.monotonic_ns(),'runner_pid':os.getpid(),'sender':'NOT_AVAILABLE_PYTHON_SIGNAL_CALLBACK','scope':'interruption; not GPU completion or normal retirement proof'})
 def __enter__(self):
  self.old={sig:signal.signal(sig,self.receive) for sig in (signal.SIGTERM,signal.SIGINT)};return self
 def append(self,event):
  fd=os.open(self.run/'signal-events.jsonl',os.O_WRONLY|os.O_CREAT|os.O_APPEND|os.O_NOFOLLOW,0o600)
  try:
   with os.fdopen(fd,'a') as out:out.write(json.dumps(event)+'\n');out.flush();os.fsync(out.fileno())
  except BaseException:raise
 def flush(self):
  if self.run is None:return
  events,self.pending=self.pending,[]
  for event in events:self.append(event)
 def before_spawn(self):
  self.flush()
  if self.interrupted:
   atomic(self.run/'result.json',{'status':'HOLD','reason':'supervisor interrupted before spawn; no process launched','supervisor_interrupted':True,'received_signals':self.received,'actual_game_starts':0,'returncode':None})
   return False
  return True
 def request_close_once(self,session,request=None):
  if not self.interrupted or self.close_requested:return
  self.close_requested=True
  if (self.run/'stop-request.json').exists():
   self.append({'event':'SUPERVISOR_NORMAL_CLOSE_ALREADY_REQUESTED','monotonic_ns':time.monotonic_ns()});return
  try:
   (request or control)(argparse.Namespace(command='stop',session=session))
   self.append({'event':'SUPERVISOR_NORMAL_CLOSE_REQUESTED','monotonic_ns':time.monotonic_ns(),'scope':'normal window close only; waiting actual child exit; GPU lock retained'})
  except Exception as error:self.append({'event':'SUPERVISOR_NORMAL_CLOSE_REQUEST_FAILED','monotonic_ns':time.monotonic_ns(),'error':str(error),'scope':'no kill or forced cleanup; GPU lock retained'})
 def __exit__(self,kind,value,tb):
  # Handle signals delivered during final audit too; never relabel interruption PASS.
  try:
   self.flush()
   if self.interrupted and self.run is not None:
    path=self.run/'result.json';result=json.loads(path.read_text()) if path.exists() else {}
    result.update(status='HOLD',supervisor_interrupted=True,received_signals=self.received)
    atomic(path,result)
  finally:
   for sig,handler in self.old.items():signal.signal(sig,handler)

def launch(args):
 with SupervisorSignals() as signals:
  code=launch_supervised(args,signals)
 return 3 if signals.interrupted else code

def launch_supervised(args,signals):
 validate_daily_args(args)
 player=player_settings.load_request(args)
 if player is not None:args.local_inputs=player_settings.runtime_for_player(args,ROOT)
 native_mouse_scope.requested(args)
 validate_pm4_producer_context(args)
 run=checked_run(args.session);build,native_artifacts,build_provenance=checked_native_build(getattr(args,'native_build',None) or ROOT/'build/native');assets=ASSETS;seed=SEED;helper=ROOT/'build/helper/wine_async_presr_play.exe'
 check_fg_capability(args,build)
 binding=runtime_inputs.load(args.local_inputs,ROOT) if getattr(args,'local_inputs',None) else None
 ge_files=GE/'files';sysroot_lib=WT/'out/deps/root/usr/lib'
 if binding is not None:
  runtime_inputs.native_capability(build)
  if getattr(args,'daily',False):runtime_inputs.daily_capability(build,binding)
  c=binding['config'];assets=Path(c['assets']['path']);seed=Path(c['seed']);helper=Path(c['helper']['path']);ge_files=Path(c['ge_files']);sysroot_lib=Path(c['sysroot_lib'])
  if args.runtime_model_helper or args.runtime_model_build_sha256:raise ValueError('local inputs select Helper; do not mix legacy Helper flags')
 helper_build=binding['helper_build'] if binding else None;helper_runtime_config=None
 if args.runtime_model_helper:helper,helper_build=checked_runtime_helper(args.runtime_model_helper,args.runtime_model_build_sha256)
 if args.continue_session==args.session:raise ValueError('continuation must create a new session')
 with contextlib.ExitStack() as managed, open('/tmp/nrbridge-poc-gpu.lock','a') as lock:
  fcntl.flock(lock,fcntl.LOCK_EX|fcntl.LOCK_NB);safety=daily_preflight(lock,binding) if getattr(args,'daily',False) else preflight(lock,args if (getattr(args,'reboot_diagnostic',None) or getattr(args,'context_recovery',None)) else None,binding)
  for p in (*native_artifacts,assets/'eboot.bin',helper):
   if not p.is_file():raise ValueError('missing input '+str(p))
  run.mkdir(mode=0o700);signals.run=run
  provenance=run/'build-provenance';provenance.mkdir()
  for name in ('source-head.txt','source-diff.patch','artifacts.sha256'):
   shutil.copyfile(build/name,provenance/name)
  profile=run/'profile';profile.mkdir();continuation=None;player_saves=None;player_binding=None;player_save_source=None
  if player is not None:
   from player_saves import PlayerSaves
   player_saves=managed.enter_context(PlayerSaves(Path(player['user_dir'])))
   player_save_source=player_saves.prepare(profile/'user')
   player_binding=player_settings.write_snapshot(run,player)
  elif args.continue_session:continuation=copy_continuation(args.continue_session,profile)
  else:
   shutil.copytree(seed/'savedata',profile/'user/savedata')
   for item in json.loads((seed/'manifest.json').read_text())['files']:
    p=profile/'user/savedata'/item['relative']
    if p.is_symlink() or sha(p)!=item['sha256']:raise ValueError('private seed SHA mismatch')
  for name in ('logs','wine-prefix','cache','config','data','state'):(profile/name).mkdir()
  online_env,online_record,online_module=daily_online.prepare(args,build,profile)
  if online_module is not None:native_artifacts=[*native_artifacts,online_module]
  (profile/'bbport.ini').write_text('upscaler=dlss\npreset=1\naspect_ratio=16:9\noutput_res=1920x1080\njitter=1\nobject_motion=1\nsharpen=0\nshow_fps=1\n');(run/'pad.txt').write_text('')
  base_keys={'PATH','HOME','USER','LOGNAME','SHELL','LANG','LANGUAGE','LC_ALL','LC_CTYPE','TZ','TERM','DISPLAY','WAYLAND_DISPLAY','XAUTHORITY','XDG_RUNTIME_DIR','XDG_SESSION_TYPE','XDG_CURRENT_DESKTOP','DBUS_SESSION_BUS_ADDRESS','PULSE_SERVER','PIPEWIRE_REMOTE','SDL_AUDIODRIVER','SDL_VIDEODRIVER'}
  env={k:v for k,v in os.environ.items() if k in base_keys}
  env.update(BB_GAME_DIR=str(assets),BB_DATA_DIR=str(profile),BB_CONFIG=str(profile/'bbport.ini'),BB_USER_DIR=str(profile/'user'),BB_GPU_USER_DIR=str(profile/'user'),BB_COPY_GPU_BUFFERS='1',BB_PREBUILT='1',BB_PROBE=str(build/'bb-probe'),BB_STARTUP_CHECK='0',BB_MODS_ENABLED='0',BB_UPSCALER='dlss',BB_UPSCALE_PRESET='1',BB_DLSS='1',BB_JITTER='1',BB_FPS='uncap',BB_FPS_LIMIT='90',BB_FRAME_STATS='1',BB_FRAME_LOG=str(profile/'logs/frames.csv'),BB_FULLSCREEN='0',BB_SKIP_NETWORK_CHOICE='1',BB_LANGUAGE='1',BB_PC_MODEL='0',BB_SAVE_LOG='1',BB_IME_TEXT='NRTest',BB_PAD_FILE=str(run/'pad.txt'),BB_DUMP_DIR=str(run/'dump'),BB_PRESENT_DUMP_TRIGGER=str(run/'present-dump-trigger'),BB_PRESENT_DUMP_COUNT='0',LD_LIBRARY_PATH=str(sysroot_lib),DLSSNR_DISABLE='1',DLSSNR_AUTO_DOWNLOAD='0',VK_LOADER_LAYERS_DISABLE='~implicit~',MANGOHUD='0',DISABLE_MANGOHUD='1')
  if player is not None:
   if player['game_dir']!=str(assets):raise ValueError('selected player game/runtime binding drift')
   (profile/'bbport.ini').write_text(player['ini_text'])
   env.update(player['environment']);env.pop('BB_IME_TEXT',None)
  for name in ('config','cache','data','state'):env['XDG_'+name.upper()+'_HOME']=str(profile/name)
  nr={'BB_NR_NATIVE_ASYNC':'0','BB_NR_NATIVE_ASYNC_PRE_SR':'1','BB_NR_NATIVE_ASYNC_SCENE_FIRST':'1','BB_NR_NATIVE_ASYNC_PRE_SR_PLAY':'1','BB_NR_NATIVE_ASYNC_PRE_SR_SKY':'off','BB_NATIVE_PERSISTENT_HISTORY':'1' if args.sky=='persistent' else '0','BB_NR_NATIVE_ASYNC_PRE_SR_SR_HISTORY':'game_only','BB_NR_NATIVE_ASYNC_PRE_SR_SR_INPUT':'switching','BB_NR_NATIVE_ASYNC_SESSION':args.session,'BB_NR_NATIVE_ASYNC_MODE':args.mode,'BB_NR_NATIVE_ASYNC_RUN_DIR':str(run),'BB_NR_NATIVE_ASYNC_SCENE_ARM':str(run/'native-scene-arm'),'BB_NR_NATIVE_ASYNC_SOURCE_LIMIT':str(args.source_limit),'BB_NR_NATIVE_ORIGIN_RUN_DIR':str(run),'BB_NR_NATIVE_ORIGIN_HELPER':str(helper),'BB_NR_NATIVE_ORIGIN_WINE':str(ge_files/'lib/wine/x86_64-unix/wine'),'BB_NR_NATIVE_ORIGIN_PRELOADER':str(ge_files/'lib/wine/x86_64-unix/wine-preloader'),'BB_NR_NATIVE_ORIGIN_PREFIX':str(profile/'wine-prefix'),'BB_NR_NATIVE_ORIGIN_WINEDEBUG':'-all,warn+vulkan,err+vulkan','BB_NR_NATIVE_ORIGIN_MODE':args.mode}
  if getattr(args,'pm4_cold_trace',False):nr.update(BB_NR_PM4_COLD_TRACE='1',BB_NR_RESOURCE_TRACE='1')
  if getattr(args,'pm4_producer_context',False):nr['BB_NR_PM4_PRODUCER_CONTEXT']='1'
  if args.runtime_model_helper or binding:nr['BB_NR_RUNTIME_MODEL']='1'
  if binding:
   helper_config=run/'helper-runtime.json';atomic(helper_config,runtime_inputs.helper_config(binding));helper_runtime_config={'path':str(helper_config),'sha256':sha(helper_config)}
   nr.update(BB_NR_NATIVE_ORIGIN_RUNTIME_CONFIG=str(helper_config),BB_NR_NATIVE_ORIGIN_GE_FILES=str(ge_files),BB_NR_NATIVE_ORIGIN_NGX_DIR=binding['config']['ngx_dir'])
  if args.history_diagnostic:
   if args.sky!='persistent' or args.mode!='resident':raise ValueError('history diagnostic requires persistent resident mode')
   nr.update(BB_NATIVE_HISTORY_DIAGNOSTIC='1',BB_NATIVE_HISTORY_DIAGNOSTIC_FRAMES='3',BB_NATIVE_HISTORY_DIAGNOSTIC_AFTER_CANDIDATE=str(args.diagnostic_after))
  if native_mouse_scope.requested(args):nr['BB_NATIVE_MOUSE']='1'
  else:nr['BB_NATIVE_MOUSE']='0'
  if getattr(args,'daily',False):nr.update(BB_NR_DAILY='1',NRBRIDGE_HOME=str(ROOT))
  if ui_start.enabled(args,build):nr[ui_start.ENVIRONMENT]=str(run/ui_start.NAME)
  if getattr(args,'daily',False):
   if player is None:env['BB_FRAME_STATS']='0'
   env.pop('BB_FRAME_LOG',None)
  env.update(nr);env.update(online_env);command=['bash',str(WT/'run.sh')]
  if getattr(args,'experimental_fg',None):
   # SR-only and explicit Native HDR integration retain the managed PreSR retirement audit.
   env.update(BB_FRAME_GEN='1',BB_FRAME_GEN_AUDIT='1',BB_PRESENT_MODE='Immediate',
              BB_FRAME_GEN_CONTROL=str(run/'fg-control'),
              BB_SEQUENCE_DUMP_TRIGGER=str(run/'fg-sequence-trigger'),BB_FPS_LIMIT='60')
   if getattr(args,'fg_native',False):env['BB_FRAME_GEN_NATIVE']='1'
   env['BB_FRAME_GEN_HUDLESS']='1' if args.experimental_fg=='fsr31-hudless' else '0'
   env['BB_FRAME_GEN_BACKEND']='fsr316' if args.experimental_fg=='fsr316' else 'fsr31'
   if args.experimental_fg=='dlssg-capture':
    env.update(BB_FRAME_GEN_BACKEND='dlssg-capture',BB_DLSSG_CAPTURE_DIR=str(run/'dlssg-input'),
               BB_DLSSG_CAPTURE_TRIGGER=str(run/'dlssg-capture-trigger'))
   env['BB_PRESET_FILE']=str(run/'sr-control')
   (run/'dump').mkdir()
   if getattr(args,'fg_validation',False):
    validation=WT/'out/deps/vulkan-validation/usr'
    env.update(BB_VK_VALIDATION='1',BB_VK_VALIDATION_SYNC='1',
               VK_LAYER_PATH=str(validation/'share/vulkan/explicit_layer.d'),
               LD_LIBRARY_PATH=str(validation/'lib')+':'+str(sysroot_lib))
  if getattr(args,'smooth_motion',False):env.update(smooth_motion.environment(run))
  if getattr(args,'experimental_fg',None)=='dlssg':
   env.update(BB_FRAME_GEN_BACKEND='dlssg',BB_DLSSG_STREAM_DIR='pending')
   env.update(dlssg_backend.prepare(args,build,run,binding,globals()))
  atomic(run/'manifest.json',{'schema':'nr-dev-session-v1','daily':getattr(args,'daily',False),'session':args.session,'native_executable':str(build/'bb-probe'),'native_build':str(build),'native_build_provenance':build_provenance,'artifacts':{str(p):sha(p) for p in (*native_artifacts,helper)},'environment':daily_online.public_environment(env),'redacted_environment':sorted(daily_online.SECRET_ENV & env.keys()),'online':online_record,'command':command,'host_safety':safety,'prelaunch_gpu_holders':gpu_holders(),'authorization_basis':'user full isolated persistent visual history development stage 2026-10-15; no legacy launch quota','mode':args.mode,'sky':args.sky,'planned_seconds':args.seconds,'watchdog_seconds':args.watchdog_seconds,'close_grace_seconds':args.close_grace,'continuation':continuation,'helper_build_manifest':helper_build,'runtime_model':bool(args.runtime_model_helper or binding),'runtime_inputs':binding,'helper_runtime_config':helper_runtime_config})
  manifest=json.loads((run/'manifest.json').read_text());manifest['nr_enabled']=not getattr(args,'sr_only',False);manifest['smooth_motion']=getattr(args,'smooth_motion',False)
  if player_binding is not None:manifest.update(player_settings=player_binding,player_save_source=player_save_source)
  atomic(run/'manifest.json',manifest)
  if getattr(args,'experimental_fg',None)=='dlssg':dlssg_backend.validate_manifest(run,manifest)
  with (run/'game.log').open('w') as log:
   daily=getattr(args,'daily',False);capture=None
   if daily:
    record_pending_daily_retirement(run)
    from daily_log import GameLog
    capture=GameLog(run,None)
   if not signals.before_spawn():return 3
   if daily:
    boot_guard=daily_session_guard.SessionGuard()
    boot_receipt=boot_guard.begin(ROOT,args.session)
   process=subprocess.Popen(command,cwd=WT,env=env,stdout=subprocess.PIPE if daily else log,stderr=subprocess.STDOUT,start_new_session=True)
   if daily:
    capture.pipe=process.stdout;capture.start()
   atomic(run/'runner.json',{'runner':live(os.getpid()),'launcher':live(process.pid)});schedule=SessionSchedule(time.monotonic(),args.seconds,args.watchdog_seconds,args.close_grace,getattr(args,'daily',False));held=False;sample_at=0;planned_retry_at=0
   print(str(run),flush=True)
   while process.poll() is None:
    signals.flush()
    failure=daily_failure_reason(run,capture) if daily and not held else None
    if failure:
     held=True;atomic(run/'result.json',{'status':'HOLD','reason':failure+'; ordinary close requested; GPU lock retained','native_lastuse_proof':False})
     if not signals.close_requested:
      signals.close_requested=True
      try:control(argparse.Namespace(command='stop',session=args.session))
      except Exception as error:atomic(run/'failure-close-error.json',{'error':str(error)})
    if signals.interrupted and not signals.close_requested:
     atomic(run/'result.json',{'status':'HOLD','reason':'supervisor interrupted; waiting actual child exit with GPU lock retained','supervisor_interrupted':True,'received_signals':signals.received})
     signals.request_close_once(args.session)
    if time.monotonic()>=sample_at:
     sample_at=time.monotonic()+1
     try:
      sample=sample_gpu_memory()
      if daily:atomic(run/'gpu-memory-latest.json',sample)
      else:
       with (run/'gpu-memory.jsonl').open('a') as memory:memory.write(json.dumps(sample)+'\n')
     except (OSError,subprocess.SubprocessError):pass
    now=time.monotonic()
    if (run/'stop-request.json').exists() and schedule.close_started is None:
     stop=json.loads((run/'stop-request.json').read_text());schedule.note_close(stop.get('monotonic_ns',time.monotonic_ns())/1e9)
    if schedule.close_due(now) and not signals.interrupted and not signals.close_requested and now>=planned_retry_at:
     schedule.note_close(now);planned_retry_at=now+1
     if not (run/'planned-close.json').exists():atomic(run/'planned-close.json',{'status':'PLANNED_NORMAL_CLOSE','monotonic_ns':time.monotonic_ns(),'scope':'ordinary planned duration reached; not GPU failure or retirement proof'})
     try:
      if not (run/'stop-request.json').exists():control(argparse.Namespace(command='stop',session=args.session))
      signals.close_requested=True
     except Exception as error:
      with (run/'planned-close-attempts.jsonl').open('a') as events:events.write(json.dumps({'monotonic_ns':time.monotonic_ns(),'error':str(error)})+'\n')
    if schedule.expired(now) and not held:
     held=True;atomic(run/'result.json',{'status':'HOLD','reason':'watchdog expired; normal close requested; no kill or GPU completion claim'})
     if not signals.close_requested:
      signals.close_requested=True
      if not (run/'stop-request.json').exists():
       try:control(argparse.Namespace(command='stop',session=args.session))
       except Exception as e:atomic(run/'watchdog-close-error.json',{'error':str(e)})
    time.sleep(.25)
   if capture:capture.finish()
   signals.flush()
   text=(run/'game.log').read_text(errors='replace');residual=holders()
   from peer_retirement import audit_session
   termination={'returncode':process.returncode,'residuals':residual,'watchdog_expired':held}
   try:
    retirement=audit_session(run,termination)
    normal=True;proof=True
   except (OSError,ValueError,KeyError,TypeError) as error:
    retirement={'retirement':'NOT_PROVED','error':str(error)};normal=False;proof=False
   atomic(run/'retirement-audit.json',retirement)
   result={'status':'PASS_NORMAL_TEARDOWN' if normal and not held and not signals.interrupted else 'HOLD','returncode':process.returncode,'native_lastuse_proof':proof,'residuals':residual,'watchdog_expired':held,'supervisor_interrupted':signals.interrupted,'received_signals':signals.received,'quality':'NOT_ACCEPTED','performance':'separate actual Present analysis required'}
   atomic(run/'result.json',result)
   from metrics import measure
   atomic(run/'present-metrics.json',measure(run))
   if player_saves is not None:
    result['player_save_sync']=player_saves.commit(profile/'user',result)
    atomic(run/'result.json',result)
    if result['player_save_sync']['status']=='error':print('Save sync failed; private progress retained: '+str(run/'profile/user/savedata'),flush=True)
   if result['status']=='PASS_NORMAL_TEARDOWN':
    if getattr(args,'experimental_fg',None)=='dlssg':
     try:result['dlssg_cpu_slot_cleanup']=dlssg_backend.cleanup_cpu_slot(run,json.loads((run/'manifest.json').read_text()),result)
     except (OSError,ValueError) as error:result['dlssg_cpu_slot_cleanup']={'status':'RETAINED_CPU_FILES','reason':str(error)}
    result['generated_inputs_cleanup']=retire_generated_inputs(run,result)
    atomic(run/'result.json',result)
   if daily:
    try:
     if result['status']=='PASS_NORMAL_TEARDOWN':boot_guard.retire(boot_receipt,result)
     else:boot_guard.hold(boot_receipt)
    except (OSError,ValueError) as error:
     result.update(status='HOLD',boot_session_receipt_error=str(error));normal=False
    atomic(run/'result.json',result)
   if normal and not held and not signals.interrupted:
    return 4 if result.get('player_save_sync',{}).get('status')=='error' else 0
   return 3

def parse_args(argv=None):
 p=argparse.ArgumentParser(description=__doc__);p.add_argument('--daily',action='store_true');p.add_argument('--joystick-mouse',action='store_true');p.add_argument('command',choices=['preflight','start','scene-ready','start-helper','arm','stop','focus','input','dump']);p.add_argument('session',nargs='?');p.add_argument('--mode',choices=['resident','skipEvaluate'],default='skipEvaluate');p.add_argument('--sky',choices=['off','persistent'],default='off');p.add_argument('--seconds',type=int,default=480,help='planned normal-close time, including preparation');p.add_argument('--watchdog-seconds',type=int,default=600);p.add_argument('--close-grace',type=int,default=120);p.add_argument('--continue-session');p.add_argument('--source-limit',type=int,default=36000);p.add_argument('--buttons',nargs='*',default=[]);p.add_argument('--hold',type=float,default=.2);[p.add_argument('--'+axis,type=int,default=128) for axis in ('lx','ly','rx','ry')];p.add_argument('--native-build',help='build/native or verified build/baselines/<full commit>');p.add_argument('--local-inputs',help='opt-in versioned ROOT/localinputs config; default remains legacy');p.add_argument('--runtime-model-helper');p.add_argument('--runtime-model-build-sha256');p.add_argument('--context-recovery',help='explicit isolated current-context diagnostics; historical failures remain open');p.add_argument('--reboot-diagnostic',help='one-session post-reboot SR-only observation; never resolves old HOLD');p.add_argument('--native-mouse',action='store_true',help='default-off camera candidate; exact isolated scope and cold trace required');p.add_argument('--pm4-cold-trace',action='store_true',help='bounded CPU producer tracing; exclude run from hot performance');p.add_argument('--pm4-producer-context',action='store_true',help='bounded caller/template context; isolated cold diagnostic only');p.add_argument('--history-diagnostic',action='store_true');p.add_argument('--diagnostic-after',type=int,default=3)
 daily_online.add_arguments(p)
 p.add_argument('--experimental-fg',choices=['fsr31','fsr31-hudless','fsr316','dlssg-capture','dlssg'],help='FSR x2, DLSSG CPU x2, or finite DLSSG input capture')
 p.add_argument('--dlssg-model',help='explicit existing DLSSG DLL for Experimental CPU bridge')
 p.add_argument('--dlssg-prefix-session',default='m2-sm-1080q-r1',help='reuse one normally retired private Wine prefix')
 p.add_argument('--fg-native',action='store_true',help='experimental 1080p Native HDR SR + FG; allows NR integration')
 p.add_argument('--smooth-motion',action='store_true',help='exclusive driver-level NVIDIA Smooth Motion experiment')
 p.add_argument('--fg-validation',action='store_true',help='explicit FG core/sync validation')
 player_settings.add_arguments(p)
 a=p.parse_args(argv)
 try:
  configure_daily_args(a)
  validate_pm4_producer_context(a)
 except ValueError as error:p.error(str(error))
 if a.context_recovery and a.reboot_diagnostic:p.error('choose one isolated diagnostic scope')
 if a.command!='preflight' and not a.session:p.error('session required')
 if ((not a.daily and not 1<=a.seconds<a.watchdog_seconds<=600)
     or not 1<=a.close_grace<=600
     or not ((a.daily and a.source_limit==0) or 1<=a.source_limit<=216000)
     or not 1<=a.diagnostic_after<=1000000):p.error('finite diagnostic budget or daily mode with positive close grace required')
 return a

def main():
 a=parse_args()
 if a.command=='preflight':
  with open('/tmp/nrbridge-poc-gpu.lock','a') as lock:
   fcntl.flock(lock,fcntl.LOCK_EX|fcntl.LOCK_NB)
   binding=runtime_inputs.load(a.local_inputs,ROOT) if a.local_inputs else None
   safety=daily_preflight(lock,binding) if a.daily else preflight(lock,runtime_binding=binding)
   print(json.dumps(safety,indent=2))
  return 0
 return launch(a) if a.command=='start' else control(a)
if __name__=='__main__':raise SystemExit(main())
