#!/usr/bin/env python3
"""One-shot recovery admission for a current-boot pending/HOLD receipt.

Runs under the exclusive project GPU lock and only writes the recovery record
when all of these hold:

  1. the boot receipt belongs to this boot and is pending or HOLD;
  2. no game or Helper process is running;
  3. the kernel journal for this boot has no GPU driver error;
  4. a short Vulkan submit probe (bb-probe --vulkan-only) returns PASS;
  5. the journal still has no GPU driver error after the probe.

The failed session's receipt stays HOLD; the recovery record embeds it unchanged
so the transition is auditable. Nothing is reset, deleted or rewritten as a
normal retirement.

usage: recovery_admit.py [--probe <bb-probe>] [--timeout 30]
"""
import argparse
import fcntl
import hashlib
import json
import os
import subprocess
import sys
from pathlib import Path

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE))

import daily_session_guard  # noqa: E402
import runner  # noqa: E402

LOCK = Path('/tmp/nrbridge-poc-gpu.lock')
DEFAULT_PROBE = Path('/home/arinp/Code/nr-home/build/sm-official/build/native/bb-probe')


def journal():
    return subprocess.check_output(
        ['journalctl', '-k', '-b', '0', '--no-pager', '--output=json'],
        text=True, stderr=subprocess.PIPE, timeout=20)


def sha(data):
    return hashlib.sha256(data).hexdigest()


def busy_processes():
    # Exact process name for the game (a command line that merely mentions the
    # probe path must not count), and the Helper by its executable name.
    found = []
    for pattern, exact in (('bb-probe', True), ('wine_runtime_model.exe', False)):
        args = ['pgrep', '-x', pattern] if exact else ['pgrep', '-f', pattern]
        out = subprocess.run(args, capture_output=True, text=True)
        found += [pid for pid in out.stdout.split() if pid.isdigit()]
    return found


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--probe', default=str(DEFAULT_PROBE))
    parser.add_argument('--timeout', type=int, default=30)
    args = parser.parse_args()
    probe = Path(args.probe)
    if not probe.is_file():
        raise SystemExit('probe binary missing: ' + str(probe))

    lock = open(LOCK, 'a+')
    try:
        fcntl.flock(lock, fcntl.LOCK_EX | fcntl.LOCK_NB)
    except BlockingIOError:
        raise SystemExit('GPU lock is held by another session')

    guard = daily_session_guard.SessionGuard()
    receipt = guard.read()
    if receipt is None:
        raise SystemExit('no boot receipt: nothing to recover')
    if receipt['boot_id'] != guard.boot_id:
        raise SystemExit('boot receipt belongs to another boot')
    if receipt['state'] == 'retired':
        print('RECOVERY_NOT_NEEDED receipt already retired')
        return
    print('RECOVERY_TARGET', receipt['state'], receipt['session'], receipt['root'])

    busy = busy_processes()
    if busy:
        raise SystemExit('live game/Helper process: ' + '; '.join(busy))

    runner.check_kernel_journal(journal(), guard.boot_id)
    print('JOURNAL_CLEAN before probe')

    env = dict(os.environ)
    build = probe.parent
    env['LD_LIBRARY_PATH'] = ':'.join(filter(None, [
        str(build / 'gpu'), str(build),
        '/home/arinp/Code/bloodborne_pc/out/deps/root/usr/lib',
        env.get('LD_LIBRARY_PATH')]))
    result = subprocess.run([str(probe), '--vulkan-only'], capture_output=True,
                            text=True, timeout=args.timeout, env=env)
    output = (result.stdout or '') + (result.stderr or '')
    print(output.strip())
    if result.returncode != 0 or 'PASS' not in output:
        raise SystemExit('probe failed: exit ' + str(result.returncode))

    runner.check_kernel_journal(journal(), guard.boot_id)
    print('JOURNAL_CLEAN after probe')

    record = {'tool': str(probe), 'sha256': sha(probe.read_bytes()),
              'result': 'PASS', 'output_sha256': sha(output.encode())}
    path = guard.write_recovery(receipt, record)
    print('RECOVERY_WRITTEN', path)
    print('RECOVERY_EVIDENCE probe_sha256=' + record['sha256'] + ' output_sha256=' + record['output_sha256'])


if __name__ == '__main__':
    main()
