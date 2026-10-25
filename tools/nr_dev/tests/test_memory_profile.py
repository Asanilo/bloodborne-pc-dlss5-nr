import importlib.util
from pathlib import Path
import sys
import unittest
import tempfile

HERE = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(HERE))
spec = importlib.util.spec_from_file_location('tested_memory_profile', HERE / 'memory_profile.py')
m = importlib.util.module_from_spec(spec)
spec.loader.exec_module(m)


class MemoryProfile(unittest.TestCase):
    def rows(self):
        return [dict(model='new', frame=str(i), cpu_monotonic_ns=str(i * 10_000_000),
                     success='1') for i in range(1, 6)]

    def test_exclusive_bounds_and_elapsed_intervals(self):
        result = m.summarize(self.rows(), 'new', 10_000_000, 50_000_000)
        self.assertEqual(result['frames'], 3)
        self.assertEqual(result['FPS'], 100)
        self.assertEqual(result['P99_ms'], 10)

    def test_fallback_failed_present_and_duplicate_frame_rejected(self):
        for key, value in [('model', 'old'), ('success', '0'), ('frame', '1')]:
            rows = self.rows(); rows[1][key] = value
            with self.subTest(key=key), self.assertRaises(ValueError):
                m.summarize(rows, 'new', 0, 60_000_000)

    def test_missing_telemetry_cannot_be_scored_as_a_slow_frame(self):
        rows = self.rows(); rows.pop(2)
        with self.assertRaisesRegex(ValueError, 'missing real Present'):
            m.summarize(rows, 'new', 0, 60_000_000)

    def test_incremental_tail_handles_partial_lines_and_real_rotation(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory); path = root / 'game.log'; tail = m.PresentTail()
            line = lambda n: f'MEMORY_PRESENT_V1 model=new frame={n} success=1 cpu_monotonic_ns={n*10000000}\n'.encode()
            partial = line(2)
            path.write_bytes(b'ordinary output\n' + line(1) + partial[:20])
            self.assertEqual([r['frame'] for r in tail.read(root)], ['1'])
            with path.open('ab') as stream: stream.write(partial[20:])
            self.assertEqual([r['frame'] for r in tail.read(root)], ['2'])
            path.rename(root / 'game.log.1'); path.write_bytes(line(3))
            self.assertEqual([r['frame'] for r in tail.read(root)], ['3'])
            self.assertEqual(tail.read(root), [])
            path.write_bytes(b'')
            with self.assertRaisesRegex(ValueError, 'truncated'):
                tail.read(root)


if __name__ == '__main__':
    unittest.main()
