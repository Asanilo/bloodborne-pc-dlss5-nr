#!/usr/bin/env python3
"""Offline checks for the display-metric honesty gate.

A threshold alone cannot prove the capture is free of compositor redraws. The
report must refuse to claim when sub-threshold differences exist or when the
per-second update rate is unstable.
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


def rows(diffs, step_ms=7):
    out = []
    for index, diff in enumerate(diffs):
        ts = 1_000_000_000 + index * step_ms * 1_000_000
        out.append(f'FRAME {index} {ts} h{index} {diff}')
    return out


class MetricTests(unittest.TestCase):
    def test_clean_capture_is_claimable(self):
        data = report(rows([0, 0, 60, 0, 58, 0, 61]))
        self.assertEqual(data['noise_floor_permille'], 0)
        self.assertTrue(data['claimable'])
        self.assertGreaterEqual(data['updates'], 3)

    def test_redraw_noise_blocks_the_claim(self):
        data = report(rows([0, 2, 60, 2, 58, 2, 61]))
        self.assertEqual(data['noise_floor_permille'], 2)
        self.assertFalse(data['claimable'])
        self.assertIn('NOT claimable', data['claim_note'])

    def test_unimodal_noise_blocks_the_claim(self):
        data = report(rows([0, 3, 3, 4, 3, 4]))
        self.assertEqual(data['updates'], 0)
        self.assertFalse(data['claimable'])

    def test_unstable_rate_blocks_the_claim(self):
        # Updates only in the first second: the rate is not a steady stream.
        data = report(rows([0, 60, 60, 0, 0, 0, 0, 0, 0, 0], step_ms=500))
        self.assertFalse(data['per_second_stable'])
        self.assertFalse(data['claimable'])


if __name__ == '__main__':
    unittest.main()
