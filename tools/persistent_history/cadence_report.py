#!/usr/bin/env python3
"""Read-only candidate/transition/bank telemetry report, never GPU or quality acceptance."""
import argparse, bisect, collections, hashlib, json, math, statistics
from pathlib import Path


def fields(line):
    return dict(piece.split("=", 1) for piece in line.split()[1:] if "=" in piece)


def summarize(run):
    logs = [p for p in (run / "game.log.1", run / "game.log") if p.is_file()]
    if not logs or any(p.is_symlink() for p in logs):
        raise ValueError("regular retained game logs required")
    text = b"".join(p.read_bytes() for p in logs).decode(errors="replace")
    current, begins, finishes, deferred, samples, banks, participation = {}, {}, [], [], {}, {}, {}
    for line in text.splitlines():
        if not line.startswith(("PERSISTENT_HISTORY_", "NR_HISTORY_")):
            continue
        f = fields(line)
        if "source" not in f:
            continue
        source = int(f["source"])
        if line.startswith("PERSISTENT_HISTORY_CURRENT "):
            current[source] = f
        elif line.startswith("PERSISTENT_HISTORY_TRANSITION_BEGIN "):
            begins[source] = f
        elif line.startswith("PERSISTENT_HISTORY_TRANSITION_FINISH "):
            finishes.append(source)
        elif line.startswith("PERSISTENT_HISTORY_CANDIDATE_DEFERRED "):
            deferred.append(source)
        elif line.startswith("NR_HISTORY_GPU_SAMPLE "):
            samples[source] = f
        elif line.startswith("NR_HISTORY_PARTICIPATION_SAMPLE "):
            participation[source] = f
        elif line.startswith("NR_HISTORY_BANK_SAMPLE "):
            banks[source] = f
    starts = sorted(begins)
    intervals = [b-a for a,b in zip(starts, starts[1:])]
    phase = collections.defaultdict(list)
    causes = collections.Counter()
    rows = []
    for source, f in sorted(samples.items()):
        valid = float(f["valid"])
        if not math.isfinite(valid) or not 0 <= valid <= 1:
            raise ValueError("invalid valid fraction")
        index = bisect.bisect_right(starts, source)-1
        offset = source-starts[index] if index >= 0 else None
        bank = banks.get(source)
        row = dict(source=source, valid=valid, changed=float(f["edited_buffer_changed"]),
                   mean_age_frames=float(f["mean_age_frames"]), mean_age_ms=float(f["mean_age_ms"]),
                   readback_monotonic_ns=int(f["monotonic_ns"]), candidate_source_offset=offset)
        if offset is not None:
            phase[offset].append(valid)
        if bank:
            statuses = [int(bank[f"invalid{k}"]) for k in range(4)]
            active_bad = bool(statuses[int(bank["active"])] & ~16)
            target_bad = bool(statuses[int(bank["candidate"])] & ~16)
            transition = bank["transition"] == "1"
            no_bank = active_bad and (not transition or target_bad)
            guard = int(bank["return_guard"])
            row.update(bank_invalid=statuses, transition=transition, weight=float(bank["weight"]),
                       active_bank_fatal=active_bad, target_bank_fatal=target_bad, return_guard=guard,
                       both_display_banks_fatal=no_bank)
            if valid == 0:
                # Coincident gates, not an attribution of which happened first.
                causes["zero_valid_with_return_guard" if guard else
                       "zero_valid_with_all_display_banks_fatal" if no_bank else
                       "zero_valid_without_bank_or_return_gate"] += 1
        elif valid == 0:
            causes["zero_valid_without_bank_readback"] += 1
        detail=participation.get(source)
        if detail:
            parsed={k:(int(v) if k in ("candidate_current","target_current") or k.startswith("reject_") else float(v))
                    for k,v in detail.items() if k not in ("source","scope","masks","target_may_be_stale")}
            row["participation"]=parsed
        rows.append(row)
    result = json.loads((run / "result.json").read_text()) if (run / "result.json").is_file() else {}
    return dict(schema="nr-cadence-report-v1", run=str(run.resolve()),
                retained_log_sha256={p.name:hashlib.sha256(p.read_bytes()).hexdigest() for p in logs},
                retained_current_frames=len(current), retained_candidate_begins=len(starts),
                candidate_interval_frames=dict(median=statistics.median(intervals) if intervals else None,
                    histogram=dict(sorted(collections.Counter(intervals).items()))),
                transition_finishes=len(finishes), deferred_candidates=len(deferred),
                completed_samples=len(rows), completed_bank_samples=len(banks),
                completed_participation_samples=len(participation),
                active_empty_samples=sum(float(v["active_live"])==0 for v in participation.values()),
                zero_valid_coincident_gates=dict(causes),
                valid_by_candidate_offset={str(k):dict(samples=len(v),mean=statistics.mean(v),
                    min=min(v),max=max(v)) for k,v in sorted(phase.items())},
                normal_retirement=result.get("status"), rows=rows,
                limitations=["Retained logs may be rotated; no missing frames are invented.",
                    "Sparse sampling can alias candidate cadence; use interval=1 for a bounded diagnostic.",
                    "readback timestamp is completion observation, not exact input/Present timestamp.",
                    "Bank gates and validity are evidence, not a visual quality or root-cause verdict."])


def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument("run",type=Path); p.add_argument("--output",type=Path)
    a=p.parse_args(); report=summarize(a.run)
    if a.output:
        with a.output.open("x") as f: json.dump(report,f,indent=2);f.write("\n")
    else: print(json.dumps(report,indent=2))


if __name__ == "__main__": main()
