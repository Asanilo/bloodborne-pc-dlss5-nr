#!/usr/bin/env python3
"""Summarize a frames.csv from the port's frame statistics.

Columns: t_s, frame_ms, ...  — frame_ms is the real frame interval.

usage: frames_summary.py <frames.csv>
"""
import csv
import statistics
import sys
from pathlib import Path


def percentile(values, fraction):
    ordered = sorted(values)
    return ordered[min(len(ordered) - 1, int(len(ordered) * fraction))]


def main() -> None:
    path = Path(sys.argv[1])
    if not path.is_file() or path.stat().st_size == 0:
        print('FRAMES_SUMMARY missing or empty:', path)
        return
    rows = []
    with path.open(newline='') as stream:
        for row in csv.DictReader(stream):
            try:
                rows.append((float(row['t_s']), float(row['frame_ms'])))
            except (KeyError, TypeError, ValueError):
                continue
    intervals = [ms for _, ms in rows if ms > 0]
    if len(intervals) < 10:
        print('FRAMES_SUMMARY too few frames:', len(intervals))
        return
    # Ignore startup/loading outliers (the first 2 s and absurd gaps).
    steady = [ms for t, ms in rows if t >= 2.0 and 0 < ms < 500]
    if len(steady) < 10:
        steady = intervals
    fps = [1000.0 / ms for ms in steady]
    print('FRAMES_SUMMARY frames=%d window=%.1fs' % (len(steady), (rows[-1][0] - rows[0][0])))
    print('  fps mean=%.1f median=%.1f p1=%.1f p5=%.1f max=%.1f' % (
        statistics.mean(fps), statistics.median(fps),
        percentile(fps, 0.01), percentile(fps, 0.05), max(fps)))
    print('  frame_ms median=%.2f p95=%.2f p99=%.2f max=%.2f' % (
        statistics.median(steady), percentile(steady, 0.95),
        percentile(steady, 0.99), max(steady)))
    # 5-second windows of mean fps.
    origin = rows[0][0]
    buckets = {}
    for t, ms in rows:
        if t < 2.0 or not (0 < ms < 500):
            continue
        bucket = int((t - origin) // 5)
        buckets.setdefault(bucket, []).append(1000.0 / ms)
    print('  5s windows:', ' '.join('%d:%.0f' % (k, statistics.mean(v))
                                    for k, v in sorted(buckets.items())))


if __name__ == '__main__':
    main()
