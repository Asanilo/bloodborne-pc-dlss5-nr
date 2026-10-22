#!/usr/bin/env python3
"""Run the fixed, device-free NGX discovery probe under the existing project GPU lock."""
import argparse
import fcntl
import json
import os
from pathlib import Path
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / 'tools/nr_dev'))
import runner


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--local-inputs', required=True)
    args = parser.parse_args()
    binding = runner.runtime_inputs.load(args.local_inputs, runner.ROOT)
    executable = ROOT / 'out/ngx-fg-probe'
    with open('/tmp/nrbridge-poc-gpu.lock', 'a') as lock:
        fcntl.flock(lock, fcntl.LOCK_EX | fcntl.LOCK_NB)
        safety = runner.daily_preflight(lock, binding)
        env = os.environ.copy()
        env.update(DLSSNR_DISABLE='1', DLSSNR_AUTO_DOWNLOAD='0',
                   LD_LIBRARY_PATH=str(ROOT / 'out/deps/root/usr/lib'))
        result = subprocess.run([str(executable), str(ROOT / 'out'),
                                 str(ROOT / 'out/fg-ngx-discovery')],
                                cwd=ROOT, env=env, text=True, stdout=subprocess.PIPE,
                                stderr=subprocess.STDOUT)
        (ROOT / 'out/fg-ngx-probe.log').write_text(result.stdout)
        report = {'returncode': result.returncode, 'host_safety': safety,
                  'binary_sha256': runner.sha(executable),
                  'scope': 'requirements discovery only; creates no VkDevice or queue, evaluates no FG',
                  'normal_instance_destroy': result.returncode == 0 and
                      'NGX_PROBE_RETIRED instance_destroyed=1 devices_created=0 GPU_submissions=0' in result.stdout}
        (ROOT / 'out/fg-ngx-probe.json').write_text(json.dumps(report, indent=2) + '\n')
        print(result.stdout, end='')
        return result.returncode if report['normal_instance_destroy'] else 3


if __name__ == '__main__':
    raise SystemExit(main())
