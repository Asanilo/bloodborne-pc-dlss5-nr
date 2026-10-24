#!/usr/bin/env python3
"""Measure a live managed session's actual real Present returns and sampled VRAM.

Enable BB_MEMORY_PROFILE=1 in the private settings' extra_env. Call after observing
the same loaded scene. Output is descriptive CPU Present timing, never scanout or
GPU completion evidence. The existing Runner remains the sole retirement owner.
"""
import argparse
import hashlib
import json
import os
from pathlib import Path
import time

import metrics
import runner


def retained(run):
    return ''.join(p.read_text(errors='replace') for p in
                   (run / 'game.log.1', run / 'game.log') if p.is_file())


class PresentTail:
    """Read each captured byte once, following the two rotating log inodes."""
    def __init__(self):
        self.streams = {}

    def read(self, run):
        live, rows = set(), []
        for name in ('game.log.1', 'game.log'):
            try:
                stream = (run / name).open('rb')
            except FileNotFoundError:
                continue
            with stream:
                info = os.fstat(stream.fileno())
                key = (info.st_dev, info.st_ino); live.add(key)
                offset, pending = self.streams.get(key, (0, b''))
                if info.st_size < offset:
                    raise ValueError('real Present capture was truncated')
                stream.seek(offset); chunk = stream.read(8 * 1024 * 1024 + 1)
                if len(chunk) > 8 * 1024 * 1024:
                    raise ValueError('unbounded real Present log')
                parts = (pending + chunk).split(b'\n')
                self.streams[key] = (offset + len(chunk), parts.pop())
                for line in parts:
                    if line.startswith(b'MEMORY_PRESENT_V1 '):
                        rows.extend(metrics.rows(line.decode('ascii'), 'MEMORY_PRESENT_V1'))
        self.streams = {key: value for key, value in self.streams.items() if key in live}
        return rows


def summarize(rows, model, begin, end):
    chosen = [r for r in rows if begin < int(r['cpu_monotonic_ns']) < end]
    if len(chosen) < 2 or any(r['model'] != model for r in chosen):
        raise ValueError('insufficient real Presents or actual model changed')
    times = [int(r['cpu_monotonic_ns']) for r in chosen]
    frames = [int(r['frame']) for r in chosen]
    if any(b <= a for a, b in zip(times, times[1:])) or len(set(frames)) != len(frames):
        raise ValueError('nonmonotonic or duplicate real Present ledger')
    if any(b != a + 1 for a, b in zip(frames, frames[1:])):
        raise ValueError('missing real Present telemetry in measurement window')
    if any(r['success'] != '1' for r in chosen):
        raise ValueError('failed Present in measurement window')
    intervals = [(b - a) / 1e6 for a, b in zip(times, times[1:])]
    return dict(frames=len(chosen), FPS=1000 * len(intervals) / sum(intervals),
                P50_ms=metrics.percentile(intervals, .5),
                P95_ms=metrics.percentile(intervals, .95),
                P99_ms=metrics.percentile(intervals, .99), max_ms=max(intervals),
                begin_ns=times[0], end_ns=times[-1])


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('session')
    p.add_argument('label')
    p.add_argument('--seconds', type=int, default=20, choices=range(5, 301))
    p.add_argument('--model', choices=('old', 'new'), required=True)
    a = p.parse_args()
    if not a.label.replace('-', '').isalnum() or len(a.label) > 60:
        p.error('short alphanumeric label required')
    run = runner.checked_run(a.session)
    game = runner.current_game(run)
    manifest = json.loads((run / 'manifest.json').read_text())
    runner.validate_daily_manifest(run, manifest)
    if manifest['environment'].get('BB_MEMORY_PROFILE') != '1':
        p.error('session must enable BB_MEMORY_PROFILE=1')
    state = json.loads((run / 'game-log-state.json').read_text())
    if state['failure_latched'] or state['error']:
        p.error('session already recorded a failure')
    route = run / 'memory-model-active.json'
    if route.exists():
        route_data = json.loads(route.read_text())
    else:
        # Older supervisors did not preserve this line across stdout rotations.
        # Accept only a still-retained actual Native report, never requested env.
        lines = [line for line in retained(run).splitlines()
                 if line.startswith('MEMORY_MODEL_ACTIVE_V1 ')]
        if len(lines) != 1:
            p.error('resolved memory route was not retained')
        route_data = dict(schema='bb-memory-model-active-v1', line=lines[0],
                          observed_monotonic_ns=time.monotonic_ns())
        with route.open('x') as out:
            json.dump(route_data, out)
    route_fields = metrics.rows(route_data['line'], 'MEMORY_MODEL_ACTIVE_V1')
    if len(route_fields) != 1 or route_fields[0]['model'] != a.model:
        p.error('actual driver route does not match requested comparison model')
    output = run / (a.label + '-memory-profile.json')
    if output.exists():
        p.error('output already exists')
    tail = PresentTail()
    tail.read(run)  # Prime old bytes before the measured window.
    begin = time.monotonic_ns()
    samples = {}
    memory_samples = {}
    collector_cpu_begin = time.process_time()
    # Poll within the bounded log retention window; retain only the small timing ledger.
    while time.monotonic_ns() < begin + a.seconds * 1_000_000_000:
        latest = run / 'gpu-memory-latest.json'
        if latest.is_file():
            sample = json.loads(latest.read_text())
            memory_samples[sample['monotonic_ns']] = sample
        for row in tail.read(run):
            if int(row['cpu_monotonic_ns']) > begin:
                samples[row['cpu_monotonic_ns']] = row
        time.sleep(.5)
    end = time.monotonic_ns()
    for row in tail.read(run):
        if int(row['cpu_monotonic_ns']) > begin:
            samples[row['cpu_monotonic_ns']] = row
    if runner.current_game(run) != game:
        raise ValueError('game process identity changed')
    state = json.loads((run / 'game-log-state.json').read_text())
    if state['failure_latched'] or state['error']:
        raise ValueError('session recorded a failure during measurement')
    rows = sorted(samples.values(), key=lambda r: int(r['cpu_monotonic_ns']))
    summary = summarize(rows, a.model, begin, end)
    result = dict(schema='bb-memory-profile-v1', session=a.session, label=a.label,
                  collector_cpu_ms=(time.process_time() - collector_cpu_begin) * 1000,
                  actual_route=route_data, game=game, model=a.model,
                  build=manifest['native_build_provenance'], summary=summary,
                  gpu_memory=metrics.memory(run, summary['begin_ns'], summary['end_ns'],
                                            samples=list(memory_samples.values())),
                  memory_samples=list(memory_samples.values()),
                  route_sha256=hashlib.sha256(route.read_bytes()).hexdigest(),
                  scope='real Present return CPU intervals; device usage includes driver/other apps; '
                        'scene equivalence requires separate observation; no GPU/scanout completion claim',
                  rows=rows)
    with output.open('x') as out:
        json.dump(result, out, indent=2)
    print(json.dumps(dict(path=str(output), summary=summary, gpu_memory=result['gpu_memory']), indent=2))


if __name__ == '__main__':
    main()
