#!/usr/bin/env python3
"""Offline, partial CPU-hook observations. Never establishes GPU safety/causality."""
import argparse
import hashlib
import json
from pathlib import Path
import re

MAX = (1 << 64) - 1
MARKER = 'NR_RESOURCE_TRACE'
DEC = {'monotonic_ns', 'seq', 'tid', 'id', 'bytes', 'retire_tick',
       'observed_tick', 'unknown_tick', 'cap', 'truncated'}
HEX = {'handle', 'bda', 'scheduler'}
TEXT = {'type', 'event', 'completion', 'scope'}


def uint64(value):
    if not re.fullmatch(r'(?:0x[0-9a-fA-F]+|[0-9]+)', value):
        raise ValueError('expected unsigned decimal or hexadecimal integer')
    number = int(value, 16 if value.startswith('0x') else 10)
    if number > MAX:
        raise ValueError('integer exceeds uint64')
    return number


def parse_record(line):
    if not line.endswith(b'\n') or len(line) > 512:
        raise ValueError('partial or oversized trace line')
    text = line.decode('ascii').strip()
    if not text.startswith(MARKER + ' '):
        raise ValueError('trace marker is not at line start')
    record = {}
    for token in text.split()[1:]:
        if token.count('=') != 1:
            raise ValueError('invalid key/value token')
        key, value = token.split('=')
        if key in record or key not in DEC | HEX | TEXT:
            raise ValueError('duplicate or unknown field')
        if key in DEC:
            if not re.fullmatch(r'[0-9]+', value):
                raise ValueError('invalid decimal')
            record[key] = uint64(value)
        elif key in HEX:
            if not re.fullmatch(r'0x[0-9a-fA-F]{16}', value):
                raise ValueError('invalid fixed-width hexadecimal')
            record[key] = uint64(value)
        else:
            if not re.fullmatch(r'[A-Za-z0-9_-]{1,24}', value):
                raise ValueError('invalid text token')
            record[key] = value
    if record.keys() != DEC | HEX | TEXT:
        raise ValueError('missing fields')
    if (record['completion'] != 'NOT_INFERRED' or record['unknown_tick'] != 0
            or record['scope'] != 'linked_image_cpu_hooks' or record['cap'] != 4096):
        raise ValueError('unsupported trace contract')
    if record['tid'] == 0 or record['seq'] == 0:
        raise ValueError('invalid thread or sequence')
    if record['type'] == 'trace' and record['event'] == 'truncated':
        if record['truncated'] != 1 or record['seq'] != 4097 or any(
                record[k] for k in ('id', 'handle', 'bda', 'bytes', 'scheduler',
                                    'retire_tick', 'observed_tick')):
            raise ValueError('invalid truncation marker')
    elif (record['type'], record['event']) not in {
            ('buffer', 'create'), ('buffer', 'destroy_begin'), ('buffer', 'destroy_end'),
            ('sampler_gc', 'schedule'), ('sampler_gc', 'destroy_begin'),
            ('sampler_gc', 'destroy_end')}:
        raise ValueError('unsupported event')
    elif record['id'] == 0 or record['truncated'] != 0 or record['seq'] > 4096:
        raise ValueError('invalid event identity')
    if record['bda'] + record['bytes'] > 1 << 64:
        raise ValueError('address interval overflows uint64 address space')
    return record


