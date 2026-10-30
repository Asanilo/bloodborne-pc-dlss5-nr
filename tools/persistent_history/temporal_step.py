#!/usr/bin/env python3
"""CPU single-step precision probe; NOT a GPU replay or NVIDIA reference.

The scalar surface gate and cubic weights are compiled from history.comp with
only GLSL float-literal suffixes and a vec4 constructor adapted to C++. Transport
and publication are a deliberately small independent oracle: explicit binary32
rounding, host libm log2/exp2, selectable binary16 RNE/RTZ. GPU FMA contraction,
transcendental accuracy, denormal handling and GLSL round ties may differ.

Inputs use an identity camera and supplied current-surface evidence. This does
not replay camera/object motion generation, candidate admission, IPC, resource
ownership, or every shader branch. It isolates where one step loses precision.
Only Python's standard library and a C++20 compiler are needed.
"""
import argparse
import ctypes
from dataclasses import dataclass, replace
import hashlib
import json
import math
from pathlib import Path
import re
import struct
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[2]
SHADER = ROOT / 'tools/persistent_history/history.comp'
EPS = 1 / 512


def f32(value):
    try:
        return struct.unpack('<f', struct.pack('<f', value))[0]
    except OverflowError:
        return math.copysign(math.inf, value)


def half(value, rounding='rne'):
    """Host binary16 RNE, or its adjacent toward-zero representable value."""
    if rounding not in ('rne', 'rtz'):
        raise ValueError('half rounding must be rne or rtz')
    try:
        bits = struct.unpack('<H', struct.pack('<e', value))[0]
    except OverflowError:
        return math.copysign(math.inf if rounding == 'rne' else 65504., value)
    stored = struct.unpack('<e', struct.pack('<H', bits))[0]
    if rounding == 'rtz' and abs(stored) > abs(value):
        stored = struct.unpack('<e', struct.pack('<H', bits - 1))[0]
    return stored


def extract_function(source, name):
    match = re.search(r'\b(?:bool|float|vec4)\s+' + re.escape(name) + r'\s*\(', source)
    if not match:
        raise ValueError('shader function missing: ' + name)
    brace = source.index('{', match.start())
    depth = 1
    end = brace + 1
    while depth:
        depth += (source[end] == '{') - (source[end] == '}')
        end += 1
    return source[match.start():end]


class ShaderMath:
    """Execute actual shader pure functions on the CPU; no Vulkan device."""
    names = ('finite1', 'depthValid', 'sameSurface', 'depthResidual', 'cubicWeights')

    def __init__(self):
        source = SHADER.read_text()
        extracted = '\n'.join(extract_function(source, name) for name in self.names)
        # GLSL unsuffixed floating literals are float, C++ literals are double.
        adapted = re.sub(r'(?<![\w.])(?:\d+\.\d*|\.\d+|\d+[eE][+-]?\d+)(?:[eE][+-]?\d+)?',
                         lambda m: m[0] + 'f', extracted)
        adapter = '''#include <algorithm>
#include <cmath>
using std::abs; using std::clamp; using std::max; using std::isnan; using std::isinf;
struct vec4 {float x,y,z,w; vec4(float a,float b,float c,float d):x(a),y(b),z(c),w(d){}};
struct Params {vec4 thresholds{0,0,0,0};} p;
''' + adapted + '''
extern "C" int surface(float a,float b,float z,float tolerance) {
 p.thresholds.y=tolerance; return sameSurface(a,b,z);
}
extern "C" float residual(float a,float b,float z,float tolerance) {
 p.thresholds.y=tolerance; return depthResidual(a,b,z);
}
extern "C" void weights(float t,float* out) {
 auto v=cubicWeights(t);out[0]=v.x;out[1]=v.y;out[2]=v.z;out[3]=v.w;
}
'''
        self.directory = tempfile.TemporaryDirectory(prefix='nr-temporal-step-')
        path = Path(self.directory.name)
        (path / 'shader_math.cpp').write_text(adapter)
        subprocess.run(['c++', '-std=c++20', '-O0', '-ffp-contract=off', '-fno-fast-math',
                        '-shared', '-fPIC', str(path / 'shader_math.cpp'), '-o', str(path / 'math.so')],
                       check=True, capture_output=True, text=True)
        self.lib = ctypes.CDLL(str(path / 'math.so'))
        self.lib.surface.argtypes = [ctypes.c_float] * 4
        self.lib.surface.restype = ctypes.c_int
        self.lib.residual.argtypes = [ctypes.c_float] * 4
        self.lib.residual.restype = ctypes.c_float
        self.lib.weights.argtypes = [ctypes.c_float, ctypes.POINTER(ctypes.c_float)]
        self.lib.weights.restype = None
        self.source_sha256 = hashlib.sha256(source.encode()).hexdigest()

    def close(self):
        self.directory.cleanup()

    def surface(self, a, b, projection_z=1.001, tolerance=.02):
        return bool(self.lib.surface(a, b, projection_z, tolerance))

    def weights(self, phase):
        out = (ctypes.c_float * 4)()
        self.lib.weights(phase, out)
        return tuple(out)


