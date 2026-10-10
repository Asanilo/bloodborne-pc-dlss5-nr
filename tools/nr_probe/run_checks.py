#!/usr/bin/env python3
"""Bounded subprocess checks; no game installation, saves, Wine or NR library is touched."""
import argparse
import json
import os
import signal
from pathlib import Path
import subprocess
import sys

def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('--bin-dir',type=Path,default=Path('out/nr-probe'))
    p.add_argument('--logs',type=Path,default=Path('out/audit/checks'))
    p.add_argument('--bridge',type=Path)
    p.add_argument('--ngx',type=Path)
    p.add_argument('--validation',action='store_true',help='Require a locally installed/enumerable Khronos layer')
    args=p.parse_args();args.logs.mkdir(parents=True,exist_ok=True)
    env=dict(os.environ,BB_DLSS_AUDIT='1')
    if args.validation:
        env['VK_INSTANCE_LAYERS']='VK_LAYER_KHRONOS_validation'
        env['VK_LAYER_VALIDATE_SYNC']='1'
        env.pop('VK_LAYER_ENABLES',None)
    probe=str(args.bin_dir.resolve()/'nr-probe')
    cases=[('noop',[probe,'--mode','no-op'],0,'NRPROBE PASS'),
           ('pass-through',[probe,'--mode','pass-through','--frames','16'],0,'NRPROBE PASS'),
           ('odd-extent',[probe,'--width','127','--height','73'],0,'NRPROBE PASS')]
    failures={'version':'E_ABI_VERSION','uuid':'E_DEVICE_UUID','format':'E_FORMAT_ROLE',
              'drop-fd':'E_FD_COUNT','truncated':'E_PACKET_SIZE','peer-exit':'E_PEER_INJECTED',
              'timeout':'VkResult=2','corrupt':'E_READBACK','sequence':'E_SEQUENCE_OR_RESOURCE_CHANGE','extent':'E_EXTENT'}
    cases.extend((name,[probe,'--fault',name],2,code) for name,code in failures.items())
    if bool(args.bridge)!=bool(args.ngx):p.error('--bridge and --ngx must be provided together')
    if args.bridge:
        smoke=str(args.bin_dir.resolve()/'dlss-sr-smoke')
        cases.extend((mode,[smoke,str(args.bridge.resolve()),str(args.ngx.resolve()),mode],0,'DLSS_SMOKE PASS') for mode in ('dlaa','quality'))
        cases.append(('missing-bridge',[smoke,str(args.logs.resolve()/'does-not-exist.so'),str(args.ngx.resolve()),'dlaa'],2,'E_BRIDGE_LOAD'))
    results=[]
    for name,command,status,expected in cases:
        try:
            proc=subprocess.Popen(command,stdout=subprocess.PIPE,stderr=subprocess.STDOUT,text=True,env=env,start_new_session=True)
            try:
                text,_=proc.communicate(timeout=45)
                actual=proc.returncode
            except subprocess.TimeoutExpired:
                # Kill the worker too; a surviving child holding stdout would otherwise
                # keep communicate() open after the producer's timeout.
                os.killpg(proc.pid,signal.SIGKILL)
                try:text,_=proc.communicate(timeout=5)
                except subprocess.TimeoutExpired:text='HARNESS E_PROCESS_GROUP_STUCK'
                text+='\nHARNESS E_PROCESS_TIMEOUT\n';actual=124
        except OSError as error:
            text=f'HARNESS E_START {error}\n';actual=127
        (args.logs/(name+'.log')).write_text(text)
        bad_validation=any(token in text for token in ['Validation Error','VUID-','UNASSIGNED-Threading','SYNC-HAZARD','Failed to load layer','ERROR: [Loader'])
        ok=actual==status and expected in text and not bad_validation
        if args.validation and status==0:
            ok=ok and 'VULKAN validation=enabled' in text
        if status==0 and 'NRPROBE PASS' in expected:
            ok=ok and text.count('GPU pid=')==2 and 'guides=OK' in text
        if name in ('dlaa','quality'):
            ok=ok and text.count('DLSS: AUDIT frame=')==10 and 'reset=1' in text and 'reset=0' in text and text.count('result=0x00000001')==10
        results.append({'name':name,'pass':ok,'exit':actual,'expected_exit':status,'expected_log':expected,'validation_errors':bad_validation})
        print(('PASS' if ok else 'FAIL'),name,'exit='+str(actual),flush=True)
    (args.logs/'summary.json').write_text(json.dumps({'validation_requested':args.validation,'results':results},indent=2)+'\n')
    return 0 if all(x['pass'] for x in results) else 1
if __name__=='__main__':sys.exit(main())
