"""Narrow reboot-isolated diagnostics; never resolves or edits historical HOLDs.

Record example (digests are full SHA256 strings):
EXAMPLE_RECORD documents the coordinator-owned input. No API writes evidence.
"""
import fcntl
import importlib.util
import json
import os
import re
import subprocess
from collections import Counter
from pathlib import Path

EXAMPLE_RECORD = {
    'schema': 'nr-isolated-context-v1', 'session': 'dev-helper-off-01',
    'boot_id': '<current boot UUID>', 'previous_boot_id': '<old boot UUID>',
    'historical_faults_resolved': False,
    'authorization_basis': 'continuing user-authorized isolated GPU development',
    'request': {'mode': 'skipEvaluate', 'sky': 'off', 'seconds': 120,
                'watchdog_seconds': 240, 'source_limit': 12000,
                'history_diagnostic': False, 'continue_session': None},
    'held_runs': {},
    'native_build': {'path': '<canonical build path>', 'provenance': {}, 'artifacts': {}},
    'helper': {'path': '<canonical runtime Helper exe>', 'sha256': '<digest>',
               'build_manifest': {'path': '<build.json>', 'sha256': '<digest>',
                                  'artifact_sha256': '<digest>'}},
    'runtime_pins_sha256': '<digest>',
    'predecessor': {'session': 'dev-loading-trace-01', 'files': {
        'manifest.json': '<digest>', 'result.json': '<digest>', 'game.log': '<digest>',
        'post-exit-observation.json': '<digest>', 'post-exit-kernel.jsonl': '<digest>'}},
}


def need(value, reason):
    if not value:
        raise ValueError('isolated context: ' + reason)


def regular(path, parent=None):
    path = Path(path)
    need(path.is_absolute() and path.resolve() == path and path.is_file()
         and not path.is_symlink(), 'canonical regular evidence required: ' + str(path))
    if parent is not None:
        need(path.is_relative_to(parent), 'evidence path escape')
    return path


def load(path):
    return json.loads(regular(path).read_text())


def request(args):
    return {k: getattr(args, k) for k in EXAMPLE_RECORD['request']}


def validate_scope(record, requested, boot, snapshot):
    need(record.get('schema') == 'nr-isolated-context-v1'
         and record.get('historical_faults_resolved') is False
         and record.get('authorization_basis'), 'explicit unresolved historical scope required')
    need(record.get('boot_id') == boot and record.get('previous_boot_id')
         and record['previous_boot_id'] != boot, 'new isolated boot required')
    need(record.get('held_runs') == snapshot and snapshot
         and all(x['boot_id'] == record['previous_boot_id'] for x in snapshot.values()),
         'new or changed HOLD')
    need(record.get('request') == requested, 'request drift')
    need(set(requested) == set(EXAMPLE_RECORD['request']), 'exact request fields required')
    need((requested['sky'], requested['mode']) in
         (('off', 'skipEvaluate'), ('persistent', 'skipEvaluate'), ('persistent', 'resident')),
         'diagnostic stage not allowed')
    need(all(type(requested[k]) is int for k in ('seconds', 'watchdog_seconds', 'source_limit'))
         and 1 <= requested['seconds'] <= 240
         and requested['seconds'] < requested['watchdog_seconds'] <= 360
         and 1 <= requested['source_limit'] <= 36000
         and type(requested['history_diagnostic']) is bool
         and requested['continue_session'] is None, 'bounded fresh-private diagnostic required')


def _kernel(r, boot):
    journal = subprocess.check_output(
        ['journalctl', '-k', '-b', '0', '--no-pager', '--output=json'],
        text=True, stderr=subprocess.PIPE, timeout=20)
    r.check_kernel_journal(journal, boot)


def _identities(r, record):
    native = record['native_build']
    build, artifacts, provenance = r.checked_native_build(native['path'])
    need(str(build) == native['path'] and provenance == native['provenance']
         and {str(p): r.sha(p) for p in artifacts} == native['artifacts'], 'Native build drift')
    helper = record['helper']
    regular(helper['path'], r.ROOT / 'build/helper-runtime-model')
    regular(helper['build_manifest']['path'], r.ROOT / 'build/helper-runtime-model')
    path, manifest = r.checked_runtime_helper(helper['path'], helper['build_manifest']['sha256'])
    need(str(path) == helper['path'] and r.sha(path) == helper['sha256']
         and manifest == helper['build_manifest'], 'Helper identity drift')
    pins = regular(r.ROOT / 'inputs/runtime-pins.json')
    need(r.sha(pins) == record['runtime_pins_sha256'], 'pin inventory drift')
    for pin in load(pins)['pins']:
        # Runtime pins may deliberately name system symlinks; their resolved bytes are pinned.
        need(r.sha(Path(pin['path'])) == pin['sha256'], 'runtime pin drift')


def _retirement(run):
    spec = importlib.util.spec_from_file_location('isolated_retirement', Path(__file__).with_name('retirement.py'))
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module.audit_session(run)