def coordinates(pixel, extent, motion=(0., 0.), jitter=(0., 0.), rounding='rne'):
    """Normalized UV order, including input RG16F and binary32 stages.

    Motion is already expressed in this execution extent's pixel units; the
    renderer-to-execution motion adapter and motion generation are not replayed.
    Jitter is previous-minus-current jitter in normalized UV units.
    """
    quantized = tuple(half(f32(v), rounding) for v in motion)
    previous = tuple(f32(f32(f32(f32(c + .5) / size) + f32(v / size)) + f32(j))
                     for c, size, v, j in zip(pixel, extent, quantized, jitter))
    if not all(math.isfinite(v) and 0 <= v < 1 for v in previous):
        return {'reason': 'motion_bounds', 'motion_half': quantized}
    texel = tuple(f32(f32(v * size) - .5) for v, size in zip(previous, extent))
    nearest = tuple(int(f32(v * size)) for v, size in zip(previous, extent))
    return {'reason': None, 'motion_half': quantized, 'texel': texel, 'nearest': nearest}


@dataclass(frozen=True)
class History:
    edit: tuple
    depth: float = .5
    frames: float = 0.
    ms: float = 0.
    confidence: float = 1.


def live(h, max_frames=12., max_ms=200., min_confidence=.5):
    return (h is not None and all(math.isfinite(v) for v in (*h.edit, h.depth, h.frames, h.ms, h.confidence))
            and h.confidence > 0 and h.confidence >= min_confidence
            and 0 <= h.frames <= max_frames and 0 <= h.ms <= max_ms)


def capture(raw, edited, rounding='rne', frames=0., ms=0.):
    """Numerical residual storage stage, RGB only; not full candidate admission."""
    raw, edited = tuple(map(f32, raw)), tuple(map(f32, edited))
    if not all(math.isfinite(v) and v >= 0 for v in (*raw, *edited, frames, ms)):
        return None
    if frames > 12 or ms > 200:
        return None
    edit = tuple(f32(math.log2(f32(f32(b + EPS) / f32(a + EPS)))) for a, b in zip(raw, edited))
    if any(not math.isfinite(v) or abs(v) > 4 for v in edit):
        return None
    return History(tuple(half(v, rounding) for v in edit), frames=f32(frames), ms=f32(ms))


def sample(grid, route, math_source, expected_depth=.5, mode='nearest', dt=(1., 16.),
           rounding='rne', max_frames=12., max_ms=200., min_confidence=.5):
    """Isolated residual/age/depth sampling, given a route and existing evidence.

    The full shader's raw-guide/colour, camera, cardinal-edge and new confidence
    evidence checks are NOT inferred from these synthetic inputs.
    """
    if mode not in ('nearest', 'cubic'):
        raise ValueError('unknown reconstruction')
    if route['reason']:
        return None, route['reason']
    qx, qy = route['nearest']
    if not (0 <= qy < len(grid) and 0 <= qx < len(grid[0])):
        return None, 'motion_bounds'
    reference = grid[qy][qx]
    def accepted(h):
        return live(h, max_frames, max_ms, min_confidence)
    if not accepted(reference):
        return None, 'source_dead'
    age = (f32(reference.frames + dt[0]), f32(reference.ms + dt[1]))
    result = replace(reference, frames=age[0], ms=age[1])
    if not accepted(result):
        return None, 'age'
    if not math_source.surface(reference.depth, expected_depth):
        return None, 'depth'
    if mode == 'nearest':
        return result, 'nearest'
    x, y = route['texel']
    bx, by = math.floor(x), math.floor(y)
    wx, wy = math_source.weights(f32(x - bx)), math_source.weights(f32(y - by))
    rgb = [0., 0., 0.]
    low, high = list(reference.edit), list(reference.edit)
    confidence = 1.
    for dy in range(4):
        for dx in range(4):
            weight = f32(wx[dx] * wy[dy])
            if weight == 0:
                continue
            tx, ty = bx + dx - 1, by + dy - 1
            if not (0 <= ty < len(grid) and 0 <= tx < len(grid[0])):
                return result, 'fallback_bounds'
            tap = grid[ty][tx]
            if not accepted(tap):
                return result, 'fallback_source_dead'
            if (tap.frames, tap.ms) != (reference.frames, reference.ms):
                return result, 'fallback_origin'
            if not math_source.surface(tap.depth, expected_depth):
                return result, 'fallback_depth'
            rgb = [f32(a + f32(weight * b)) for a, b in zip(rgb, tap.edit)]
            confidence = min(confidence, tap.confidence)
            if 1 <= dx <= 2 and 1 <= dy <= 2:
                low = list(map(min, low, tap.edit))
                high = list(map(max, high, tap.edit))
    if half(confidence, 'rtz') < min_confidence:
        return result, 'fallback_confidence'
    filtered = tuple(half(min(max(v, lo), hi), rounding) for v, lo, hi in zip(rgb, low, high))
    return replace(result, edit=filtered, confidence=half(confidence, rounding)), 'cubic'


