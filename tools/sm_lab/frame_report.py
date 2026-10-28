#!/usr/bin/env python3
"""Summarize frame-count output: how often displayed content actually changed.

usage: frame_report.py <file>   (output of tools/sm_lab/frame_count.c)
"""
import statistics
import sys
from pathlib import Path


def main() -> None:
    path = Path(sys.argv[1])
    frames = []
    for line in path.read_text().splitlines():
        if line.startswith('FRAME '):
            _, index, ts, digest = line.split()
            frames.append((int(index), int(ts), digest))
    if len(frames) < 2:
        print('not enough frames')
        return
    duration = (frames[-1][1] - frames[0][1]) / 1e9
    changes = []
    previous = None
    for index, ts, digest in frames:
        if previous is not None and digest != previous:
            changes.append(ts)
        previous = digest
    intervals = [(b - a) / 1e6 for a, b in zip(changes, changes[1:])]
    per_second = {}
    origin = frames[0][1]
    for ts in changes:
        second = int((ts - origin) / 1e9)
        per_second[second] = per_second.get(second, 0) + 1
    summary = {
        'captures': len(frames),
        'capture_rate_hz': round(len(frames) / duration, 1) if duration else None,
        'duration_s': round(duration, 3),
        'content_changes': len(changes),
        'content_rate_hz': round(len(changes) / duration, 1) if duration else None,
        'change_interval_ms': {
            'median': round(statistics.median(intervals), 2) if intervals else None,
            'p95': round(sorted(intervals)[int(len(intervals) * 0.95)], 2) if intervals else None,
            'min': round(min(intervals), 2) if intervals else None,
            'max': round(max(intervals), 2) if intervals else None,
        },
        'changes_per_second': dict(sorted(per_second.items())),
    }
    print(summary)


if __name__ == '__main__':
    main()