def _predecessor(r, record):
    pred = record['predecessor']
    run = r.checked_run(pred['session'])
    need(pred['session'] != record['session'], 'predecessor must be a different session')
    required = {'manifest.json', 'result.json', 'game.log',
                'post-exit-observation.json', 'post-exit-kernel.jsonl'}
    need(required <= pred['files'].keys(), 'missing predecessor evidence')
    # Pin every regular JSON/log used by the actual retirement auditor, including nested jobs.
    evidence = {str(p.relative_to(run)) for p in run.rglob('*')
                if p.is_file() and (p.suffix in ('.json', '.jsonl') or p.name == 'game.log')
                and not p.is_relative_to(run / 'profile')}
    need(evidence <= pred['files'].keys(), 'predecessor audit inputs not fully pinned')
    for name, digest in pred['files'].items():
        path = regular(run / name, run)
        need(r.sha(path) == digest, 'predecessor evidence drift')
    manifest = load(run / 'manifest.json')
    result = load(run / 'result.json')
    need(result.get('status') == 'PASS_NORMAL_TEARDOWN' and result.get('returncode') == 0
         and result.get('native_lastuse_proof') is True and result.get('residuals') == []
         and result.get('watchdog_expired') is False
         and result.get('supervisor_interrupted') is False
         and result.get('received_signals') == 0, 'unambiguous predecessor retirement required')
    native = record['native_build']
    need(manifest['session'] == pred['session']
         and manifest['host_safety']['boot_id'] == record['boot_id']
         and manifest['native_build_provenance'] == native['provenance']
         and all(manifest['artifacts'].get(p) == digest for p, digest in native['artifacts'].items()),
         'predecessor boot/build mismatch')
    observation = load(run / 'post-exit-observation.json')
    need(observation.get('boot_id') == record['boot_id']
         and observation.get('session') == pred['session']
         and observation.get('project_holders') == [] and observation.get('driver_errors') == []
         and observation.get('result_sha256') == r.sha(run / 'result.json')
         and observation.get('kernel_journal_sha256') == r.sha(run / 'post-exit-kernel.jsonl'),
         'predecessor post-exit observation mismatch')
    r.check_kernel_journal((run / 'post-exit-kernel.jsonl').read_text(), record['boot_id'])
    audit = _retirement(run)
    stage = (record['request']['sky'], record['request']['mode'])
    if stage == ('off', 'skipEvaluate'):
        need(audit['path'] == 'NO_PEER_NORMAL_EXIT' and manifest['sky'] == 'off'
             and manifest['mode'] == 'skipEvaluate', 'SR-only predecessor required')
    else:
        expected_sky = 'off' if stage == ('persistent', 'skipEvaluate') else 'persistent'
        helper = record['helper']
        continuing_resident = stage == ('persistent', 'resident') and manifest['mode'] == 'resident'
        need(audit['path'] == 'PEER_NORMAL_RETIREMENT' and manifest['sky'] == expected_sky
             and (manifest['mode'] == 'skipEvaluate' or continuing_resident) and manifest.get('runtime_model') is True
             and manifest.get('helper_build_manifest') == helper['build_manifest']
             and manifest['artifacts'].get(helper['path']) == helper['sha256'],
             'matching real Helper skip predecessor required')
        counts = audit['terminal_counts']
        dispositions = Counter(job['disposition'] for job in audit['jobs'])
        need(set(dispositions) <= {'expected_skip', 'captured', 'obsolete_discard'}
             and dispositions['expected_skip'] == counts['skip_jobs']
             and dispositions['captured'] == counts['captured_jobs']
             and dispositions['obsolete_discard'] == counts['obsolete_jobs'],
             'per-job dispositions must match terminal counts')
        for job in audit['jobs']:
            ordinal = job['job']
            complete = load(run / 'frames' / f'bucket-{ordinal // 64:06d}' /
                            f'async-complete-{pred["session"]}-g0-f{ordinal}.json')
            resident = manifest['mode'] == 'resident'
            need(complete.get('mode') == manifest['mode']
                 and type(complete.get('evaluate_calls')) is int
                 and complete['evaluate_calls'] == int(resident)
                 and complete.get('output_valid') is resident,
                 'actual Helper mode/Evaluate/output does not match predecessor')
        need(type(counts['finalized_jobs']) is int and counts['finalized_jobs'] > 0,
             'actual finalized jobs required')
        if continuing_resident:
            need(counts['captured_jobs'] > 0 and counts['skip_jobs'] == 0
                 and record['request']['source_limit'] <= 7200,
                 'bounded resident continuation needs actual NR capture retirement')
        else:
            need(counts['skip_jobs'] == counts['finalized_jobs']
                 and counts['captured_jobs'] == counts['obsolete_jobs'] == 0,
                 'actual skip jobs required')
            if stage == ('persistent', 'resident'):
                need(record['request']['source_limit'] <= 120,
                     'first resident diagnostic limited to 120 admitted packets')


