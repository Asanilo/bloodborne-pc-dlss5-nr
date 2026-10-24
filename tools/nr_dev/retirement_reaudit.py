#!/usr/bin/env python3
"""Correct only the dynamic-DLSS owner-dispatch audit omission after full proof.

No GPU work, process termination, failure-latch removal or save synchronization.
The original HOLD and audit are preserved. A current-boot held receipt can be
retired only after the corrected auditor checks every recorded GPU owner again.
"""
import argparse
import fcntl
import hashlib
import json
from pathlib import Path

import peer_retirement

SCHEMA = 'nrbridge-retirement-reaudit-v1'
OMISSION = 'DLSSG explicit no-Helper exit'
ORIGINAL_RESULT = 'retirement-reaudit-original-result.json'
ORIGINAL_AUDIT = 'retirement-reaudit-original-audit.json'
RECORD = 'retirement-reaudit.json'


def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def read(path):
    if path.resolve() != path or path.is_symlink() or not path.is_file():
        raise ValueError('canonical regular retirement evidence required')
    return json.loads(path.read_text())


def normal_termination(result):
    if (type(result.get('returncode')) is not int or result['returncode'] != 0
            or result.get('residuals') != [] or result.get('watchdog_expired') is not False
            or result.get('supervisor_interrupted') is not False
            or type(result.get('received_signals')) is not int or result['received_signals'] != 0):
        raise ValueError('reaudit cannot resolve abnormal or incomplete termination')


def original_scope(run, receipt):
    manifest = read(run / 'manifest.json')
    if (receipt['state'] != 'hold' or run != Path(receipt['root']) / 'runs' / receipt['session']
            or manifest.get('session') != receipt['session'] or manifest.get('daily') is not True
            or manifest.get('host_safety', {}).get('boot_id') != receipt['boot_id']
            or manifest.get('environment', {}).get('BB_FRAME_GEN_BACKEND') != 'dlssg'
            or not (run / 'dlssg-generations.json').is_file()):
        raise ValueError('reaudit requires the exact held dynamic-DLSS session')
    original = read(run / ORIGINAL_RESULT)
    normal_termination(original)
    if (run / 'native.abort').exists():
        raise ValueError('Native ownership quarantine cannot be resolved by reaudit')
    if original.get('status') != 'HOLD' or read(run / ORIGINAL_AUDIT) != {
            'retirement': 'NOT_PROVED', 'error': OMISSION}:
        raise ValueError('reaudit is restricted to the owner-dispatch omission')
    return original


def evidence(run):
    # Include terminal/FD/model records and retained stdout, without copying
    # frames, Wine prefixes, generated game images or any player data.
    excluded = {'result.json', 'retirement-audit.json', RECORD,
                ORIGINAL_RESULT, ORIGINAL_AUDIT, 'nr-prefix-lease.json'}
    paths = {p for p in run.glob('*.json') if p.name not in excluded}
    paths.update(p for p in (run / 'game.log', run / 'game.log.1') if p.exists())
    paths.update((run / 'dlssg-generations').rglob('*'))
    if len(paths) > 16384:
        raise ValueError('bounded retirement evidence set required')
    out = {}
    for path in sorted(paths):
        if path.resolve() != path or path.is_symlink():
            raise ValueError('retirement evidence cannot follow links')
        if path.is_dir():
            continue
        if not path.is_file() or path.stat().st_nlink != 1 or path.stat().st_size > 32 * 1024 * 1024:
            raise ValueError('bounded regular retirement evidence required')
        out[str(path.relative_to(run))] = digest(path)
    return out


def validate_reconciliation(run, receipt):
    """Recheck the exact archived HOLD, immutable proof, and actual owner audit."""
    original_scope(run, receipt)
    record = read(run / RECORD)
    if (record.get('schema') != SCHEMA or record.get('receipt') != receipt
            or record.get('original_result_sha256') != digest(run / ORIGINAL_RESULT)
            or record.get('original_audit_sha256') != digest(run / ORIGINAL_AUDIT)
            or record.get('evidence') != evidence(run)):
        raise ValueError('retirement reconciliation evidence drift')
    result = read(run / 'result.json')
    normal_termination(result)
    if (result.get('status') != 'PASS_NORMAL_TEARDOWN'
            or result.get('native_lastuse_proof') is not True
            or result.get('retirement_reaudit') != record['original_result_sha256']):
        raise ValueError('reaudited result lacks its original HOLD binding')
    audited = peer_retirement.audit_session(run)
    # Only result.json changes after the first, read-only audit.
    comparable = lambda value: {k: v for k, v in value.items() if k != 'result.json_sha256'}
    if comparable(audited) != comparable(record.get('audit', {})):
        raise ValueError('fresh actual owner audit differs from reconciliation')
    return result


def reconcile(run, guard, no_holders):
    receipt = guard.read()
    if receipt is None or receipt['boot_id'] != guard.boot_id or receipt['state'] != 'hold':
        raise ValueError('matching current-boot HOLD receipt required')
    if no_holders() is not True:
        raise ValueError('live GPU owners prevent retirement reconciliation')
    # Resume safely if interrupted after preserving evidence or publishing result.
    for source, archive in (('result.json', ORIGINAL_RESULT),
                            ('retirement-audit.json', ORIGINAL_AUDIT)):
        target = run / archive
        if not target.exists():
            with target.open('xb') as stream:
                stream.write((run / source).read_bytes())
    original = original_scope(run, receipt)
    if not (run / RECORD).exists():
        before = evidence(run)
        audited = peer_retirement.audit_session(run)
        if before != evidence(run) or no_holders() is not True or guard.read() != receipt:
            raise ValueError('retirement evidence/owners changed during reaudit')
        record = dict(schema=SCHEMA, receipt=receipt,
                      auditor_sha256=digest(Path(peer_retirement.__file__).resolve()),
                      original_result_sha256=digest(run / ORIGINAL_RESULT),
                      original_audit_sha256=digest(run / ORIGINAL_AUDIT), evidence=before,
                      audit=audited, scope='actual normal retirement; quality NOT_ACCEPTED')
        with (run / RECORD).open('x') as stream:
            json.dump(record, stream, indent=2); stream.write('\n')
    record = read(run / RECORD)
    # Use atomic replacement; archives and the failure latch remain unchanged.
    result = original | dict(status='PASS_NORMAL_TEARDOWN', native_lastuse_proof=True,
                             retirement_reaudit=record['original_result_sha256'])
    from runner import atomic
    atomic(run / 'result.json', result)
    validate_reconciliation(run, receipt)
    if no_holders() is not True:
        raise ValueError('live GPU owners appeared before receipt retirement')
    guard.retire_reaudited(receipt, run)
    atomic(run / 'retirement-audit.json', peer_retirement.audit_session(run))
    return result


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('session')
    args = parser.parse_args()
    import runner
    with open('/tmp/nrbridge-poc-gpu.lock', 'a') as lock:
        fcntl.flock(lock, fcntl.LOCK_EX | fcntl.LOCK_NB)
        return 0 if reconcile(runner.checked_run(args.session),
                              runner.daily_session_guard.SessionGuard(),
                              lambda: not runner.holders()) else 3


if __name__ == '__main__':
    raise SystemExit(main())
