#!/usr/bin/env python3
"""Summarize frame-count output: displayed content updates by change amplitude.

Each capture carries a sampled difference against the previous capture in
permille of sampled pixels. Compositor redraws/dithering move a few samples; a
new game frame moves most of them. Counting only captures above a threshold
filters the compositor's own work, which plain hashing could not.

usage: frame_report.py <file> [threshold_permille]
"""
import statistics
import sys
from pathlib import Path

THRESHOLDS = (1, 5, 10, 25)


def main() -> None:
    path = Path(sys.argv[1])
    threshold = int(sys.argv[2]) if len(sys.argv) > 2 else 5
    frames = []
    for line in path.read_text().splitlines():
        if not line.startswith('FRAME '):
            continue
        parts = line.split()
        if len(parts) < 5:
            raise SystemExit('frame-count output lacks the diff column; rebuild it')
        frames.append((int(parts[1]), int(parts[2]), parts[3], int(parts[4])))
    if len(frames) < 2:
        print('not enough frames')
        return
    duration = (frames[-1][1] - frames[0][1]) / 1e9
    diffs = [f[3] for f in frames]
    per_threshold = {}
    for limit in THRESHOLDS:
        hits = [f for f in frames if f[3] >= limit]
        per_threshold[limit] = {
            'updates': len(hits),
            'rate_hz': round(len(hits) / duration, 1) if duration else None,
        }
    updates = [f for f in frames if f[3] >= threshold]
    intervals = [(b[1] - a[1]) / 1e6 for a, b in zip(updates, updates[1:])]
    per_second = {}
    origin = frames[0][1]
    for ts, _, _, diff in frames:
        if diff >= threshold:
            second = int((ts - origin) / 1e9)
            per_second[second] = per_second.get(second, 0) + 1
    print({
        'captures': len(frames),
        'duration_s': round(duration, 3),
        'threshold_permille': threshold,
        'updates': len(updates),
        'update_rate_hz': round(len(updates) / duration, 1) if duration else None,
        'update_interval_ms': {
            'median': round(statistics.median(intervals), 2) if intervals else None,
            'p95': round(sorted(intervals)[int(len(intervals) * 0.95)], 2) if intervals else None,
        },
        'by_threshold': per_threshold,
        'diff_permille': {
            'median': statistics.median(diffs),
            'p95': sorted(diffs)[int(len(diffs) * 0.95)],
            'max': max(diffs),
        },
        'updates_per_second': dict(sorted(per_second.items())),
    })


if __name__ == '__main__':
    main()
