#!/usr/bin/env python3
"""Start a private NR playtest using the verified isolated runner."""
import argparse,datetime,json,signal,subprocess,sys,time,select
from pathlib import Path
import runner
import native_mouse_scope
DEFAULT_HELPER=runner.ROOT/'build/helper-runtime-model/06ac7e6b7260172d/wine_runtime_model.exe'
DEFAULT_HELPER_BUILD_SHA='e7ab371cfb5d4f2fd799336065bdb69953106c3c731498a09f7609947aaa1680'


def parse_args(argv=None):
 p=argparse.ArgumentParser(description='NR 日常／隔离入口；进入可控制场景后确认开启 NR。')
 inputs=p.add_mutually_exclusive_group()
 inputs.add_argument('--local-inputs',help='显式 ROOT/localinputs 版本化配置；默认保留legacy路径')
 inputs.add_argument('--legacy-inputs',action='store_true',help='显式选择原Helper与61项legacy pins，供旧基线回滚')
 p.add_argument('--daily',action='store_true',help='日常游玩，无固定时长；默认原生鼠标')
 p.add_argument('--joystick-mouse',action='store_true',help='兼容摇杆模拟鼠标')
 p.add_argument('--native-build',help='build/native or verified build/baselines/<full commit>')
 p.add_argument('--experimental-fg',choices=['fsr31','fsr31-hudless','fsr316','dlssg-capture','dlssg'],help='FSR x2, DLSSG x2, or finite input capture')
 p.add_argument('--dlssg-model',help='explicit existing DLSSG DLL for Experimental DLSSG bridge')
 p.add_argument('--dlssg-transport',choices=['files-v1','mapped-v1','gpu-dmabuf-v1'],default='mapped-v1',help='Experimental payload transport; GPU mailbox is opt-in')
 p.add_argument('--dlssg-pipeline',choices=['sync-v1','submit-ready-v1','late-ready-v1','consumer-ready-v1','consumer-queue-v1'],default='sync-v1',help='Async submission prerequisite; sync-v1 preserves the verified bridge')
 p.add_argument('--dlssg-helper-wake',choices=['poll-v1','notify-v1'],default='poll-v1',help='GPU Helper control wake; poll-v1 retains the validated baseline')
 p.add_argument('--dlssg-prefix-session',default='m2-sm-1080q-r1',help='reuse normally retired private Wine prefix')
 p.add_argument('--fg-native',action='store_true',help='experimental 1080p Native HDR SR + NR + FG route')
 p.add_argument('--smooth-motion',action='store_true',help='driver-level NVIDIA interpolation; excludes application FG')
 p.add_argument('--fg-fps-limit',type=int,default=None,help='explicit experiment real-frame cap: 0 uncapped; default 60')
 p.add_argument('--fg-profile',action='store_true',help='bounded FG CPU and GPU stage timing')
 p.add_argument('--fg-validation',action='store_true',help='Vulkan core/sync validation for the FG experiment')
 p.add_argument('--session',default='play-'+datetime.datetime.now().strftime('%Y%m%d-%H%M%S'))
 p.add_argument('--seconds',type=int,default=480,help='计划正常关闭时间（含准备时间）')
 p.add_argument('--watchdog-seconds',type=int,default=600)
 p.add_argument('--source-limit',type=int,default=36000)
 p.add_argument('--context-recovery',help='已有协调方scope；在启动监督进程前核对精确请求')
 p.add_argument('--native-mouse',action='store_true',help='显式原生鼠标候选；须scope与cold trace匹配')
 p.add_argument('--pm4-cold-trace',action='store_true',help='透传已有冷诊断；整run排除热性能评分')
 p.add_argument('--pm4-producer-context',action='store_true',help='透传已有生产者冷诊断；须匹配scope')
 p.add_argument('--continue-session',help='仅续用已正常退休的新dev session私有存档和NR配置')
 p.set_defaults(mode='resident',sky='persistent',history_diagnostic=False)
 runner.daily_online.add_arguments(p)
 runner.player_settings.add_arguments(p)
 a=p.parse_args(argv)
 try:runner.configure_daily_args(a)
 except ValueError as error:p.error(str(error))
 if not a.daily and not 1<=a.seconds<a.watchdog_seconds<=600:p.error('require 1 <= seconds < watchdog-seconds <= 600')
 if not (a.daily and a.source_limit==0 or 1<=a.source_limit<=216000):p.error('source-limit must be 1..216000')
 if a.pm4_producer_context and not (a.context_recovery and a.pm4_cold_trace):p.error('producer context requires context-recovery and pm4-cold-trace')
 return a


