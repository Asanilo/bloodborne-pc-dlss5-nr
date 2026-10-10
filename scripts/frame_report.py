#!/usr/bin/env python3
"""Summarize real-render frame traces from BB_SAVE_LOG=1; compare identical routes."""
import argparse
import csv
import json
import math
from pathlib import Path


def summarize(path, warmup=5.0):
    with path.open(newline="") as stream:
        reader = csv.DictReader(stream)
        if not {"t_s", "frame_ms"}.issubset(reader.fieldnames or []):
            raise ValueError("Expected BB_FRAME_LOG columns t_s and frame_ms")
        rows = []
        for source in reader:
            try:
                row = {k: float(v) for k, v in source.items() if v not in (None, "")}
            except (TypeError, ValueError):
                continue  # a final truncated row from a crash
            if all(math.isfinite(v) for v in row.values()) and row["t_s"] >= warmup and row["frame_ms"] > 0:
                rows.append(row)
    if not rows:
        raise ValueError("No valid frames remain after warm-up")
    times = sorted(r["frame_ms"] for r in rows)
    percentile = lambda p: times[max(0, math.ceil(len(times) * p) - 1)]
    slowest = sorted(rows, key=lambda r: r["frame_ms"], reverse=True)[:10]
    return {"file": str(path), "frames": len(rows), "warmup_s": warmup,
            "render_fps": 1000 * len(times) / sum(times),
            "median_ms": percentile(.5), "p95_ms": percentile(.95), "p99_ms": percentile(.99),
            "worst_ms": times[-1], "over_33ms": sum(t > 33.333 for t in times),
            "over_50ms": sum(t > 50 for t in times),
            "tracked_image_mb_peak": max(r.get("image_mb", 0) for r in rows),
            "tracked_buffer_mb_peak": max(r.get("buffer_mb", 0) for r in rows),
            "slowest_frames": slowest}


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("traces", type=Path, nargs="+")
    parser.add_argument("--warmup", type=float, default=5)
    args = parser.parse_args()
    try:
        print(json.dumps([summarize(p, args.warmup) for p in args.traces], indent=2))
    except (OSError, ValueError) as error:
        parser.exit(1, f"{error}\n")
