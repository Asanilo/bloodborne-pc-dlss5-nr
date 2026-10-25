#!/usr/bin/env python3
"""Reclaim only a private CPU Wine prefix after an unproved session and a NEW boot.

Never changes the failed result, GPU audit, failure latch or boot receipt. This
does not prove normal GPU retirement and does not launch/admit GPU work.
"""
import argparse
import fcntl
import hashlib
import json
import os
from pathlib import Path
import tempfile
import uuid


def read(path):
    if path.resolve() != path or path.is_symlink() or not path.is_file():
        raise ValueError('canonical private recovery metadata required')
    return json.loads(path.read_text())


def atomic(path, value):
    with tempfile.NamedTemporaryFile(mode='w', dir=path.parent, delete=False) as stream:
        temporary = Path(stream.name)
        json.dump(value, stream, indent=2); stream.write('\n')
        stream.flush(); os.fsync(stream.fileno())
    try:
        os.replace(temporary, path)
    finally:
        temporary.unlink(missing_ok=True)


def identity(path):
    if path.resolve() != path or path.is_symlink() or not path.is_dir():
        raise ValueError('canonical prefix directory required')
    info = path.stat()
    if info.st_uid != os.getuid():
        raise ValueError('prefix must belong to the current UID')
    return dict(device=info.st_dev, inode=info.st_ino)


def recover(root, run, current_boot, no_holders):
    root, run = Path(root), Path(run)
    if (root.resolve() != root or run.resolve() != run or run.parent != root / 'runs'
            or str(uuid.UUID(current_boot)) != current_boot):
        raise ValueError('canonical session and actual boot identity required')
    manifest, result = read(run / 'manifest.json'), read(run / 'result.json')
    previous_boot = manifest['host_safety']['boot_id']
    if (str(uuid.UUID(previous_boot)) != previous_boot or previous_boot == current_boot
            or manifest.get('session') != run.name or result.get('status') != 'HOLD'):
        raise ValueError('an original HOLD and a different actual boot are required')
    if no_holders() is not True:
        raise ValueError('live private GPU/Helper owners prevent prefix recovery')
    archive = run / 'prefix-recovery-original-lease.json'
    lease_path = run / 'nr-prefix-lease.json'
    lease = read(archive if archive.exists() else lease_path)
    source, target = Path(lease['source']), Path(lease['target'])
    if (lease['state'] != 'RETAINED_UNPROVED' or target != run / 'profile/wine-prefix'
            or source.resolve() != source or source.name != 'wine-prefix'
            or source.parent.name != 'profile' or source.parents[2] != root / 'runs'
            or source.parent.parent == run):
        raise ValueError('only an exact retained private prefix lease can be recovered')
    prior = read(source.parent.parent / 'result.json')
    if (prior.get('status') != 'PASS_NORMAL_TEARDOWN'
            or prior.get('native_lastuse_proof') is not True or prior.get('residuals') != []
            or type(prior.get('returncode')) is not int or prior['returncode'] != 0
            or prior.get('watchdog_expired') is not False
            or prior.get('supervisor_interrupted') is not False
            or type(prior.get('received_signals')) is not int or prior['received_signals'] != 0):
        raise ValueError('original prefix source was not normally retired')
    record_path = run / 'prefix-recovery.json'
    if not record_path.exists():
        if source.exists() or not (target / 'system.reg').is_file():
            raise ValueError('prefix source exists or retained prefix is not initialized')
        record = dict(schema='nr-prefix-new-boot-recovery-v1', session=run.name,
                      previous_boot=previous_boot, current_boot=current_boot,
                      source=str(source), target=str(target),
                      original_lease_sha256=hashlib.sha256(lease_path.read_bytes()).hexdigest(),
                      prior_result_sha256=hashlib.sha256((source.parent.parent / 'result.json').read_bytes()).hexdigest(),
                      manifest_sha256=hashlib.sha256((run / 'manifest.json').read_bytes()).hexdigest(),
                      prefix_identity=identity(target), state='PREPARED',
                      original_result_sha256=hashlib.sha256((run / 'result.json').read_bytes()).hexdigest(),
                      scope='CPU prefix reuse only; original GPU retirement remains NOT_PROVED')
        if not archive.exists():
            with archive.open('xb') as stream:
                stream.write(lease_path.read_bytes())
        elif archive.read_bytes() != lease_path.read_bytes():
            raise ValueError('original lease changed before recovery preparation')
        atomic(record_path, record)
    record = read(record_path)
    if (record.get('schema') != 'nr-prefix-new-boot-recovery-v1'
            or record.get('state') not in ('PREPARED', 'RETURNED_AFTER_REBOOT')
            or record.get('session') != run.name or record.get('previous_boot') != previous_boot
            or record.get('current_boot') != current_boot
            or record.get('source') != str(source) or record.get('target') != str(target)
            or record.get('original_lease_sha256') != hashlib.sha256(archive.read_bytes()).hexdigest()
            or record.get('prior_result_sha256') != hashlib.sha256((source.parent.parent / 'result.json').read_bytes()).hexdigest()
            or record.get('manifest_sha256') != hashlib.sha256((run / 'manifest.json').read_bytes()).hexdigest()
            or record.get('original_result_sha256') != hashlib.sha256((run / 'result.json').read_bytes()).hexdigest()):
        raise ValueError('prefix recovery context/evidence changed')
    if no_holders() is not True:
        raise ValueError('private owners appeared before prefix rename')
    if not source.exists():
        if identity(target) != record['prefix_identity']:
            raise ValueError('retained prefix directory identity changed')
        target.rename(source)
    elif identity(source) != record['prefix_identity']:
        raise ValueError('prefix source was recreated by another owner')
    target.mkdir(exist_ok=True)
    identity(target)  # Never accept a symlink placeholder after a partial rename.
    if any(target.iterdir()):
        raise ValueError('current session prefix directory is not empty after recovery')
    atomic(lease_path, lease | dict(state='RETURNED_AFTER_REBOOT'))
    atomic(record_path, record | dict(state='RETURNED_AFTER_REBOOT'))
    return read(record_path)


def main():
    parser = argparse.ArgumentParser(description=__doc__); parser.add_argument('session')
    args = parser.parse_args()
    import runner
    with open('/tmp/nrbridge-poc-gpu.lock', 'a') as lock:
        fcntl.flock(lock, fcntl.LOCK_EX | fcntl.LOCK_NB)
        boot = Path('/proc/sys/kernel/random/boot_id').read_text().strip()
        import subprocess
        journal = subprocess.check_output(['journalctl', '-k', '-b', '0', '--no-pager', '--output=json'], text=True, timeout=20)
        runner.check_kernel_journal(journal, boot)
        report = recover(runner.ROOT, runner.checked_run(args.session), boot,
                         lambda: not runner.holders())
        print(json.dumps(report, indent=2))


if __name__ == '__main__':
    main()
