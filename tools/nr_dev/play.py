#!/usr/bin/env python3
"""Start a private NR playtest using the verified isolated runner."""
import argparse,datetime,json,signal,subprocess,sys,time,select
from pathlib import Path
import runner
DEFAULT_HELPER=runner.ROOT/'build/helper-runtime-model/06ac7e6b7260172d/wine_runtime_model.exe'
DEFAULT_HELPER_BUILD_SHA='e7ab371cfb5d4f2fd799336065bdb69953106c3c731498a09f7609947aaa1680'


def main():
 p=argparse.ArgumentParser(description='私有 NR 试玩入口；不使用正式存档。进入可控制场景后，在本终端按 Enter 开启 NR。')
 p.add_argument('--native-build',help='build/native or verified build/baselines/<full commit>')
 p.add_argument('--session',default='play-'+datetime.datetime.now().strftime('%Y%m%d-%H%M%S'))
 p.add_argument('--seconds',type=int,default=480,help='计划正常关闭时间（含准备时间；小于600秒）');p.add_argument('--continue-session',help='仅续用已正常退休的新dev session私有存档和NR配置')
 a=p.parse_args()
 if not 1<=a.seconds<600:p.error('planned seconds must be 1..599; independent watchdog remains600')
 if a.native_build:runner.checked_native_build(a.native_build)
 runner.checked_runtime_helper(DEFAULT_HELPER,DEFAULT_HELPER_BUILD_SHA)
 if a.continue_session:runner.continuation_source(a.continue_session)
 run=runner.checked_run(a.session)
 if run.exists():p.error('session already exists; use a fresh name')
 command=[sys.executable,str(Path(__file__).with_name('runner.py')),'start',a.session,'--mode','resident','--sky','persistent','--seconds',str(a.seconds),'--watchdog-seconds','600','--runtime-model-helper',str(DEFAULT_HELPER),'--runtime-model-build-sha256',DEFAULT_HELPER_BUILD_SHA]
 if a.native_build:command+=['--native-build',a.native_build]
 if a.continue_session:command+=['--continue-session',a.continue_session]
 child=subprocess.Popen(command,start_new_session=True)
 def stop(signum,frame):raise KeyboardInterrupt
 signal.signal(signal.SIGTERM,stop)
 try:
  print('\n游戏使用独立私有存档'+('，续用 '+a.continue_session if a.continue_session else '（新NRTest seed）')+'。Space/Cross 继续；WASD/鼠标或手柄操作。',flush=True)
  print('F8：显示旁路；F9：同源画面捕获；Delete：NR控制菜单。加载的模型Style/Intensity仍需显式Apply。',flush=True)
  print(f'计划 {a.seconds} 秒后请求正常关闭；独立watchdog为600秒，关闭仍需真实GPU退休。',flush=True)
  print('进入可控制的实际游戏场景后，回到本终端按 Enter 开启 NR：',flush=True)
  while child.poll() is None:
   if select.select([sys.stdin],[],[],.2)[0]:
    if not sys.stdin.readline():raise EOFError('terminal input closed')
    break
  if child.poll() is not None:return child.returncode
  runner.control(argparse.Namespace(command='scene-ready',session=a.session))
  runner.control(argparse.Namespace(command='start-helper',session=a.session))
  ready=run/('async-fd-retired-'+a.session+'-g0.json');deadline=time.monotonic()+120
  while not ready.exists():
   if child.poll() is not None:return child.returncode
   if time.monotonic()>deadline:raise RuntimeError('Helper readiness timeout; requesting normal close')
   time.sleep(.1)
  runner.control(argparse.Namespace(command='arm',session=a.session))
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
