#!/usr/bin/env python3
"""Offline checks for the display-metric statistics.

Two things are covered:
- the per-second buckets must use the monotonic timestamp (not the frame index),
  count missing seconds as zero and keep the trailing partial second apart;
- a clean-looking, steady above-threshold stream is still only a *candidate*:
  it cannot be distinguished from steady compositor pollution, so the report
  must never claim that interpolation is proven.
"""
import sys
import unittest
from pathlib import Path
from tempfile import TemporaryDirectory

WT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(WT / 'tools/sm_lab'))

import frame_report  # noqa: E402


def report(rows, threshold=5):
    with TemporaryDirectory() as tmp:
        path = Path(tmp) / 'frames.txt'
        path.write_text('\n'.join(rows) + '\n')
        return frame_report.summarize(path, threshold)


def rows(diffs, step_ms=7, start_index=0, start_ns=1_000_000_000):
    out = []
    for offset, diff in enumerate(diffs):
        ts = start_ns + offset * step_ms * 1_000_000
        out.append(f'FRAME {start_index + offset} {ts} h{offset} {diff}')
    return out


class MetricTests(unittest.TestCase):
    def test_clean_stream_is_only_a_candidate(self):
        # 3.9 s at 10 Hz with every second containing the same number of updates.
        data = report(rows([60, 0] * 20, step_ms=100))
        self.assertEqual(data['noise_floor_permille'], 0)
        self.assertTrue(data['candidate_consistent'])
        self.assertFalse(data['interpolation_proven'])
        self.assertEqual(data['provenance'], 'unverified_capture')

    def test_steady_above_threshold_pollution_is_not_proof(self):
        # Every capture differs strongly and steadily: indistinguishable from a
        # stream of new frames, so the report must still refuse to claim.
        steady = [60] * 600
        data = report(rows(steady, step_ms=7))
        self.assertEqual(data['noise_floor_permille'], 0)
        self.assertTrue(data['per_second_stable'])
        self.assertTrue(data['candidate_consistent'])
        self.assertFalse(data['interpolation_proven'])
        self.assertIn('indistinguishable', data['limits'])

    def test_redraw_noise_blocks_the_candidate(self):
        data = report(rows([0, 2, 60, 2, 58, 2, 61, 2, 60]))
        self.assertEqual(data['noise_floor_permille'], 2)
        self.assertFalse(data['candidate_consistent'])
        self.assertFalse(data['interpolation_proven'])

    def test_unimodal_noise_blocks_the_candidate(self):
        data = report(rows([0, 3, 3, 4, 3, 4]))
        self.assertEqual(data['updates'], 0)
        self.assertFalse(data['candidate_consistent'])

    def test_unstable_burst_blocks_the_candidate(self):
        data = report(rows([0, 60, 60, 0, 0, 0, 0, 0, 0, 0], step_ms=500))
        self.assertFalse(data['per_second_stable'])
        self.assertFalse(data['candidate_consistent'])

    def test_buckets_use_timestamps_not_indices(self):
        # Indices start far away from the timestamps; buckets must follow time.
        data = report(rows([60, 0, 60, 0, 60], step_ms=700, start_index=5000))
        self.assertEqual(data['complete_seconds'], 2)
        self.assertEqual(data['updates_per_second'], {0: 1, 1: 1})
        self.assertEqual(data['tail_updates'], 1)
        self.assertEqual(data['updates'], 3)

    def test_missing_seconds_count_as_zero(self):
        # Updates only in the first second; the empty buckets must appear.
        data = report(rows([60, 60, 0, 0, 0, 0, 0, 0, 0], step_ms=500))
        self.assertEqual(data['complete_seconds'], 4)
        self.assertEqual(data['updates_per_second'], {0: 2, 1: 0, 2: 0, 3: 0})
        self.assertFalse(data['per_second_stable'])


if __name__ == '__main__':
    unittest.main()
