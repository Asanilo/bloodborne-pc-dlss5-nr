#!/usr/bin/env python3
"""Summarize frame-count output: displayed content updates by change amplitude.

Each capture carries a sampled difference against the previous capture in
permille of sampled pixels. This module reports **candidate statistics only**:
a steady stream of above-threshold differences can come from new game frames,
but also from a steady compositor effect, and nothing here can tell them apart
without trusted frame identity or capture provenance.

usage: frame_report.py <file> [threshold_permille]
"""
import statistics
import sys
from pathlib import Path

THRESHOLDS = (1, 5, 10, 25)


def summarize(path: Path, threshold: int = 5) -> dict:
    frames = []
    for line in path.read_text().splitlines():
        if not line.startswith('FRAME '):
            continue
        parts = line.split()
        if len(parts) < 5:
            raise SystemExit('frame-count output lacks the diff column; rebuild it')
        # FRAME <index> <monotonic_ns> <hash> <diff_permille>
        frames.append((int(parts[1]), int(parts[2]), parts[3], int(parts[4])))
    if len(frames) < 2:
        return {'error': 'not enough frames'}

    origin = frames[0][1]
    duration = (frames[-1][1] - origin) / 1e9
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

    # Whole seconds from the first capture, missing seconds counted as zero. The
    # trailing partial second is reported separately and never used for stability.
    complete_seconds = int(duration)
    per_second = {second: 0 for second in range(complete_seconds)}
    tail_updates = 0
    for _, timestamp, _, diff in frames:
        if diff < threshold:
            continue
        offset = (timestamp - origin) / 1e9
        if offset >= complete_seconds:
            tail_updates += 1
        else:
            per_second[int(offset)] += 1

    # Sub-threshold differences are compositor redraws; if any exist, an "update"
    # count cannot be attributed to new frames.
    below = [d for d in diffs[1:] if 0 < d < threshold]
    noise_floor = statistics.median(below) if below else 0
    counts = list(per_second.values())
    if complete_seconds >= 2:
        stable = max(counts) <= 1.25 * min(counts)
    else:
        stable = False
    candidate = bool(updates) and noise_floor == 0 and stable
    return {
        'captures': len(frames),
        'duration_s': round(duration, 3),
        'complete_seconds': complete_seconds,
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
        'tail_updates': tail_updates,
        'noise_floor_permille': noise_floor,
        'sub_threshold_frames': len(below),
        'per_second_stable': stable,
        # Candidate statistics, not proof: a steady above-threshold compositor
        # effect looks exactly like a steady stream of new frames.
        'candidate_consistent': candidate,
        'interpolation_proven': False,
        'provenance': 'unverified_capture',
        'limits': ('A steady above-threshold update stream is indistinguishable from '
                   'compositor pollution; trusted frame identity or independent '
                   'calibration is required before any interpolation claim.'),
    }


def main() -> None:
    path = Path(sys.argv[1])
    threshold = int(sys.argv[2]) if len(sys.argv) > 2 else 5
    print(summarize(path, threshold))


if __name__ == '__main__':
    main()
