# SPDX-License-Identifier: GPL-2.0-or-later
"""Bounded X11 capture of the exact managed game PID, never the desktop."""
import argparse,ctypes as C,ctypes.util,json,subprocess,sys,os,time,re
import runner
from pathlib import Path
p=argparse.ArgumentParser(description=__doc__);p.add_argument('session');p.add_argument('output');p.add_argument('--seconds',type=float,default=12);a=p.parse_args()
if not 1<=a.seconds<=30 or not re.fullmatch(r'[A-Za-z0-9][A-Za-z0-9_.-]{0,63}\.(?:png|mp4)',a.output):p.error('bounded 1..30 seconds and PNG/MP4 basename required')
run=runner.checked_run(a.session);manifest=json.loads((run/'manifest.json').read_text());runner.validate_daily_manifest(run,manifest)
guard=runner.daily_session_guard.SessionGuard().read();assert guard and guard['state']=='pending' and guard['session']==a.session and guard['root']==str(runner.ROOT)
identity=runner.current_game(run);pid=identity['pid']
x=C.CDLL(ctypes.util.find_library('X11'));D=C.c_void_p;U=C.c_ulong
x.XOpenDisplay.argtypes=[C.c_char_p];x.XOpenDisplay.restype=D
x.XDefaultRootWindow.argtypes=[D];x.XDefaultRootWindow.restype=U
x.XQueryTree.argtypes=[D,U,C.POINTER(U),C.POINTER(U),C.POINTER(C.POINTER(U)),C.POINTER(C.c_uint)]
x.XInternAtom.argtypes=[D,C.c_char_p,C.c_int];x.XInternAtom.restype=U
x.XGetWindowProperty.argtypes=[D,U,U,C.c_long,C.c_long,C.c_int,U,C.POINTER(U),C.POINTER(C.c_int),C.POINTER(U),C.POINTER(U),C.POINTER(C.POINTER(C.c_ubyte))]
x.XFree.argtypes=[C.c_void_p];x.XCloseDisplay.argtypes=[D]
d=x.XOpenDisplay(None);assert d
root=U();parent=U();children=C.POINTER(U)();count=C.c_uint()
assert x.XQueryTree(d,x.XDefaultRootWindow(d),C.byref(root),C.byref(parent),C.byref(children),C.byref(count))
ids=[children[i] for i in range(count.value)]
if children:x.XFree(children)
matches=[]
for w in ids:
 atom=x.XInternAtom(d,b'_NET_WM_PID',True);kind=U();fmt=C.c_int();n=U();remain=U();buf=C.POINTER(C.c_ubyte)()
 rc=x.XGetWindowProperty(d,w,atom,0,1,False,6,C.byref(kind),C.byref(fmt),C.byref(n),C.byref(remain),C.byref(buf))
 try:
  if not rc and kind.value==6 and fmt.value==32 and n.value==1 and buf and C.cast(buf,C.POINTER(U))[0]==pid:matches.append(w)
 finally:
  if buf:x.XFree(buf)
x.XCloseDisplay(d);assert len(matches)==1,matches
out=run/a.output;assert not out.exists()
cmd=['ffmpeg','-hide_banner','-loglevel','error','-f','x11grab','-window_id',str(matches[0]),'-draw_mouse','0','-framerate','30','-i',os.environ['DISPLAY']]
if out.suffix=='.png':cmd+=['-frames:v','1']
else:cmd+=['-t',str(a.seconds),'-vf','scale=960:-2','-c:v','libx264','-preset','ultrafast','-crf','18','-pix_fmt','yuv420p']
record=dict(game=identity,window=matches[0],requested_monotonic_ns=time.monotonic_ns(),scope='X11 game-window capture, wall clock, not exact NR/Present source')
with out.with_suffix('.capture.json').open('x') as f:json.dump(record,f,indent=2)
subprocess.run(cmd+[str(out)],check=True,timeout=a.seconds+15)
