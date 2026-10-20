"""Real pipe/rotation tests, without a game, network or GPU."""
import importlib.util
import json
import os
from pathlib import Path
import tempfile
import threading
import unittest

HERE = Path(__file__).resolve().parents[1]
spec = importlib.util.spec_from_file_location('daily_log_test', HERE / 'daily_log.py')
log = importlib.util.module_from_spec(spec)
spec.loader.exec_module(log)


class DailyLog(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.addCleanup(self.tmp.cleanup)
        self.run = Path(self.tmp.name)

    def capture(self, payload, broken=False):
        read, write = os.pipe()
        capture = log.GameLog(self.run, os.fdopen(read, 'rb'), limit=4096)
        if broken:
            (self.run / 'game.log').mkdir()
        capture.start()
        def produce():
            with os.fdopen(write, 'wb') as stream:
                stream.write(payload)
        producer = threading.Thread(target=produce)
        producer.start(); producer.join(5)
        self.assertFalse(producer.is_alive(), 'capture must keep draining after storage errors')
        capture.finish()
        return json.loads((self.run / 'game-log-state.json').read_text())

    def test_rotation_stays_bounded_and_preserves_terminal_tail(self):
        payload = b'ordinary line\n' * 40000 + b'FINAL_NATIVE_TERMINAL\n'
        state = self.capture(payload)
        self.assertTrue(state['complete'])
        self.assertFalse(state['failure_latched'])
        self.assertIsNone(state['error'])
        self.assertEqual(state['bytes_seen'], len(payload))
        self.assertGreater(state['rotations'], 2)
        for path in self.run.glob('game.log*'):
            self.assertLessEqual(path.stat().st_size, 4096)
        self.assertTrue((self.run / 'game.log').read_bytes().endswith(b'FINAL_NATIVE_TERMINAL\n'))

    def test_failure_survives_rotation_beyond_both_retained_files(self):
        state = self.capture(b'x' * 65520 + b'\nNATIVE_RESOURCE_QUARANTINE error=1\n' + b'ok\n' * 40000)
        self.assertTrue(state['complete'])
        self.assertTrue(state['failure_latched'])
        self.assertNotIn(b'QUARANTINE', (self.run / 'game.log').read_bytes())
        self.assertNotIn(b'QUARANTINE', (self.run / 'game.log.1').read_bytes())

    def test_terminal_line_is_not_split_at_rotation_boundary(self):
        terminal = b'NATIVE_ASYNC_PRE_SR_EXIT_PROOF complete=1\n'
        self.capture(b'x\n' * 2044 + terminal)
        self.assertEqual((self.run / 'game.log').read_bytes(), terminal)

    def test_storage_failure_is_sticky_and_does_not_block_the_producer(self):
        state = self.capture(b'ordinary\n' * 40000, broken=True)
        self.assertFalse(state['complete'])
        self.assertIsNotNone(state['error'])

    def test_unclosed_inherited_pipe_never_becomes_completion(self):
        read, write = os.pipe()
        capture = log.GameLog(self.run, os.fdopen(read, 'rb'), limit=4096)
        capture.start()
        try:
            capture.finish()
        finally:
            os.close(write)
        state = json.loads((self.run / 'game-log-state.json').read_text())
        self.assertFalse(state['complete'])
        self.assertIn('stdout still open', state['error'])


if __name__ == '__main__':
    unittest.main()
