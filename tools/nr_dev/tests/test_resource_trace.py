"""Synthetic CPU records only; no GPU, assets, or historical log changes."""
import hashlib
import importlib.util
from pathlib import Path
import tempfile
import unittest

SPEC = importlib.util.spec_from_file_location(
    'resource_trace', Path(__file__).resolve().parents[1] / 'resource_trace.py')
m = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(m)


def event(seq, action='create', identity=1, handle=8, bda=0x1000, size=16,
          ns=None, kind='buffer', retire=0, observed=0, scheduler=0):
    return (f'NR_RESOURCE_TRACE monotonic_ns={seq * 10 if ns is None else ns} '
            f'seq={seq} tid=123 type={kind} event={action} id={identity} '
            f'handle=0x{handle:016x} bda=0x{bda:016x} bytes={size} '
            f'retire_tick={retire} observed_tick={observed} scheduler=0x{scheduler:016x} '
            'completion=NOT_INFERRED unknown_tick=0 scope=linked_image_cpu_hooks '
            'cap=4096 truncated=0\n')


class ResourceTraceTests(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.log = Path(self.tmp.name) / 'game.log'

    def tearDown(self):
        self.tmp.cleanup()

    def analyze(self, text, address=0x1000, ns=None):
        self.log.write_text(text)
        return m.analyze(self.log, address, ns)

    def test_half_open_uint64_boundaries(self):
        text = event(1, bda=m.MAX - 15, size=16)
        self.assertEqual(len(self.analyze(text, m.MAX)['buffer_matches']), 1)
        self.assertEqual(len(self.analyze(text, m.MAX - 16)['buffer_matches']), 0)
        self.assertEqual(len(self.analyze(event(1), 0x1010)['buffer_matches']), 0)
        self.assertTrue(self.analyze(event(1, bda=m.MAX, size=2))['coverage']['malformed_records'])
        for value in ('-1', '18446744073709551616', '0x10000000000000000', '__import__(os)', '1e3'):
            with self.assertRaises(ValueError):
                m.uint64(value)

    def test_handle_reuse_does_not_join_ids(self):
        text = event(1) + event(2, 'destroy_begin') + event(3, 'destroy_end')
        text += event(4, identity=2)
        matches = self.analyze(text)['buffer_matches']
        self.assertEqual([r['id'] for r in matches], [1, 2])
        self.assertEqual(matches[0]['observation'], 'destroy_end_observed')
        self.assertEqual(matches[1]['observation'], 'start_observed_no_destroy_record')

    def test_overlapping_ranges_all_reported(self):
        result = self.analyze(event(1) + event(2, identity=2, handle=9, bda=0xff8, size=32))
        self.assertEqual(len(result['buffer_matches']), 2)

    def test_truncation_interrupted_destroy(self):
        marker = event(4097, 'truncated', identity=0, handle=0, bda=0, size=0,
                       kind='trace').replace('truncated=0', 'truncated=1')
        result = self.analyze(event(1) + event(2, 'destroy_begin') + marker)
        self.assertTrue(result['coverage']['truncated'])
        records = result['buffer_matches'][0]['records']
        self.assertEqual([r['event'] for r in records], ['create', 'destroy_begin'])
        self.assertFalse(result['coverage']['complete'])

    def test_missing_and_partial_records(self):
        empty = self.analyze('ordinary game log\n')
        self.assertFalse(empty['coverage']['complete'])
        self.assertEqual(empty['buffer_matches'], [])
        self.assertIn('never establishes safety', empty['conclusion'])
        result = self.analyze(event(1, 'destroy_end'))
        self.assertEqual(len(result['buffer_destroy_without_create']), 1)
        result = self.analyze(event(1) + event(2, 'destroy_begin') + event(3, 'destroy_end')[:-5])
        self.assertEqual(result['buffer_matches'][0]['observation'], 'destroy_begin_without_end_observed')
        self.assertEqual(len(result['coverage']['malformed_records']), 1)

    def test_fault_time_filters_both_create_and_destroy(self):
        text = event(1) + event(2, 'destroy_begin') + event(3, 'destroy_end') + event(4, identity=2)
        result = self.analyze(text, ns=25)
        self.assertEqual(len(result['buffer_matches']), 1)
        self.assertEqual(result['buffer_matches'][0]['observation'], 'destroy_begin_without_end_observed')
        self.assertEqual(result['coverage']['matching_creates_after_fault_excluded'], 1)
        self.assertEqual(self.analyze(text, ns=5)['buffer_matches'], [])

    def test_unknown_time_explicit(self):
        result = self.analyze(event(1, ns=0), ns=5)
        self.assertEqual(result['coverage']['unknown_time_records'], 1)
        self.assertIn('unknown event time', result['buffer_matches'][0]['limitations'])

    def test_conflicting_id_and_mixed_trace_do_not_claim_join(self):
        result = self.analyze(event(1) + event(2, 'destroy_begin', handle=9)
                              + event(3, 'destroy_end', handle=9))
        self.assertEqual(result['buffer_matches'][0]['observation'], 'ambiguous_or_incomplete')
        result = self.analyze(event(1) + event(1, 'destroy_begin') + event(2, 'destroy_end'))
        self.assertTrue(result['coverage']['sequence_or_time_issues'])
        self.assertEqual(result['buffer_matches'][0]['observation'], 'ambiguous_or_incomplete')

    def test_malformed_and_malicious_lines_never_create_hits(self):
        valid = event(1)
        bad = [valid.replace('id=1 ', 'id=1 id=2 '),
               valid.replace('bytes=16 ', 'bytes=-16 '),
               valid.replace('type=buffer', 'type=__import__'),
               valid.replace('handle=0x0000000000000008', 'handle=0x8'),
               valid.replace('scope=linked_image_cpu_hooks', 'scope=all'),
               valid.replace('id=1 ', 'id=0 '), valid.rstrip('\n'),
               'prefix ' + valid, valid.replace('bytes=16 ', 'bytes=1e9 ')]
        for line in bad:
            with self.subTest(line=line):
                result = self.analyze(line)
                self.assertEqual(result['buffer_matches'], [])
                self.assertTrue(result['coverage']['malformed_records'])

    def sampler(self, observed=7, retire=7, scheduler=99):
        args = dict(kind='sampler_gc', bda=0, size=0, retire=retire, scheduler=scheduler)
        return (event(1, 'schedule', observed=1, **args)
                + event(2, 'destroy_begin', observed=observed, **args)
                + event(3, 'destroy_end', observed=observed, **args))

    def test_sampler_tick_observations_require_known_identity(self):
        for observed, retire, scheduler, expected in (
                (7, 7, 99, 'observed_at_or_after_retire'),
                (9, 7, 99, 'observed_at_or_after_retire'),
                (6, 7, 99, 'observed_below_retire'),
                (0, 0, 99, 'not_checkable'), (7, 7, 0, 'not_checkable')):
            result = self.analyze(self.sampler(observed, retire, scheduler))
            self.assertEqual(result['sampler_gc_observations'][0]['tick_observation'], expected)
        text = self.sampler().replace(
            'observed_tick=7 scheduler=0x0000000000000063',
            'observed_tick=7 scheduler=0x0000000000000064', 1)
        result = self.analyze(text)
        self.assertEqual(result['sampler_gc_observations'][0]['tick_observation'], 'not_checkable')

    def test_sha_and_output_exclusive_create(self):
        self.log.write_text(event(1))
        result = m.analyze(self.log, 0x1000)
        self.assertEqual(result['log']['sha256'], hashlib.sha256(self.log.read_bytes()).hexdigest())
        output = Path(self.tmp.name) / 'new.json'
        argv = ['--game-log', str(self.log), '--xid-address', '0x1000', '--output', str(output)]
        m.main(argv)
        original = output.read_bytes()
        with self.assertRaises(FileExistsError):
            m.main(argv)
        self.assertEqual(output.read_bytes(), original)
        link = Path(self.tmp.name) / 'link.json'
        link.symlink_to(output)
        with self.assertRaises(FileExistsError):
            m.main(argv[:-1] + [str(link)])


if __name__ == '__main__':
    unittest.main()
