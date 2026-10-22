import unittest
from wine_probe_result import evaluate_status


class EvaluateResultTests(unittest.TestCase):
    def row(self, result='00000001', disable='00000000', eligible='1'):
        return f'FG_EVAL_GPU_COMPLETE frame=1 result=0x{result} disable_word=0x{disable} present_eligible={eligible} native_presents=0'

    def test_create_is_not_evaluate(self):
        self.assertEqual(evaluate_status('FG_CORE_CREATE result=0x00000001')['status'], 'NOT_EVALUATED')

    def test_failed_call_cannot_be_eligible(self):
        self.assertEqual(evaluate_status(self.row(result='bad00005'))['eligible_outputs'], 0)

    def test_unchanged_sentinel_cannot_be_eligible(self):
        self.assertEqual(evaluate_status(self.row(disable='ffffffff'))['eligible_outputs'], 0)

    def test_disabled_output_cannot_be_eligible(self):
        self.assertEqual(evaluate_status(self.row(disable='00000001'))['eligible_outputs'], 0)

    def test_only_first_byte_is_boolean(self):
        result = evaluate_status(self.row(disable='ffffff00'))
        self.assertEqual(result['eligible_outputs'], 1)
        self.assertEqual(result['status'], 'OUTPUT_REQUIRES_PIXEL_VALIDATION')
        self.assertEqual(result['native_presents'], 0)

    def test_recording_success_is_not_gpu_completion(self):
        self.assertEqual(evaluate_status('FG_EVALUATE frame=1 result=0x00000001')['eligible_outputs'], 0)


if __name__ == '__main__':
    unittest.main()
