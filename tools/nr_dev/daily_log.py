"""Bounded stdout capture for daily play; resource failures survive log rotation."""
import json
import hashlib
import os
from pathlib import Path
import re
import select
import threading
import time

FAILURE = re.compile(rb'\b(?:HOLD|QUARANTINE)\b|NATIVE_\S*(?:HOLD|QUARANTINE|ASSOCIATION_REJECTED)|functional=FAIL|VK_ERROR_DEVICE_LOST|STOP: GPU')


def read_completed(run):
    """Read both retained files in stream order; never clear a failure latch.

    The returned hashes identify the exact bytes parsed, not a complete historical
    stdout trace. Earlier rotations are covered only by the persistent failure latch.
    """
    run = Path(run)

    def regular_bytes(name):
        path = run / name
        if path.is_symlink() or not path.is_file():
            raise ValueError('daily log requires regular retained input: ' + name)
        return path.read_bytes()

    state_raw = regular_bytes('game-log-state.json')
    state = json.loads(state_raw)
    if (not isinstance(state, dict) or state.get('schema') != 'nr-daily-game-log-v1'
            or state.get('failure_latched') is not False or state.get('error') is not None
            or state.get('complete') is not True):
        raise ValueError('daily bounded log never lost a recorded failure')
    rotations, limit, seen = (state.get(k) for k in ('rotations', 'limit_bytes', 'bytes_seen'))
    if (type(rotations) is not int or rotations < 0 or type(limit) is not int or limit < 1024
            or type(seen) is not int or seen < 0
            or type(state.get('retained_files')) is not int or state['retained_files'] != 2):
        raise ValueError('daily bounded log capture metadata required')
    previous = run / 'game.log.1'
    if rotations == 0 and (previous.exists() or previous.is_symlink()):
        raise ValueError('unexpected previous daily log without a recorded rotation')
    names = ['game.log.1', 'game.log'] if rotations else ['game.log']
    data = {name: regular_bytes(name) for name in names}
    if any(len(raw) > limit for raw in data.values()) or sum(map(len, data.values())) > seen:
        raise ValueError('daily retained log exceeds recorded capture bounds')
    # Do not add separators or deduplicate proofs: the files are consecutive
    # stdout bytes, including any long line spanning a rotation boundary.
    raw = b''.join(data.values())
    if FAILURE.search(raw):
        raise ValueError('daily retained log contains a recorded resource failure')
    return raw.decode(errors='replace'), {
        'schema': 'nr-daily-game-log-audit-v1',
        'order': names,
        'file_sha256': {name: hashlib.sha256(part).hexdigest() for name, part in data.items()},
        'file_bytes': {name: len(part) for name, part in data.items()},
        'state_sha256': hashlib.sha256(state_raw).hexdigest(),
        'rotations': rotations,
        'bytes_seen': seen,
        'scope': 'retained stdout tail only; earlier failures remain guarded by the capture latch',
    }


