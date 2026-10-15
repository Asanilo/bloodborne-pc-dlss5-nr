#!/usr/bin/env python3
"""Small CPU coordinate-chain experiment, NOT a production GPU replacement.

1280*720 pixels. Existing private images: (4*24 + 2*28 + 16 + 5*8)
B/pixel = 191,692,800 B = 182.8125 MiB (excludes buffers/driver alignment).
Adding four RG32 coordinate maps unchanged costs 29,491,200 B = 28.125 MiB,
exceeding 192 MiB. A theoretical complete role/format redesign: two anchors
at 16 B/px plus four maps at (16+2) B/px = 104 B/px vs existing bank96;
net +7,372,800 B, total199,065,600 B =189.84375 MiB before buffers/alignment.
This is a capacity lower bound, not an allocation recipe: finalizing old+target
needs a non-aliasing flatten destination and resource-role rotation. Immutable
anchors cannot grow without bound as each new model origin arrives.

Scalar 1D log-residuals isolate resampling, with float64 host coordinates.
Real RG32 coordinate precision, 2D visibility, finite GPU ownership, masks,
camera depth and throughput require separate validation. Coordinate fields are
also repeatedly sampled: nonlinear composition can accumulate geometric error.
"""
import math
import unittest


def sample(values, origins, x):
    """Bounded linear sample, no wrap/clamp or mixed model-origin support."""
    if not math.isfinite(x):
        return None
    lo = math.floor(x)
    t = x-lo
    taps = [(lo, 1-t), (lo+1, t)]
    participating = [(i,w) for i,w in taps if w > 0]
    if any(i < 0 or i >= len(values) or values[i] is None for i,_ in participating):
        return None
    identity = origins[participating[0][0]]
    if identity is None or any(origins[i] != identity for i,_ in participating):
        return None
    return sum(values[i]*w for i,w in participating), identity


def advance(values, origins, backward):
    output, identity = [], []
    for x in backward:
        result = sample(values, origins, x)
        output.append(None if result is None else result[0])
        identity.append(None if result is None else result[1])
    return output, identity


def anchor_display(anchor, anchor_origin, coordinates, route):
    output = []
    for x, origin in zip(coordinates, route):
        result = None if x is None or origin is None else sample(anchor, anchor_origin, x)
        output.append(None if result is None or result[1] != origin else result[0])
    return output


def smooth_warp(x, n):
    return x + .35*math.sin(math.pi*x/(n-1))


class AnchorExperiment(unittest.TestCase):
    def test_translation_roundtrip_avoids_residual_diffusion(self):
        n = 81
        source = [math.sin(2*math.pi*i/8) for i in range(n)]
        ids = [17]*n
        repeated, repeated_ids = source[:], ids[:]
        coordinates, route = list(map(float, range(n))), ids[:]
        for _ in range(8):
            for shift in (.25, -.25):
                backward = [i+shift for i in range(n)]
                repeated,repeated_ids = advance(repeated,repeated_ids,backward)
                coordinates,route = advance(coordinates,route,backward)
        display = anchor_display(source,ids,coordinates,route)
        interior = range(20,n-20)
        old_error = sum((repeated[i]-source[i])**2 for i in interior)/len(interior)
        anchor_error = sum((display[i]-source[i])**2 for i in interior)/len(interior)
        coordinate_error = max(abs(coordinates[i]-i) for i in interior)
        self.assertLess(coordinate_error,1e-12)
        self.assertLess(anchor_error,1e-24)
        self.assertGreater(old_error,.1)
        print(f'translation: repeated MSE={old_error:.6g}, anchor MSE={anchor_error:.6g}, coordinate max error={coordinate_error:.6g}')

    def test_nonlinear_coordinate_chain_still_accumulates_error(self):
        n = 81
        source = [math.sin(2*math.pi*i/8) for i in range(n)]
        ids = [17]*n
        backward = [smooth_warp(i,n) for i in range(n)]
        coordinates,route = list(map(float,range(n))),ids[:]
        repeated,repeated_ids = source[:],ids[:]
        for _ in range(16):
            coordinates,route = advance(coordinates,route,backward)
            repeated,repeated_ids = advance(repeated,repeated_ids,backward)
        # Analytic composition is ground truth; accumulated sampled coordinates
        # are not exact for a spatially varying warp.
        exact = []
        for i in range(n):
            x = float(i)
            for _ in range(16):x=smooth_warp(x,n)
            exact.append(x)
        ideal=anchor_display(source,ids,exact,ids)
        display=anchor_display(source,ids,coordinates,route)
        interior=range(20,n-20)
        coord_error=max(abs(coordinates[i]-exact[i]) for i in interior)
        anchor_error=sum((display[i]-ideal[i])**2 for i in interior)/len(interior)
        old_error=sum((repeated[i]-ideal[i])**2 for i in interior)/len(interior)
        self.assertGreater(coord_error,1e-5)
        self.assertGreater(anchor_error,0)
        self.assertLess(anchor_error,old_error)
        print(f'nonlinear: coordinate max error={coord_error:.6g}, anchor MSE={anchor_error:.6g}, repeated MSE={old_error:.6g}')

    def test_disocclusion_is_not_filled_or_resurrected(self):
        ids=[1]*8
        coordinates,route=advance(list(map(float,range(8))),ids,[i+.5 for i in range(8)])
        self.assertIsNone(coordinates[-1])
        display=anchor_display([1.]*8,ids,coordinates,route)
        self.assertIsNone(display[-1])
        # Returning motion cannot restore a mask already lost to disocclusion.
        coordinates,route=advance(coordinates,route,[i-.5 for i in range(8)])
        self.assertIsNone(coordinates[0])
        self.assertIsNone(coordinates[-1])

    def test_origin_identity_and_flattening_ages(self):
        anchor=[.1]*4+[.3]*4
        origins=[101]*4+[202]*4
        self.assertIsNone(sample(anchor,origins,3.5))
        self.assertEqual(sample(anchor,origins,3.),(.1,101))
        # A merged display may contain both model origins. Rebasing coordinates
        # to a new current-space anchor does NOT re-label either model's age.
        ages_frames=[8]*4+[3]*4
        ages_ms=[130.]*4+[48.]*4
        frozen_age=list(zip(ages_frames,ages_ms))
        advanced_age=[(f+1,ms+16.) for f,ms in frozen_age]
        self.assertEqual(advanced_age[0],(9,146.))
        self.assertEqual(advanced_age[-1],(4,64.))
        self.assertNotEqual(advanced_age[0],advanced_age[-1])
        # Two retained anchors plus an incoming third origin need flattening or
        # extra storage/routing; an identity coordinate map cannot remove this.
        self.assertEqual(len(set(origins+[303])),3)
        self.assertIsNone(sample([0.,1.],[101,202],.5))


if __name__ == '__main__':
    unittest.main()
