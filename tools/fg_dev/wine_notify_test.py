#!/usr/bin/env python3
"""CPU-only Linux rename -> Wine notification test in an existing retired prefix."""
import fcntl
import hashlib
import json
import os
from pathlib import Path
import statistics
import subprocess
import tempfile
import time

ROOT=Path(__file__).resolve().parents[2]
GE=Path('/home/arinp/.local/share/Steam/compatibilitytools.d/GE-Proton11-7-x86_64/files')
PREFIX=Path('/home/arinp/Code/bloodborne_pc/.worktree/nrbridge/out/nr-dev/runs/m2-native-dlssg-validation-r17/profile/wine-prefix')

def atomic(path,value):
    temp=path.with_suffix('.native-tmp');temp.write_text(str(value)+'\n');temp.replace(path)

def wait(path,value,process):
    deadline=time.monotonic()+5
    while time.monotonic()<deadline:
        if path.exists() and path.read_text().strip()==str(value):return
        if process.poll() is not None:raise RuntimeError('CPU notification probe exited early')
        time.sleep(.0001)
    raise RuntimeError('CPU notification timeout')

def main():
    assert PREFIX.is_dir()
    result=json.loads((PREFIX.parent.parent/'result.json').read_text())
    assert result['status']=='PASS_NORMAL_TEARDOWN'
    with open('/tmp/nrbridge-poc-gpu.lock','a') as lock,tempfile.TemporaryDirectory(prefix='dlssg-wine-notify-') as directory:
        fcntl.flock(lock,fcntl.LOCK_EX|fcntl.LOCK_NB)
        compiler=Path('/home/arinp/Code/dlss5_linux/build/toolchain/llvm-mingw-20261006-ucrt-ubuntu-22.04-x86_64/bin/x86_64-w64-mingw32-clang++')
        sources=['tools/fg_dev/wine_notify_probe.cpp','tools/fg_dev/dlssg_wine_wake.h']
        source_hashes={name:hashlib.sha256((ROOT/name).read_bytes()).hexdigest() for name in sources}
        compile_command=[str(compiler),'-std=c++17','-static','-O1','-Wall','-Wextra','-Werror',str(ROOT/sources[0]),'-o',str(ROOT/'out/wine-notify-probe.exe')]
        subprocess.run(compile_command,check=True)
        assert source_hashes=={name:hashlib.sha256((ROOT/name).read_bytes()).hexdigest() for name in sources},'CPU notification sources changed during compile'
        root=Path(directory);atomic(root/'request.txt',1) # Request exists before notification registration.
        env=os.environ|{'WINEPREFIX':str(PREFIX),'WINELOADERNOEXEC':'1','WINEDEBUG':'-all','WINEDLLOVERRIDES':'mscoree,mshtml='}
        command=[str(GE/'lib/wine/x86_64-unix/wine-preloader'),str(GE/'lib/wine/x86_64-unix/wine'),str(ROOT/'out/wine-notify-probe.exe'),'Z:'+str(root)]
        process=subprocess.Popen(command,env=env,stdout=subprocess.PIPE,stderr=subprocess.STDOUT,text=True)
        wait(root/'ready.txt',1,process);wait(root/'ack.txt',1,process)
        time.sleep(.16) # Several timed waits must not lose the armed notification.
        samples=[]
        for sequence in range(2,66):
            start=time.monotonic_ns();atomic(root/'request.txt',sequence);wait(root/'ack.txt',sequence,process)
            samples.append((time.monotonic_ns()-start)/1e6)
        output=process.communicate(timeout=10)[0]
        assert process.returncode==0 and 'CPU_NOTIFY_PASS requests=65 fallback_count=0 Vulkan_loaded=0' in output,output
        record={'scope':'CPU-only cross-Linux/Wine atomic rename; no Vulkan/NGX or GPU completion claim',
                'requests':65,'fallbacks':0,'roundtrip_median_ms':statistics.median(samples),
                'roundtrip_p95_ms':sorted(samples)[int((len(samples)-1)*.95)],'output':output,
                'source_sha256':source_hashes,'compile_command':compile_command,'artifact_sha256':hashlib.sha256((ROOT/'out/wine-notify-probe.exe').read_bytes()).hexdigest()}
        (ROOT/'out/m22-wine-notify-cpu.json').write_text(json.dumps(record,indent=2)+'\n');print(json.dumps(record))

if __name__=='__main__':main()