class GameLog:
    def __init__(self, run, pipe, limit=8 * 1024 * 1024):
        if type(limit) is not int or limit < 1024:
            raise ValueError('bounded game log requires at least 1024 bytes')
        self.run, self.pipe, self.limit = Path(run), pipe, limit
        self.state = {'schema': 'nr-daily-game-log-v1', 'complete': False,
                      'failure_latched': False, 'error': None, 'bytes_seen': 0,
                      'rotations': 0, 'limit_bytes': limit, 'retained_files': 2}
        self.state['validation_summary'] = {'schema': 'nr-validation-summary-v1',
                                            'vuids': {}, 'sync_hazards': 0,
                                            'other_vuids': 0,
                                            'scope': 'all captured stdout; independent of ownership retirement'}
        self.deadline = None
        self.thread = threading.Thread(target=self.pump, name='daily-game-log', daemon=True)
        self.save()

    def save(self):
        temporary = self.run / 'game-log-state.tmp'
        temporary.write_text(json.dumps(self.state, indent=2) + '\n')
        temporary.replace(self.run / 'game-log-state.json')

    def start(self):
        self.thread.start()

    def finish(self):
        self.deadline = time.monotonic() + 2
        self.thread.join(3)
        if self.thread.is_alive():
            raise ValueError('daily stdout capture did not finish; retirement not proved')

    def pump(self):
        stream = None
        try:
            path = self.run / 'game.log'
            stream = path.open('wb')
            size, tail, pending = 0, b'', b''
            def emit(data):
                nonlocal stream, size
                summary = self.state['validation_summary']
                # Complete bounded lines, including messages later lost to rotation.
                # A diagnostic failure does not by itself invent unknown GPU ownership.
                if b'gpu error (Render.Vulkan):' in data:
                    ids = set(re.findall(rb'VUID-[A-Za-z0-9_-]+', data))
                    for raw in ids:
                        key = raw.decode('ascii')
                        if key in summary['vuids'] or len(summary['vuids']) < 128:
                            summary['vuids'][key] = summary['vuids'].get(key, 0) + 1
                        else:
                            summary['other_vuids'] += 1
                    summary['sync_hazards'] += int(b'SYNC-HAZARD' in data)
                    if ids or b'SYNC-HAZARD' in data:
                        self.save()  # diagnostic counts precede rotation of the failing line
                if data.startswith(b'MEMORY_MODEL_ACTIVE_V1 '):
                    # Preserve the resolved driver route across bounded stdout rotation.
                    route = self.run / 'memory-model-active.json'
                    if route.exists():
                        raise ValueError('duplicate active memory model report')
                    with route.open('x') as out:
                        json.dump({'schema': 'bb-memory-model-active-v1',
                                   'line': data.decode().strip(),
                                   'observed_monotonic_ns': time.monotonic_ns()}, out)
                if data.startswith(b'MEMORY_RESIDENCY_V1 '):
                    target = self.run / 'memory-residency-latest.json'
                    temporary = target.with_suffix('.tmp')
                    temporary.write_text(json.dumps({'schema': 'bb-memory-residency-v1',
                        'line': data.decode().strip(), 'observed_monotonic_ns': time.monotonic_ns()}) + '\n')
                    temporary.replace(target)
                if size + len(data) > self.limit:
                    stream.close()
                    path.replace(self.run / 'game.log.1')
                    stream = path.open('wb')
                    size = 0
                    self.state['rotations'] += 1
                stream.write(data)
                size += len(data)
            while True:
                if self.deadline is not None and time.monotonic() >= self.deadline:
                    raise ValueError('stdout still open after launcher exit')
                if not select.select([self.pipe], [], [], .1)[0]:
                    continue
                chunk = os.read(self.pipe.fileno(), 65536)
                if not chunk:
                    if pending:
                        emit(pending)
                    self.state['complete'] = True
                    break
                self.state['bytes_seen'] += len(chunk)
                if not self.state['failure_latched'] and FAILURE.search(tail + chunk):
                    self.state['failure_latched'] = True
                    self.save()  # Persistent latch precedes any rotation of the failing text.
                tail = (tail + chunk)[-1024:]
                pending += chunk
                while pending:
                    newline = pending.find(b'\n') + 1
                    length = newline if 0 < newline <= self.limit else self.limit
                    if not newline and len(pending) < self.limit:
                        break
                    emit(pending[:length])
                    pending = pending[length:]
                stream.flush()
        except Exception as error:
            self.state.update(complete=False, error=str(error)[:512])
            # Continue draining after storage failure so logging cannot deadlock the game.
            while self.deadline is None or time.monotonic() < self.deadline:
                if select.select([self.pipe], [], [], .1)[0] and not os.read(self.pipe.fileno(), 65536):
                    break
        finally:
            if stream is not None:
                stream.close()
            self.pipe.close()
            self.save()