def _record(r, path):
    path = regular(path, r.ROOT / 'inputs')
    need(path.parent == r.ROOT / 'inputs' and path.stat().st_size <= 262144,
         'bounded direct inputs record required')
    return path, load(path)


def _common(r, record, requested):
    boot = Path('/proc/sys/kernel/random/boot_id').read_text().strip()
    validate_scope(record, requested, boot, r.held_run_snapshot())
    _identities(r, record)
    _predecessor(r, record)
    _kernel(r, boot)


def check_launch(r, args, lock):
    need(lock is not None and Path(os.readlink('/proc/self/fd/' + str(lock.fileno())))
         == Path('/tmp/nrbridge-poc-gpu.lock'), 'GPU lock required')
    fcntl.flock(lock, fcntl.LOCK_EX | fcntl.LOCK_NB)
    with open('/tmp/nrbridge-poc-gpu.lock', 'a') as probe:
        try:
            fcntl.flock(probe, fcntl.LOCK_EX | fcntl.LOCK_NB)
        except BlockingIOError:
            pass
        else:
            raise ValueError('isolated context: GPU lock not exclusive')
    path, record = _record(r, args.context_recovery)
    need(record['session'] == args.session and not r.checked_run(args.session).exists(),
         'exact unused session required')
    need(str(Path(args.native_build or r.ROOT / 'build/native').absolute()) == record['native_build']['path']
         and args.runtime_model_helper == record['helper']['path']
         and args.runtime_model_build_sha256 == record['helper']['build_manifest']['sha256'],
         'launch binary options drift')
    need(not r.holders(), 'project GPU holder remains')
    _common(r, record, request(args))
    return {'boot_id': record['boot_id'], 'unresolved_driver_hang': False,
            'historical_faults_resolved': False,
            'scope': 'bounded isolated context diagnostic; historical causes OPEN',
            'context_recovery': {'path': str(path), 'sha256': r.sha(path)},
            'request': record['request']}


def _supervisor(r, run):
    identity = load(run / 'runner.json')['runner']
    need(r.live(identity['pid']) == identity, 'supervisor identity changed')
    proc = Path('/proc') / str(identity['pid'])
    for fd in (proc / 'fd').iterdir():
        try:
            if os.readlink(fd) != '/tmp/nrbridge-poc-gpu.lock':
                continue
            info = (proc / 'fdinfo' / fd.name).read_text()
            if re.search(r'^lock:\s+\d+:\s+FLOCK\s+ADVISORY\s+WRITE\s+'
                         + str(identity['pid']) + r'\s', info, re.M):
                return
        except OSError:
            continue
    raise ValueError('isolated context: supervisor GPU lock missing')


def check_control(r, run, game, command):
    if command not in ('start-helper', 'arm'):
        return
    manifest = load(run / 'manifest.json')
    safety = manifest.get('host_safety', {})
    binding = safety.get('context_recovery')
    if binding is None:
        need('request' not in safety and safety.get('historical_faults_resolved') is not False,
             'isolated recovery binding missing')
        return
    path, record = _record(r, binding['path'])
    need(r.sha(path) == binding['sha256'] and record['session'] == run.name
         and safety.get('boot_id') == record['boot_id']
         and safety.get('historical_faults_resolved') is False
         and manifest['session'] == run.name and safety.get('request') == record['request'],
         'session recovery binding drift')
    req = record['request']
    need(manifest['mode'] == req['mode'] and manifest['sky'] == req['sky']
         and manifest['planned_seconds'] == req['seconds']
         and manifest['watchdog_seconds'] == req['watchdog_seconds']
         and manifest.get('continuation') is None
         and manifest['environment']['BB_NR_NATIVE_ASYNC_SOURCE_LIMIT'] == str(req['source_limit'])
         and (manifest['environment'].get('BB_NATIVE_HISTORY_DIAGNOSTIC') == '1') is req['history_diagnostic']
         and manifest['native_build'] == record['native_build']['path']
         and manifest['native_build_provenance'] == record['native_build']['provenance']
         and manifest.get('runtime_model') is True
         and manifest.get('helper_build_manifest') == record['helper']['build_manifest']
         and all(manifest['artifacts'].get(p) == digest for p, digest in
                 (record['native_build']['artifacts'] | {record['helper']['path']: record['helper']['sha256']}).items()),
         'manifest request/build drift')
    _supervisor(r, run)
    need(r.live(game['pid']) == game, 'game identity changed')
    for holder in r.holders():
        if holder['pid'] == game['pid'] and holder['start_ticks'] == game['start_ticks']:
            continue
        proc = Path('/proc') / str(holder['pid'])
        env = (proc / 'environ').read_bytes().split(b'\0')
        exe = Path(holder['exe']).name
        need((exe.startswith(('wine', 'wineserver')) or exe.endswith('.exe'))
             and ('WINEPREFIX=' + str(run / 'profile/wine-prefix')).encode() in env,
             'foreign project holder')
    _common(r, record, req)
