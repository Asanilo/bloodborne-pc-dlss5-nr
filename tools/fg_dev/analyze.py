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
        start = line.find('FG_PRESENT ')
        if start < 0:
            continue
        # Another writer can leave an unterminated diagnostic before this
        # complete record (observed with "Write fault sites ... faults").
        line = line[start:]
        fields = dict(re.findall(r'(\w+)=(\S+)', line))
        records.append({key: value if key in ('kind', 'scope') else int(value)
                        for key, value in fields.items()})
    counts = collections.Counter(r['kind'] for r in records if r['success'])
    failed = sum(not r['success'] for r in records)
    errors, pairs, previous = [], 0, None
    complete_batches=0;partial_batches=0;batch=[]
    intervals = {'all': [], 'real': [], 'generated': []}
    last = {}
    for r in records:
        if not r['success']:
            previous = None;batch=[]
            continue
        if r['kind'] == 'generated':
            count=r.get('generated_count',1);index=r.get('generated_index',1)
            if not 1<=index<=count<=5:errors.append('invalid generated index/count')
            if previous and previous['kind']=='generated':
                if count==1:errors.append('consecutive generated presents')
                elif (index!=previous.get('generated_index',1)+1 or count!=previous.get('generated_count',1)
                      or any(r.get(k)!=previous.get(k) for k in ('source_frame','fg_frame','source_game_frame','fg_epoch'))):
                    errors.append('MFG output order/identity mismatch')
            elif previous and index!=1:errors.append('MFG batch starts after first output')
            batch.append(r)
            if not r['fg_frame']:
                errors.append('generated present without FG identity')
        elif r['kind'] == 'real' and previous and previous['kind'] == 'generated':
            if previous.get('generated_index',1)!=previous.get('generated_count',1):errors.append('MFG batch incomplete before real')
            if (r['source_frame'] != previous['source_frame'] or r['fg_frame'] != previous['fg_frame']
                    or r.get('source_game_frame') != previous.get('source_game_frame')
                    or r.get('fg_epoch')!=previous.get('fg_epoch')):
                errors.append('generated/real identity mismatch')
            else:
                pairs += 1
                count=previous.get('generated_count',1)
                identity=('source_frame','fg_frame','source_game_frame','fg_epoch')
                if (len(batch)==count and [x.get('generated_index',1) for x in batch]==list(range(1,count+1))
                    and all(x.get('generated_count',1)==count and all(x.get(k)==r.get(k) for k in identity) for x in batch)):
                    complete_batches+=1
                else:partial_batches+=1
            batch=[]
        for key in ('all', r['kind']):
            if key not in intervals:
                continue
            if key in last:
                intervals[key].append((r['cpu_monotonic_ns'] - last[key]) / 1e6)
            last[key] = r['cpu_monotonic_ns']
        previous = r
    return {'records': len(records), 'successful_presents': dict(counts), 'failed_presents': failed,
            'matched_generated_then_real_pairs': pairs, 'complete_generated_batches':complete_batches,
            'partial_or_invalid_batches':partial_batches, 'order_errors': errors,
            'generated_per_real': counts['generated'] / counts['real'] if counts['real'] else None,
            'interval_ms': {k: {'median': statistics.median(v) if v else None,
                               'p95': percentile(v, .95), 'p99': percentile(v,.99), 'max': max(v) if v else None}
                            for k, v in intervals.items()},
            'scope': 'CPU successful Present-return intervals; not scanout, input latency or image quality'}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('logs', nargs='+', type=Path)
    args = parser.parse_args()
    print(json.dumps(analyze(''.join(p.read_text(errors='replace') for p in args.logs)), indent=2))


if __name__ == '__main__':
    main()
