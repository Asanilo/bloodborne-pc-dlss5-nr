#!/usr/bin/env python3
"""Regression for the synthetic precision probe, never hardware acceptance."""
from dataclasses import replace
import math
import unittest

from temporal_step import ShaderMath, History, capture, coordinates, f32, half, publish, report, sample


class TemporalStep(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.shader = ShaderMath()

    @classmethod
    def tearDownClass(cls):
        cls.shader.close()

    @staticmethod
    def grid(fn=lambda x, y: .25, size=8):
        return [[History((fn(x, y),) * 3, frames=3., ms=48.) for x in range(size)] for y in range(size)]

    def test_actual_shader_scalar_functions_and_depth_edges(self):
        for phase in (0., .125, .25, .5, .875):
            weights = self.shader.weights(phase)
            self.assertEqual(sum(weights), 1.)
            # Catmull-Rom reproduces a linear ramp before bounded half storage.
            self.assertEqual(sum(w * (i-1) for i, w in enumerate(weights)), phase)
        self.assertLess(self.shader.weights(.25)[0], 0.)
        for invalid in (-.001, 1.001, math.nan, math.inf):
            self.assertFalse(self.shader.surface(invalid, invalid))
        self.assertTrue(self.shader.surface(1., 1.))
        self.assertFalse(self.shader.surface(1., .999999))
        near, far = f32(1.001 - .1/50), f32(1.001 - .1/100)
        self.assertFalse(self.shader.surface(near, far))
        self.assertTrue(self.shader.surface(near, near))

    def test_motion_half_and_uv_phase_are_distinct_stages(self):
        route = coordinates((3, 3), (8, 8), (.2501, -.2501))
        self.assertEqual(route['motion_half'], (.25, -.25))
        self.assertEqual(route['texel'], (3.25, 2.75))
        self.assertEqual(route['nearest'], (3, 3))
        self.assertEqual(coordinates((0, 0), (8, 8), (-.501, 0))['reason'], 'motion_bounds')
        for value in (math.nan, math.inf, -math.inf):
            self.assertEqual(coordinates((3, 3), (8, 8), (value, 0))['reason'], 'motion_bounds')
        # Jitter is UV, and enters after the motion/extent operation.
        self.assertEqual(coordinates((3, 3), (8, 8), jitter=(.125, 0))['nearest'], (4, 3))

    def test_constant_and_linear_ramp_analytic_baselines(self):
        for rounding in ('rne', 'rtz'):
            for phase in (-.25, .25, .5):
                route = coordinates((3, 3), (8, 8), (phase, .125), rounding=rounding)
                h, reason = sample(self.grid(), route, self.shader, mode='cubic', rounding=rounding)
                self.assertEqual(reason, 'cubic')
                self.assertEqual(h.edit, (.25,) * 3)
                self.assertEqual((h.frames, h.ms), (4., 64.))
                ramp = self.grid(lambda x, y: x/16.)
                h, reason = sample(ramp, route, self.shader, mode='cubic', rounding=rounding)
                self.assertEqual(h.edit[0], (3+phase)/16.)
                nearest, _ = sample(ramp, route, self.shader)
                self.assertEqual(nearest.edit[0], route['nearest'][0]/16.)

    def test_plus_minus_quarter_roundtrip_is_not_lossless_for_detail(self):
        def warp(grid, shift, mode):
            size = len(grid)
            return [[sample(grid, coordinates((x, y), (size, size), (shift, 0)),
                            self.shader, mode=mode, dt=(0, 0))[0]
                     for x in range(size)] for y in range(size)]
        ramp = self.grid(lambda x, y: x/16.)
        result = warp(warp(ramp, .25, 'cubic'), -.25, 'cubic')
        self.assertEqual(result[3][3].edit, ramp[3][3].edit)
        impulse = self.grid(lambda x, y: float(x == 3))
        cubic = warp(warp(impulse, .25, 'cubic'), -.25, 'cubic')
        nearest = warp(warp(impulse, .25, 'nearest'), -.25, 'nearest')
        self.assertEqual(nearest[3][3].edit, impulse[3][3].edit)
        self.assertGreater(cubic[3][3].edit[0], 0.)
        self.assertLess(cubic[3][3].edit[0], 1.)

    def test_depth_and_origin_rejection_preserve_nearest(self):
        route = coordinates((3, 3), (8, 8), (.25, .125))
        for change, expected in (({'depth': .8}, 'fallback_depth'),
                                 ({'ms': 47.}, 'fallback_origin'),
                                 ({'confidence': 0.}, 'fallback_source_dead')):
            grid = self.grid(lambda x, y: x/16.)
            # This is an outer negative-lobe contributor, not the nearest tap.
            grid[2][2] = replace(grid[2][2], **change)
            result, reason = sample(grid, route, self.shader, mode='cubic')
            self.assertEqual(reason, expected)
            self.assertEqual(result.edit, grid[3][3].edit)
            self.assertEqual((result.frames, result.ms), (4., 64.))
        result, reason = sample(self.grid(), route, self.shader, expected_depth=.8)
        self.assertIsNone(result)
        self.assertEqual(reason, 'depth')

    def test_zero_weight_taps_and_out_of_bounds(self):
        grid = [[History((.25,) * 3)]]
        result, reason = sample(grid, coordinates((0, 0), (1, 1)), self.shader, mode='cubic')
        self.assertEqual(reason, 'cubic')
        self.assertEqual(result.edit, (.25,) * 3)
        result, reason = sample(grid, coordinates((0, 0), (1, 1), (.25, 0)), self.shader, mode='cubic')
        self.assertEqual(reason, 'fallback_bounds')
        self.assertEqual(result.edit, (.25,) * 3)

    def test_ttl_exact_boundary_and_nonfinite_or_negative_age(self):
        route = coordinates((0, 0), (1, 1))
        grid = [[History((.25,) * 3, frames=11, ms=184)]]
        result, reason = sample(grid, route, self.shader)
        self.assertEqual(reason, 'nearest')
        self.assertEqual((result.frames, result.ms), (12., 200.))
        for dt in ((1.001, 16), (1, 16.001), (math.nan, 16), (1, math.inf)):
            self.assertEqual(sample(grid, route, self.shader, dt=dt), (None, 'age'))
        for age in (-1, math.nan, math.inf):
            invalid = [[replace(grid[0][0], frames=age)]]
            self.assertEqual(sample(invalid, route, self.shader), (None, 'source_dead'))
            self.assertIsNone(capture((1, 1, 1), (2, 2, 2), frames=age))
            self.assertIsNone(capture((1, 1, 1), (2, 2, 2), ms=age))

    def test_half_modes_and_confidence_fallback(self):
        for value in (.80001, -.80001, .91201, -.91201):
            self.assertLessEqual(abs(half(value, 'rtz')), abs(value))
            self.assertLessEqual(abs(half(value, 'rtz')), abs(half(value, 'rne')))
        self.assertEqual(half(.5, 'rne'), half(.5, 'rtz'))
        for rounding in ('rne', 'rtz'):
            self.assertTrue(math.isnan(half(math.nan, rounding)))
            self.assertEqual(half(math.inf, rounding), math.inf)
            self.assertEqual(half(-math.inf, rounding), -math.inf)
        for sign in (-1., 1.):
            for value in (65520., 1e30):
                self.assertEqual(half(sign*value, 'rtz'), sign*65504.)
                self.assertEqual(half(sign*value, 'rne'), sign*math.inf)
        grid = [[History((.25,) * 3, confidence=.95) for _ in range(8)] for _ in range(8)]
        grid[2][2] = replace(grid[2][2], confidence=.80001)
        route = coordinates((3, 3), (8, 8), (.25, .125))
        result, reason = sample(grid, route, self.shader, mode='cubic', min_confidence=.8)
        self.assertEqual(reason, 'fallback_confidence')
        self.assertEqual(result.confidence, .95)

    def test_capture_compose_publish_separate_quantization(self):
        raw = tuple(half(v) for v in (.2, .3, .4, .75))
        target = (.31, .42, .53)
        for rounding in ('rne', 'rtz'):
            h = capture(raw[:3], target, rounding)
            self.assertEqual(tuple(half(v, rounding) for v in h.edit), h.edit)
            final, intermediate, failed = publish([raw], [h], rounding)
            self.assertFalse(failed)
            self.assertEqual(final[0][3], raw[3])
            self.assertLess(max(abs(a-b) for a,b in zip(final[0], target)), .001)
            self.assertNotEqual(final[0][:3], intermediate[0][:3])

    def test_return_guard_is_whole_image_and_raw_alpha_survives(self):
        raw = [(1., 2., 3., .25), (65504., 1., 1., .75)]
        final, _, failed = publish(raw, [History((.25,) * 3), History((4.,) * 3)])
        self.assertTrue(failed)
        self.assertEqual(final, raw)
        final, _, failed = publish(raw[:1], [None])
        self.assertFalse(failed)
        self.assertEqual(final, raw[:1])

    def test_report_keeps_stage_errors_and_rejection_reasons(self):
        result = report(self.shader, 'rne')
        self.assertGreater(result['capture_max_log_error'], 0.)
        self.assertGreater(result['raw_input_half_max_error'], 0.)
        self.assertGreater(result['publish_max_rgb_error'], 0.)
        self.assertGreater(result['publish_incremental_half_error'], 0.)
        self.assertEqual(result['sampling']['cubic']['linear_ramp_error'], 0.)
        self.assertEqual(result['rejection_reasons'], {'ttl': 'age', 'depth': 'depth', 'mixed_origin': 'fallback_origin'})


if __name__ == '__main__':
    unittest.main()