def launch_command(a):
 """Read-only checks before spawning the supervisor; Runner rechecks under lock."""
 if getattr(a,'launcher_settings',None):a.local_inputs=runner.player_settings.runtime_for_player(a,runner.ROOT)
 native_mouse_scope.requested(a)
 run=runner.checked_run(a.session)
 if run.exists():raise ValueError('session already exists; use a fresh name')
 record=None
 if a.context_recovery:
  import context_recovery as recovery
  _,record=recovery._record(runner,a.context_recovery)
  recovery.need(record.get('session')==a.session,'exact unused session required')
  recovery.validate_scope(record,recovery.request(a),Path('/proc/sys/kernel/random/boot_id').read_text().strip(),runner.held_run_snapshot())
  recovery._trace_launch(record,a)
 build,_,_=runner.checked_native_build(a.native_build or runner.ROOT/'build/native')
 runner.check_fg_capability(a,build)
 a.ingame_nr_start=runner.ui_start.enabled(a,build)
 binding=None
 if a.local_inputs:
  binding=runner.runtime_inputs.load(a.local_inputs,runner.ROOT)
  runner.runtime_inputs.native_capability(build)
  if a.daily:runner.runtime_inputs.daily_capability(build,binding)
  helper=Path(binding['config']['helper']['path']);helper_build=binding['helper_build']
 else:helper,helper_build=runner.checked_runtime_helper(DEFAULT_HELPER,DEFAULT_HELPER_BUILD_SHA)
 if record is not None:
  recovery.need(record['native_build']['path']==str(build)
                and record.get('runtime_inputs')==binding
                and record['helper']['path']==str(helper)
                and record['helper']['build_manifest']==helper_build,'launch build/runtime inputs drift')
  recovery._identities(runner,record)
  recovery._predecessor(runner,record)
 if a.continue_session:runner.continuation_source(a.continue_session)
 command=[sys.executable,str(Path(__file__).with_name('runner.py')),'start',a.session,'--mode',a.mode,'--sky',a.sky,'--seconds',str(a.seconds),'--watchdog-seconds',str(a.watchdog_seconds),'--source-limit',str(a.source_limit)]
 if a.local_inputs:command+=['--local-inputs',a.local_inputs]
 else:command+=['--runtime-model-helper',str(DEFAULT_HELPER),'--runtime-model-build-sha256',DEFAULT_HELPER_BUILD_SHA]
 if a.native_build:command+=['--native-build',a.native_build]
 if a.continue_session:command+=['--continue-session',a.continue_session]
 if a.context_recovery:command+=['--context-recovery',a.context_recovery]
 if a.online_pool!='offline':command+=['--online-pool',a.online_pool,'--online-account-config',a.online_account_config]
 if a.bbsc:command+=['--bbsc']
 for name in ('launcher_settings','launcher_ini','player_data_dir'):
  value=getattr(a,name,None)
  if value:command+=['--'+name.replace('_','-'),value]
 if getattr(a,'sr_only',False):command+=['--sr-only']
 if getattr(a,'experimental_fg',None):command+=['--experimental-fg',a.experimental_fg]
 if getattr(a,'fg_native',False):command+=['--fg-native']
 if getattr(a,'fg_validation',False):command+=['--fg-validation']
 if getattr(a,'fg_profile',False):command+=['--fg-profile']
 if getattr(a,'fg_fps_limit',None) is not None:command+=['--fg-fps-limit',str(a.fg_fps_limit)]
 if getattr(a,'smooth_motion',False):command+=['--smooth-motion']
 if getattr(a,'dlssg_model',None):command+=['--dlssg-model',a.dlssg_model,'--dlssg-prefix-session',a.dlssg_prefix_session,'--dlssg-transport',a.dlssg_transport,'--dlssg-helper-wake',a.dlssg_helper_wake,'--dlssg-pipeline',a.dlssg_pipeline]
 for flag in ('daily','joystick_mouse','pm4_cold_trace','pm4_producer_context','native_mouse'):
  if getattr(a,flag):command+=['--'+flag.replace('_','-')]
 return run,command


