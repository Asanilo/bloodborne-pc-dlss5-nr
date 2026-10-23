import unittest
from analyze import analyze


def row(kind, source, fg, ns, success=1):
    return f'FG_PRESENT kind={kind} source_frame={source} fg_frame={fg} success={success} cpu_monotonic_ns={ns}\n'


class PresentAuditTests(unittest.TestCase):
    def test_complete_present_after_unterminated_diagnostic(self):
        text = (row('generated', 7, 3, 1) +
                'Write fault sites (guest code, this window): 4377 faults' +
                row('real', 7, 3, 2) + row('generated', 8, 4, 3) + row('real', 8, 4, 4))
        result = analyze(text)
        self.assertEqual(result['matched_generated_then_real_pairs'], 2)
        self.assertEqual(result['order_errors'], [])

    def test_pairs_and_real_only_warmup(self):
        result = analyze(row('real', 1, 0, 0) + row('generated', 2, 2, 8_000_000) +
                         row('real', 2, 2, 16_000_000))
        self.assertEqual(result['matched_generated_then_real_pairs'], 1)
        self.assertEqual(result['successful_presents'], {'real': 2, 'generated': 1})
        self.assertEqual(result['interval_ms']['all']['median'], 8)
        self.assertEqual(result['order_errors'], [])

    def test_failed_present_does_not_count_as_generated(self):
        result = analyze(row('generated', 2, 2, 1, 0) + row('real', 2, 2, 2))
        self.assertEqual(result['failed_presents'], 1)
        self.assertEqual(result['successful_presents'], {'real': 1})
        self.assertEqual(result['generated_per_real'], 0)

    def test_native_real_source_must_match_inside_present_pair(self):
        text = (row('generated', 7, 3, 1).rstrip() + ' source_game_frame=21\n' +
                row('real', 7, 3, 2).rstrip() + ' source_game_frame=22\n')
        self.assertEqual(analyze(text)['order_errors'], ['generated/real identity mismatch'])

    def test_mismatched_sources_and_duplicate_generated_are_rejected(self):
        result = analyze(row('generated', 1, 1, 1) + row('generated', 2, 2, 2) + row('real', 3, 3, 3))
        self.assertEqual(len(result['order_errors']), 2)
        self.assertEqual(result['matched_generated_then_real_pairs'], 0)


if __name__ == '__main__':
    unittest.main()
