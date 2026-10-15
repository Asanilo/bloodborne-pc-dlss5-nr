#!/usr/bin/env python3
"""CPU numerical boundary oracle; shader compilation is separate, never GPU acceptance."""
import math,pathlib,re,unittest
ROOT=pathlib.Path(__file__).resolve().parents[2]
HEADER=(ROOT/'gpu/shadps4/video_core/renderer_vulkan/native_persistent_history_shader.h').read_text()
SHADER=(ROOT/'tools/persistent_history/history.comp').read_text()
def parameter(name):
    literal=re.search(r'\b'+name+r'\{([^}]+)\}',HEADER).group(1).replace('f','')
    return eval(literal,{'__builtins__':{}})
EPS=parameter('Epsilon'); MAX_FRAMES=parameter('MaxFrames'); MAX_MS=parameter('MaxMs')
COLOUR=parameter('ColourTolerance'); DEPTH=parameter('DepthTolerance')
def capture(raw,edited,frames,ms):
    if any(not math.isfinite(v) or v<0 for v in raw+edited):return None
    if frames>MAX_FRAMES or ms>MAX_MS:return None
    edit=tuple(math.log2((b+EPS)/(a+EPS)) for a,b in zip(raw,edited))
    if any(abs(v)>parameter('MaxLogEdit') for v in edit):return None
    return edit,frames,ms

def advance(history,oldraw,raw,olddepth,depth,dtframes=1,dtms=16,exposure=1,mv=(0,0),uv=(.5,.5),edge=False):
    if history is None:return None
    e,frames,ms=history;frames+=dtframes;ms+=dtms
    if frames>MAX_FRAMES or ms>MAX_MS:return None
    if edge or not all(math.isfinite(v) for v in raw+mv+(depth,exposure)):return None
    if any(v<0 or v>=1 for v in (uv[0]+mv[0],uv[1]+mv[1])):return None
    if abs(depth-olddepth)>DEPTH*max(abs(depth),.001):return None
    if exposure<=0 or any(abs(math.log2((a+EPS)/(b*exposure+EPS)))>COLOUR for a,b in zip(raw,oldraw)):return None
    return e,frames,ms

def compose(raw,h):
    if h is None:return raw
    return tuple(max((max(v,0)+EPS)*2**e-EPS,0) for v,e in zip(raw[:3],h[0]))+(raw[3],)

class Boundaries(unittest.TestCase):
    def test_source_contract(self):
        # Validate oracle assumptions against shader actually compiled, not detached constants.
        for text in ['m.zw+=p.age.xy','m=vec4(d,packed(origin),p.age.zw)', 'state.invalid[src]==0u', 'p.flags.w','vec4(rgb,raw.a)','difference','motionImage','p.motion.zw']:
            self.assertIn(text,SHADER)
        self.assertNotIn('m.zw=vec2(0)',SHADER)
    def test_true_origin_age(self):
        h=capture((1,1,1),(2,2,2),4,65)
        self.assertEqual(advance(h,(1,1,1),(1,1,1),.5,.5)[1:],(5,81))
        self.assertIsNone(advance(h,(1,1,1),(1,1,1),.5,.5,9,1))
        self.assertIsNone(advance(h,(1,1,1),(1,1,1),.5,.5,1,136))
    def test_candidate_fail_preserves_active(self):
        old=capture((1,1,1),(1.5,1.5,1.5),3,50)
        candidate=capture((1,1,1),(math.nan,2,2),2,32)
        adopted=candidate if candidate is not None else old
        self.assertIs(adopted,old)
        self.assertEqual(adopted[1:],(3,50))
    def test_occlusion_colour_and_invalid_motion(self):
        h=capture((1,1,1),(2,2,2),0,0)
        for kwargs in [dict(depth=.7),dict(raw=(2,1,1)),dict(edge=True),dict(mv=(math.nan,0)),dict(mv=(1,0))]:
            args=dict(raw=(1,1,1),depth=.5);args.update(kwargs)
            self.assertIsNone(advance(h,(1,1,1),olddepth=.5,**args))
        self.assertIsNotNone(advance(h,(1,1,1),(2,2,2),.5,.5,exposure=2))
    def test_current_raw_and_alpha(self):
        h=capture((1,1,1),(2,2,2),0,0)
        out=compose((1.1,.9,1,.37),h)
        self.assertEqual(out[3],.37)
        self.assertNotEqual(out[:3],(2,2,2))
        self.assertTrue(all(math.isfinite(v) for v in out))
    def test_closed_boundaries(self):
        self.assertIsNotNone(capture((1,1,1),(2,2,2),12,200))
        self.assertIsNone(capture((1,1,1),(2,2,2),12,200.001))
        self.assertIsNone(capture((1,1,1),(math.inf,2,2),0,0))
if __name__=='__main__':unittest.main()
