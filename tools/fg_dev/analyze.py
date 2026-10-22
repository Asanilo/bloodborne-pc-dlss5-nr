#!/usr/bin/env python3
"""Read FG_PRESENT records. Successful queue presents are not physical scanout measurements."""
import argparse
import collections
import json
from pathlib import Path
import re
import statistics


def percentile(values, p):
    return sorted(values)[min(len(values) - 1, int((len(values) - 1) * p))] if values else None


def analyze(text):
    records = []
    for line in text.splitlines():
        if not line.startswith('FG_PRESENT '):
            continue
        fields = dict(re.findall(r'(\w+)=(\S+)', line))
        records.append({key: value if key in ('kind', 'scope') else int(value)
                        for key, value in fields.items()})
    counts = collections.Counter(r['kind'] for r in records if r['success'])
    failed = sum(not r['success'] for r in records)
    errors, pairs, previous = [], 0, None
    intervals = {'all': [], 'real': [], 'generated': []}
    last = {}
    for r in records:
        if not r['success']:
            previous = None
            continue
        if r['kind'] == 'generated':
            if previous and previous['kind'] == 'generated':
                errors.append('consecutive generated presents')
            if not r['fg_frame']:
                errors.append('generated present without FG identity')
        elif r['kind'] == 'real' and previous and previous['kind'] == 'generated':
            if (r['source_frame'] != previous['source_frame'] or r['fg_frame'] != previous['fg_frame']
                    or r.get('source_game_frame') != previous.get('source_game_frame')):
                errors.append('generated/real identity mismatch')
            else:
                pairs += 1
        for key in ('all', r['kind']):
            if key not in intervals:
                continue
            if key in last:
                intervals[key].append((r['cpu_monotonic_ns'] - last[key]) / 1e6)
            last[key] = r['cpu_monotonic_ns']
        previous = r
    return {'records': len(records), 'successful_presents': dict(counts), 'failed_presents': failed,
            'matched_generated_then_real_pairs': pairs, 'order_errors': errors,
            'generated_per_real': counts['generated'] / counts['real'] if counts['real'] else None,
            'interval_ms': {k: {'median': statistics.median(v) if v else None,
                               'p95': percentile(v, .95), 'max': max(v) if v else None}
                            for k, v in intervals.items()},
            'scope': 'CPU successful Present-return intervals; not scanout, input latency or image quality'}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('logs', nargs='+', type=Path)
    args = parser.parse_args()
    print(json.dumps(analyze(''.join(p.read_text(errors='replace') for p in args.logs)), indent=2))


if __name__ == '__main__':
    main()