def wait_for_scene(a, run, child, stream=None):
 """One user attestation, from game UI or the existing launcher/CLI pipe."""
 stream=sys.stdin if stream is None else stream
 ingame=getattr(a,'ingame_nr_start',False)
 while child.poll() is None:
  if ingame and (run/runner.ui_start.NAME).exists():
   manifest=json.loads((run/'manifest.json').read_text())
   runner.validate_daily_manifest(run,manifest)
   if runner.ui_start.requested(run,manifest):
    # Same PID/starttime, pinned build and actual loaded libraries are checked
    # again by every existing scene-ready/start-helper/arm control operation.
    runner.current_game(run)
    return 'game'
  if stream is not None and select.select([stream],[],[],.2)[0]:
   if stream.readline():return 'launcher'
   if not ingame:raise EOFError('terminal input closed')
   stream=None  # A desktop launch needs no terminal to keep waiting for F8.
  elif stream is None:time.sleep(.2)
 return None


def main():
 a=parse_args()
 run,command=launch_command(a)
 child=subprocess.Popen(command,start_new_session=True)
 def stop(signum,frame):raise KeyboardInterrupt
 signal.signal(signal.SIGTERM,stop)
 try:
  if getattr(a,'launcher_settings',None):
   print('\n使用启动器选择的存档；正常退休后安全同步，失败时保留本轮私人副本。Space/Cross 继续；WASD/鼠标或手柄操作。',flush=True)
  else:print('\n游戏使用独立私有存档'+('，续用 '+a.continue_session if a.continue_session else '（新NRTest seed）')+'。Space/Cross 继续；WASD/鼠标或手柄操作。',flush=True)
  print('F8：首次按下启用 NR／后续切换 NR 效果；Delete：详细设置。',flush=True) if getattr(a,'ingame_nr_start',False) else print('F8：已启用后切换 NR 效果；Delete：详细设置。',flush=True)
  if a.daily:print('日常模式：无固定游戏时长；正常关闭窗口退出。',flush=True)
  else:print(f'计划 {a.seconds} 秒后请求正常关闭；独立watchdog为{a.watchdog_seconds}秒，关闭仍需真实GPU退休。',flush=True)
  if getattr(a,'sr_only',False):
   print('SR-only：不会启动 Helper 或 NR。',flush=True)
   return child.wait()
  if getattr(a,'ingame_nr_start',False):prompt='进入可操作场景后直接按 F8 开启 NR；也可点击启动器的开启 NR 按钮。'
  elif getattr(a,'launcher_settings',None):prompt='进入可操作场景后，点击启动器的开启 NR 按钮。'
  else:prompt='进入可控制的实际游戏场景后，在本终端按 Enter 开启 NR。'
  print('NR_SCENE_READY_WAIT: '+prompt,flush=True)
  route=wait_for_scene(a,run,child)
  if route is None:return child.returncode
  print('NR_SCENE_READY_ACCEPTED: 场景确认已接收，正在启动 NR。',flush=True)
  runner.control(argparse.Namespace(command='scene-ready',session=a.session))
  runner.control(argparse.Namespace(command='start-helper',session=a.session))
  ready=run/('async-fd-retired-'+a.session+'-g0.json');deadline=time.monotonic()+120
  while not ready.exists():
   if child.poll() is not None:return child.returncode
   if time.monotonic()>deadline:raise RuntimeError('Helper readiness timeout; requesting normal close')
   time.sleep(.1)
  runner.control(argparse.Namespace(command='arm',session=a.session))
  if route!='game':
   try:runner.control(argparse.Namespace(command='focus',session=a.session))
   except Exception as error:print('NR已开启；自动切回游戏窗口失败，请手动切回：'+str(error),file=sys.stderr,flush=True)
  print('NR 已开启。请保持游戏窗口在前台；正常关闭窗口结束，记录和私有存档保留于 '+str(run),flush=True)
  return child.wait()
 except (KeyboardInterrupt,EOFError,Exception) as error:
  print('结束试玩准备／监督：'+str(error),file=sys.stderr,flush=True)
  if child.poll() is None:
   try:runner.control(argparse.Namespace(command='stop',session=a.session))
   except Exception:
    # Signal only the managed supervisor. Its handler holds the GPU lock and
    # requests normal close; it never treats signal/kill as GPU retirement.
    child.send_signal(signal.SIGTERM)
   return child.wait()
  return child.returncode
if __name__=='__main__':raise SystemExit(main())
