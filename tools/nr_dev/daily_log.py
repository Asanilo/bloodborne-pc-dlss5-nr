"""Bounded stdout capture for daily play; resource failures survive log rotation."""
import json
import os
from pathlib import Path
import re
import select
import threading
import time

FAILURE = re.compile(rb'\b(?:HOLD|QUARANTINE)\b|NATIVE_\S*(?:HOLD|QUARANTINE|ASSOCIATION_REJECTED)|functional=FAIL|VK_ERROR_DEVICE_LOST|STOP: GPU')


class GameLog:
    def __init__(self, run, pipe, limit=8 * 1024 * 1024):
        if type(limit) is not int or limit < 1024:
            raise ValueError('bounded game log requires at least 1024 bytes')
        self.run, self.pipe, self.limit = Path(run), pipe, limit
        self.state = {'schema': 'nr-daily-game-log-v1', 'complete': False,
                      'failure_latched': False, 'error': None, 'bytes_seen': 0,
                      'rotations': 0, 'limit_bytes': limit, 'retained_files': 2}
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