def publish(raws, histories, rounding='rne'):
    """Compose FP32 then the unmasked Return shader's whole-image half guard."""
    composed = []
    for raw, history in zip(raws, histories):
        rgb = raw[:3] if history is None else tuple(
            f32(max(f32(f32(f32(max(v, 0) + EPS) * f32(2 ** e)) - EPS), 0))
            for v, e in zip(raw, history.edit))
        composed.append((*rgb, raw[3]))
    failed = any(any(not math.isfinite(v) or abs(v) > 65504 for v in image[:3])
                 or not math.isfinite(raw[3]) for raw, image in zip(raws, composed))
    selected = raws if failed else composed
    return [tuple(half(v, rounding) for v in pixel) for pixel in selected], composed, failed


def report(math_source, rounding):
    input_raw = (.2, .3, .4, .75)
    raw = tuple(half(v, rounding) for v in input_raw)
    target = tuple(map(f32, (.31, .42, .53)))
    history = capture(raw[:3], target, rounding)
    exact_log = tuple(math.log2((b + EPS) / (a + EPS)) for a, b in zip(raw, target))
    final, composed, failed = publish([raw], [history], rounding)
    ramp = [[History((x / 16.,) * 3, frames=3., ms=48.) for x in range(8)] for _ in range(8)]
    route = coordinates((3, 3), (8, 8), (.2501, .125), rounding=rounding)
    samples = {}
    for mode in ('nearest', 'cubic'):
        sampled, reason = sample(ramp, route, math_source, mode=mode, rounding=rounding)
        ideal = route['texel'][0] / 16.
        samples[mode] = {'reason': reason, 'edit': sampled.edit[0], 'linear_ramp_error': sampled.edit[0] - ideal,
                         'age': [sampled.frames, sampled.ms]}
    masks = {}
    for name, values in [('ttl', {'max_frames': 3.}), ('depth', {'expected_depth': .8})]:
        _, masks[name] = sample(ramp, route, math_source, **values)
    different = [row[:] for row in ramp]
    different[2][2] = replace(different[2][2], ms=47.)
    _, masks['mixed_origin'] = sample(different, route, math_source, mode='cubic')
    return {'half_rounding': rounding,
            'raw_input_half_max_error': max(abs(a-b) for a,b in zip(raw, input_raw)),
            'capture_max_log_error': max(abs(a-b) for a,b in zip(history.edit, exact_log)),
            'compose_max_rgb_error': max(abs(a-b) for a,b in zip(composed[0], target)),
            'publish_max_rgb_error': max(abs(a-b) for a,b in zip(final[0], target)),
            'publish_incremental_half_error': max(abs(a-b) for a,b in zip(final[0][:3], composed[0][:3])),
            'rgb_error_reference': 'compose/publish errors compare to target; incremental compares publish to compose',
            'return_guard_failed': failed, 'published_alpha': final[0][3], 'raw_half_alpha': raw[3],
            'motion_input': [.2501, .125], 'route': route, 'sampling': samples, 'rejection_reasons': masks}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--rounding', choices=('rne', 'rtz', 'both'), default='both')
    args = parser.parse_args()
    math_source = ShaderMath()
    try:
        result = {'schema': 'nr-cpu-temporal-step-v1', 'gpu_execution': False,
                  'shader_sha256': math_source.source_sha256, 'extracted_shader_functions': ShaderMath.names,
                  'limits': ['Synthetic identity-camera stages, not full shader replay or NVIDIA equivalence.',
                             'Host libm and explicit FP32; GPU FMA, transcendental/denormal behavior may differ.',
                             'RNE and RTZ are explicit bounds, not a claim about a particular GPU store mode.',
                             'Colour-guide and full cardinal-edge correspondence require real input capture.'],
                  'runs': [report(math_source, r) for r in (('rne', 'rtz') if args.rounding == 'both' else (args.rounding,))]}
        print(json.dumps(result, indent=2, allow_nan=False))
    finally:
        math_source.close()


if __name__ == '__main__':
    main()
