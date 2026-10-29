#!/usr/bin/env python3
"""Offline checks for the acceptance prefix borrow/return semantics.

Runs the real PrefixLease against throw-away directories (no project prefix is
touched) and asserts the round plan always borrows from the session that owns
an initialized prefix, never from a previous borrower whose directory is empty
again after the return.
"""
import sys
import tempfile
import unittest
from pathlib import Path

WT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(WT / 'tools/nr_dev'))
sys.path.insert(0, str(WT / 'tools/sm_lab'))

import prefix_lease  # noqa: E402
import prefix_plan  # noqa: E402

PASS = {'status': 'PASS_NORMAL_TEARDOWN', 'native_lastuse_proof': True, 'residuals': []}


def make_prefix(path: Path, initialized: bool) -> Path:
    path.mkdir(parents=True)
    if initialized:
        (path / 'system.reg').write_text('reg')
    return path


class PlanTests(unittest.TestCase):
    def test_owner_is_first_round(self):
        rounds = prefix_plan.sessions('1800', ['sm', 'off', 'sm'])
        self.assertEqual([r[2] for r in rounds],
                         ['sm-acc-1800-sm1', 'sm-acc-1800-off2', 'sm-acc-1800-sm3'])
        self.assertIsNone(prefix_plan.reuse_for(rounds, 0))
        self.assertEqual(prefix_plan.reuse_for(rounds, 1), 'sm-acc-1800-sm1')
        self.assertEqual(prefix_plan.reuse_for(rounds, 2), 'sm-acc-1800-sm1')

    def test_off_first_still_owns(self):
        rounds = prefix_plan.sessions('9', ['off', 'sm'])
        self.assertEqual(prefix_plan.reuse_for(rounds, 1), 'sm-acc-9-off1')

    def test_single_round_never_reuses(self):
        rounds = prefix_plan.sessions('9', ['sm'])
        self.assertIsNone(prefix_plan.reuse_for(rounds, 0))

    def test_unknown_kind_refused(self):
        with self.assertRaises(SystemExit):
            prefix_plan.sessions('9', ['sm', 'fg'])


class BorrowReturnTests(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.home = Path(self.tmp.name) / 'home'
        self.runs = self.home / 'runs'
        self.owner = make_prefix(self.runs / 'sm-acc-1-sm1/profile/wine-prefix', True)
        self.borrower = make_prefix(self.runs / 'sm-acc-2-off2/profile/wine-prefix', False)
        self.record = self.runs / 'lease.json'

    def tearDown(self):
        self.tmp.cleanup()

    def test_borrow_moves_and_return_restores_owner(self):
        lease = prefix_lease.PrefixLease(self.owner, self.borrower, self.record)
        with lease:
            self.assertFalse((self.owner / 'system.reg').exists())
            self.assertTrue((self.borrower / 'system.reg').is_file())
            lease.complete(PASS)
        self.assertTrue((self.owner / 'system.reg').is_file())
        self.assertFalse((self.borrower / 'system.reg').exists())
        self.assertEqual(list(self.borrower.iterdir()), [])

    def test_planner_never_borrows_from_empty_borrower(self):
        lease = prefix_lease.PrefixLease(self.owner, self.borrower, self.record)
        with lease:
            lease.complete(PASS)
        rounds = prefix_plan.sessions('1', ['sm', 'off', 'sm'])
        third = prefix_plan.reuse_for(rounds, 2)
        self.assertEqual(third, 'sm-acc-1-sm1')
        self.assertNotEqual(third, 'sm-acc-1-off2')
        self.assertTrue(prefix_plan.owner_initialized(self.home, third))
        self.assertFalse(prefix_plan.owner_initialized(self.home, 'sm-acc-1-off2'))

    def test_game_never_started_returns_prefix(self):
        lease = prefix_lease.PrefixLease(self.owner, self.borrower, self.record)
        with lease:
            pass  # started stays False: nothing ran, so the prefix goes back
        self.assertTrue((self.owner / 'system.reg').is_file())
        self.assertFalse((self.borrower / 'system.reg').exists())

    def test_uninitialized_owner_refused(self):
        with self.assertRaises(ValueError):
            with prefix_lease.PrefixLease(self.borrower, self.owner, self.record):
                pass

    def test_failed_round_keeps_prefix(self):
        lease = prefix_lease.PrefixLease(self.owner, self.borrower, self.record)
        with self.assertRaises(ValueError):
            with lease:
                lease.started = True  # the runner sets this once the game started
                lease.complete({'status': 'HOLD', 'native_lastuse_proof': False, 'residuals': []})
        # A HOLD must not silently return the prefix to the owner.
        self.assertTrue((self.borrower / 'system.reg').is_file())
        self.assertFalse((self.owner / 'system.reg').exists())


if __name__ == '__main__':
    unittest.main()
