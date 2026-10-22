#!/usr/bin/env python3
"""Build only the small Native GPU mailbox fixture in the existing out/."""
import fcntl
import json
from pathlib import Path
import subprocess
from build_wine_probe import identity
ROOT=Path(__file__).resolve().parents[2]
INPUTS=['tools/fg_dev/native_gpu_probe.cpp','gpu/upscale/dlssg_gpu_contract.h',
        'gpu/upscale/dlssg_mailbox_import.h','gpu/upscale/dlssg_gpu_peer.h',
        'gpu/render/native_pre_sr_spawn_log.h','gpu/third_party/nlohmann/include/nlohmann/json.hpp']


def sources():
    paths=[ROOT/p for p in INPUTS]+sorted((ROOT/'out/deps/root/usr/include/vulkan').glob('*.h'))
    return {str(p.relative_to(ROOT)):identity(p) for p in paths}


def main():
    with open('/tmp/nrbridge-poc-gpu.lock','a') as lock:
        fcntl.flock(lock,fcntl.LOCK_EX|fcntl.LOCK_NB)
        before=sources();target=ROOT/'out/native-dlssg-gpu-probe';temp=target.with_suffix('.building')
        command=['c++','-std=c++20','-O1','-Wall','-Wextra','-Wno-missing-field-initializers',
                 '-I'+str(ROOT/'gpu/third_party/nlohmann/include'),'-I'+str(ROOT/'out/deps/root/usr/include'),
                 str(ROOT/INPUTS[0]),'-ldl','-o',str(temp)]
        subprocess.run(command,check=True)
        if sources()!=before:raise RuntimeError('GPU probe source changed during compile')
        temp.replace(target)
        record={'schema':'dlssg-native-gpu-probe-build-v1','head':subprocess.check_output(['git','rev-parse','HEAD'],cwd=ROOT,text=True).strip(),
                'inputs':before,'command':command,'artifact':identity(target)}
        (ROOT/'out/m22-native-gpu-build.json').write_text(json.dumps(record,indent=2)+'\n')


if __name__=='__main__':main()
