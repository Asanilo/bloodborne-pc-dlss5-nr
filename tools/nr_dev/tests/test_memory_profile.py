import importlib.util
from pathlib import Path
import sys
import unittest

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


if __name__ == '__main__':
    unittest.main()