def analyze(log, address, fault_ns=None):
    address = uint64(address) if isinstance(address, str) else address
    if not isinstance(address, int) or not 0 <= address <= MAX:
        raise ValueError('invalid target address')
    if fault_ns is not None and (not isinstance(fault_ns, int) or not 0 < fault_ns <= MAX):
        raise ValueError('fault time must be a nonzero uint64 monotonic ns')
    raw = Path(log).read_bytes()
    events, malformed, issues = [], [], []
    last_seq = last_time = 0
    truncated = False
    for number, line in enumerate(raw.splitlines(keepends=True), 1):
        if MARKER.encode() not in line:
            continue
        try:
            r = parse_record(line)
        except (ValueError, UnicodeError) as error:
            malformed.append({'line': number, 'reason': str(error)})
            continue
        r['line'] = number
        if r['seq'] != last_seq + 1:
            issues.append({'line': number, 'reason': 'sequence discontinuity or mixed linked images'})
        if last_time and r['monotonic_ns'] and r['monotonic_ns'] < last_time:
            issues.append({'line': number, 'reason': 'monotonic time regressed'})
        last_seq = r['seq']
        last_time = r['monotonic_ns']
        if r['type'] == 'trace':
            truncated = True
        else:
            if truncated:
                issues.append({'line': number, 'reason': 'event after truncation marker'})
            events.append(r)
    groups = {}
    for r in events:
        groups.setdefault((r['type'], r['id']), []).append(r)

    def selected(records):
        return [r for r in records if fault_ns is None or not r['monotonic_ns']
                or r['monotonic_ns'] <= fault_ns]

    def summary(records, start_event):
        rs = selected(records)
        starts = [r for r in rs if r['event'] == start_event]
        begins = [r for r in rs if r['event'] == 'destroy_begin']
        ends = [r for r in rs if r['event'] == 'destroy_end']
        problems = []
        if issues:
            problems.append('trace sequence/time integrity uncertain; identity join not reliable')
        if len(starts) != 1:
            problems.append('missing or ambiguous start identity')
        # Never infer destroy association by handle alone, or accept conflicting id reuse.
        identities = {(r['handle'], r['bda'], r['bytes'], r['scheduler'], r['retire_tick']) for r in rs}
        if len(identities) != 1:
            problems.append('conflicting fields for same resource id')
        if len(begins) > 1 or len(ends) > 1:
            problems.append('duplicate destroy records')
        if ends and (not begins or begins[0]['line'] >= ends[0]['line']):
            problems.append('destroy end without ordered begin')
        if starts and begins and starts[0]['line'] >= begins[0]['line']:
            problems.append('destroy precedes start')
        if any(not r['monotonic_ns'] for r in rs):
            problems.append('unknown event time')
        status = ('ambiguous_or_incomplete' if problems else
                  'destroy_end_observed' if ends else
                  'destroy_begin_without_end_observed' if begins else
                  'start_observed_no_destroy_record')
        return {'records': rs, 'observation': status, 'limitations': problems,
                'records_after_fault_excluded': len(records) - len(rs)}

    matches = []
    creates_after = 0
    orphans = []
    sampler = []
    for (kind, identity), records in groups.items():
        start = 'create' if kind == 'buffer' else 'schedule'
        data = summary(records, start)
        data.update(type=kind, id=identity)
        if kind == 'buffer':
            creates = [r for r in records if r['event'] == 'create']
            if not creates:
                orphans.append(data)
            covering = [r for r in creates if r['bda'] != 0 and r['bytes'] > 0
                        and r['bda'] <= address < r['bda'] + r['bytes']]
            eligible = selected(covering)
            creates_after += len(covering) - len(eligible)
            if eligible:
                data['covering_create_lines'] = [r['line'] for r in eligible]
                matches.append(data)
        else:
            if not data['records']:
                continue
            ends = [r for r in data['records'] if r['event'] == 'destroy_end']
            data['tick_observation'] = 'not_checkable'
            if not data['limitations'] and len(ends) == 1:
                end = ends[0]
                if end['scheduler'] and end['retire_tick'] and end['observed_tick']:
                    data['tick_observation'] = ('observed_at_or_after_retire' if
                        end['observed_tick'] >= end['retire_tick'] else 'observed_below_retire')
            sampler.append(data)
    return {
        'schema': 'nr-resource-trace-analysis-v1',
        'log': {'path': str(Path(log).resolve()), 'sha256': hashlib.sha256(raw).hexdigest(),
                'bytes': len(raw)},
        'target_address': f'0x{address:016x}', 'fault_monotonic_ns': fault_ns,
        'scope': 'partial linked-image CPU hooks; not all Vulkan resources; excludes SDK images',
        'conclusion': 'observations only; no match never establishes safety or excludes a cause; '
                      'destroy calls and tick observations do not establish GPU completion or Xid causality',
        'coverage': {'complete': False, 'trace_records': len(events), 'truncated': truncated,
                     'trace_records_absent': not events,
                     'malformed_records': malformed, 'sequence_or_time_issues': issues,
                     'unknown_time_records': sum(not r['monotonic_ns'] for r in events),
                     'fault_time_known': fault_ns is not None,
                     'limitations': ['hooks cover only emitted instrumented resource events',
                                     'absence of records and log termination do not prove complete lifetimes',
                                     ('fault time assumed to share the log monotonic clock and boot; '
                                      'clock correspondence is not independently verified' if fault_ns is not None else
                                      'without fault time, records cannot be placed before or after Xid')],
                     'matching_creates_after_fault_excluded': creates_after},
        'buffer_matches': matches, 'buffer_destroy_without_create': orphans,
        'sampler_gc_observations': sampler,
    }


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--game-log', required=True, type=Path)
    parser.add_argument('--xid-address', required=True, type=uint64)
    parser.add_argument('--fault-monotonic-ns', type=uint64)
    parser.add_argument('--output', required=True, type=Path)
    args = parser.parse_args(argv)
    report = analyze(args.game_log, args.xid_address, args.fault_monotonic_ns)
    # Exclusive create also protects existing symlinks and race-time replacement.
    with args.output.open('x', encoding='utf-8') as stream:
        json.dump(report, stream, indent=2)
        stream.write('\n')


if __name__ == '__main__':
    main()
