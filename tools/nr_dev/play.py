#!/usr/bin/env python3
"""Start a private NR playtest using the verified isolated runner."""
import argparse,datetime,json,signal,subprocess,sys,time
from pathlib import Path
import runner

def main():
 p=argparse.ArgumentParser(description='私有 NR 试玩入口；不使用正式存档。进入可控制场景后，在本终端按 Enter 开启 NR。')
 p.add_argument('--session',default='play-'+datetime.datetime.now().strftime('%Y%m%d-%H%M%S'))
 p.add_argument('--seconds',type=int,default=600,help='本轮有限测试时长（1–600秒，含准备时间）')
 a=p.parse_args()
 if not 1<=a.seconds<=600:p.error('seconds must be 1..600 (current Helper service capacity)')
 run=runner.checked_run(a.session)
 if run.exists():p.error('session already exists; use a fresh name')
 command=[sys.executable,str(Path(__file__).with_name('runner.py')),'start',a.session,'--mode','resident','--sky','persistent','--seconds',str(a.seconds)]
 child=subprocess.Popen(command,start_new_session=True)
 def stop(signum,frame):raise KeyboardInterrupt
 signal.signal(signal.SIGTERM,stop)
 try:
  print('\n游戏使用独立 NRTest 存档。Space/Cross 继续；WASD/鼠标或手柄操作。',flush=True)
  input('进入可控制的实际游戏场景后，回到本终端按 Enter 开启 NR：')
  if child.poll() is not None:return child.returncode
  runner.control(argparse.Namespace(command='scene-ready',session=a.session))
  runner.control(argparse.Namespace(command='start-helper',session=a.session))
  ready=run/('async-fd-retired-'+a.session+'-g0.json');deadline=time.monotonic()+60
  while not ready.exists():
   if child.poll() is not None:return child.returncode
   if time.monotonic()>deadline:raise RuntimeError('Helper readiness timeout; requesting normal close')
   time.sleep(.1)
  runner.control(argparse.Namespace(command='arm',session=a.session))
  print('NR 已开启。请正常关闭游戏窗口结束；记录和私有存档保留于 '+str(run),flush=True)
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
