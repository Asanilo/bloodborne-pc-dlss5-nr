#!/usr/bin/env python3
"""Coordinator-owned bounded private-scene inputs; starts no game, Helper or GPU context."""
import argparse,json,re,signal,time
from pathlib import Path
from runner import checked_run,current_game,live

def last_source(run):
 with (run/'game.log').open('rb') as f:
  f.seek(0,2);size=f.tell();f.seek(max(0,size-131072));text=f.read().decode(errors='replace')
 matches=re.findall(r'NATIVE_ASYNC_PRE_SR_CURRENT_PRESENT .*?source_game_frame=(\d+)',text)
 return int(matches[-1]) if matches else None

def main():
 p=argparse.ArgumentParser(description=__doc__);p.add_argument('session');p.add_argument('--phase',choices=['static','dynamic'],required=True);p.add_argument('--label',required=True);p.add_argument('--seconds',type=float,required=True);a=p.parse_args()
 if not 1<=a.seconds<=180 or not re.fullmatch('[a-zA-Z0-9-]+',a.label):p.error('bounded duration and safe label required')
 run=checked_run(a.session);identity=current_game(run);pad=run/'pad.txt';assert pad.is_file() and not pad.is_symlink()
 def event(kind,**fields):
  with (run/'scenario-events.jsonl').open('a') as f:f.write(json.dumps({'kind':kind,'label':a.label,'phase':a.phase,'session':a.session,'monotonic_ns':time.monotonic_ns(),'last_actual_present_source':last_source(run),**fields})+'\n')
 def stop(signum,frame):raise InterruptedError('scenario signal '+str(signum))
 for s in [signal.SIGTERM,signal.SIGINT]:signal.signal(s,stop)
 pattern=[(2.,'rx=185'),(2.,''),(2.,'rx=71'),(2.,''),(.8,'rx=255'),(3.,''),(.8,'rx=0'),(3.,''),(1.2,'ly=72'),(3.,''),(1.2,'ly=184'),(3.,'')]
 if a.phase=='static':pattern=[(a.seconds,'')]
 deadline=time.monotonic()+a.seconds;event('begin',requested_seconds=a.seconds,game=identity);index=0
 try:
  while time.monotonic()<deadline:
   if live(identity['pid'])!=identity:raise RuntimeError('private game PID/starttime/exe changed')
   seconds,value=pattern[index%len(pattern)];index+=1;duration=min(seconds,max(0,deadline-time.monotonic()));pad.write_text(value);event('input',value=value,duration=duration)
   time.sleep(duration)
  event('end',completed=True)
 finally:pad.write_text('')
if __name__=='__main__':main()
