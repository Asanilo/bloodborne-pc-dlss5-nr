#!/usr/bin/env python3
"""Short recorded NR motions in an already admitted private gameplay session.

Does not start a game/Helper, choose a player save, or create a GPU context.
Use a prepared safe location: the routine cannot identify vegetation/highlights.
"""
import argparse,ctypes,ctypes.util,json,math,re,signal,time
from pathlib import Path
import runner
PATTERNS={
 'run-stop':[(1.,''),(1.5,'ly=0 circle'),(3.,''),(1.5,'ly=255'),(3.,'')],
 'static':[(6.,'')],
 'turn':[(1.,''),(.35,'mdx=16'),(1.,''),(.35,'mdx=-16'),(2.,'')],
 'walk':[(1.,''),(.65,'ly=70'),(1.,''),(.65,'ly=186'),(2.,'')],
 'dodge':[(1.,''),(.12,'ly=70 circle'),(.8,''),(.12,'ly=186 circle'),(2.,'')],
}
def capture_key(identity):
 # SDL accepts directed XSendEvent here; XTest is not delivered in this setup.
 # Verify the actual focused X11 game's PID, then send only to that window.
 C=ctypes;x=C.CDLL(ctypes.util.find_library('X11') or 'libX11.so.6')
 display=C.c_void_p;ulong=C.c_ulong
 x.XOpenDisplay.argtypes=[C.c_char_p];x.XOpenDisplay.restype=display
 x.XCloseDisplay.argtypes=[display];x.XGetInputFocus.argtypes=[display,C.POINTER(ulong),C.POINTER(C.c_int)]
 x.XInternAtom.argtypes=[display,C.c_char_p,C.c_int];x.XInternAtom.restype=ulong
 x.XGetWindowProperty.argtypes=[display,ulong,ulong,C.c_long,C.c_long,C.c_int,ulong,C.POINTER(ulong),C.POINTER(C.c_int),C.POINTER(ulong),C.POINTER(ulong),C.POINTER(C.POINTER(C.c_ubyte))]
 x.XFree.argtypes=[C.c_void_p];x.XKeysymToKeycode.argtypes=[display,ulong];x.XKeysymToKeycode.restype=C.c_ubyte;x.XSync.argtypes=[display,C.c_int]
 class Key(C.Structure):
  _fields_=[('type',C.c_int),('serial',ulong),('send_event',C.c_int),('display',display),('window',ulong),('root',ulong),('subwindow',ulong),('time',ulong),('x',C.c_int),('y',C.c_int),('x_root',C.c_int),('y_root',C.c_int),('state',C.c_uint),('keycode',C.c_uint),('same_screen',C.c_int)]
 class Event(C.Union):_fields_=[('key',Key),('pad',C.c_long*24)]
 x.XDefaultRootWindow.argtypes=[display];x.XDefaultRootWindow.restype=ulong
 x.XSendEvent.argtypes=[display,ulong,C.c_int,C.c_long,C.POINTER(Event)];x.XSendEvent.restype=C.c_int
 d=x.XOpenDisplay(None)
 if not d:raise ValueError('XWayland display required for scoped F9 capture')
 try:
  window=ulong();revert=C.c_int();x.XGetInputFocus(d,C.byref(window),C.byref(revert))
  atom=x.XInternAtom(d,b'_NET_WM_PID',True);kind=ulong();fmt=C.c_int();count=ulong();remaining=ulong();data=C.POINTER(C.c_ubyte)()
  rc=x.XGetWindowProperty(d,window,atom,0,1,False,6,C.byref(kind),C.byref(fmt),C.byref(count),C.byref(remaining),C.byref(data))
  try:
   if rc or kind.value!=6 or fmt.value!=32 or count.value!=1 or not data or C.cast(data,C.POINTER(ulong))[0]!=identity['pid']:raise ValueError('F9 focused window PID does not match exact managed game')
  finally:
   if data:x.XFree(data)
  key=x.XKeysymToKeycode(d,0xffc6) # XK_F9
  if not key:raise ValueError('F9 keycode unavailable')
  def send(kind,mask):
   event=Event();event.key=Key(type=kind,display=d,window=window.value,root=x.XDefaultRootWindow(d),keycode=key,same_screen=1)
   if not x.XSendEvent(d,window,False,mask,C.byref(event)):raise ValueError('directed F9 send failed')
   x.XSync(d,False)
  try:send(2,1);time.sleep(.15)
  finally:send(3,2)
 finally:x.XCloseDisplay(d)

def main():
 p=argparse.ArgumentParser(description=__doc__);p.add_argument('session');p.add_argument('routine',choices=PATTERNS);p.add_argument('--label',required=True);p.add_argument('--capture',action='store_true',help='request existing bounded F9 current-source/retained-link capture');p.add_argument('--capture-at',type=float,default=.75,help='fraction of the selected action elapsed before F9, 0..0.95; default .75 avoids capturing a pre-movement queued origin');p.add_argument('--capture-index',type=int,help='zero-based action index, e.g. 2 for the run-stop settling window');a=p.parse_args();assert re.fullmatch('[a-zA-Z0-9-]{1,60}',a.label)
 if not math.isfinite(a.capture_at) or not 0<=a.capture_at<=.95:p.error('capture-at must be finite in [0,.95]')
 if a.capture_index is not None and not 0<=a.capture_index<len(PATTERNS[a.routine]):p.error('capture-index out of range')
 run=runner.checked_run(a.session);manifest=json.loads((run/'manifest.json').read_text());runner.validate_daily_manifest(run,manifest);identity=runner.current_game(run);guard=runner.daily_session_guard.SessionGuard().read();assert guard and guard['state']=='pending' and guard['session']==a.session and guard['root']==str(runner.ROOT),'exact live managed session required'
 assert manifest['nr_enabled'] is True,'NR session required';pad=run/'pad.txt';assert pad.is_file() and not pad.is_symlink() and not pad.read_text().strip(),'released private pad required'
 runner.control(argparse.Namespace(command='focus',session=a.session))
 record=run/f'quality-{a.label}.jsonl';assert not record.exists(),'no overwrite';stream=record.open('x')
 def event(kind,**extra):stream.write(json.dumps(dict(kind=kind,session=a.session,routine=a.routine,label=a.label,monotonic_ns=time.monotonic_ns(),**extra))+'\n');stream.flush()
 def interrupted(s,f):raise InterruptedError('bounded NR scenario interrupted')
 for s in (signal.SIGINT,signal.SIGTERM):signal.signal(s,interrupted)
 event('begin',identity=identity,capture_at_fraction=a.capture_at,scope='input schedule only; capture log supplies actual source/camera/Present identity')
 try:
  for index,(duration,value) in enumerate(PATTERNS[a.routine]):
   assert runner.live(identity['pid'])==identity,'game PID/start/exe identity changed';pad.write_text(value+'\n');event('input',index=index,value=value,duration=duration)
   if a.capture and index==(a.capture_index if a.capture_index is not None else (1 if len(PATTERNS[a.routine])>1 else 0)):
    offset=duration*a.capture_at;time.sleep(offset);capture_key(identity)
    event('capture_requested',key='F9',backend='XSendEvent_exact_focused_PID',after_action_seconds=offset,scope='request, not successful GPU copy or simulated-action proof')
    time.sleep(duration-offset)
   else:time.sleep(duration)
  event('end',completed=True)
 finally:pad.write_text('');event('released');stream.close()
if __name__=='__main__':main()
