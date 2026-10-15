#!/usr/bin/env python3
"""CPU numerical boundary oracle; shader compilation is separate, never GPU acceptance."""
import math,pathlib,re,unittest,struct
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

PROJECTION=(1.,1.,1.001,-.1)
def expected_previous(depth,uv=(.5,.5),projection=PROJECTION,previous=PROJECTION,transform=(0,0,1,0)):
    if not all(math.isfinite(x) for x in projection+previous+transform):return None
    if any(abs(v[0])<=1e-8 or abs(v[1])<=1e-8 or v[2]<1 or v[3]>=0 for v in (projection,previous)):return None
    if not math.isfinite(depth) or not 0<=depth<=1:return None
    if depth==1:return 1
    z=projection[3]/(depth-projection[2])
    view=((uv[0]*2-1)*z/projection[0],(uv[1]*2-1)*z/projection[1],z,1)
    prevz=sum(a*b for a,b in zip(transform,view))
    if prevz<=0:return None
    result=previous[2]+previous[3]/prevz
    return result if 0<=result<=1 and math.isfinite(result) else None

def surface(a,b,projection_z=PROJECTION[2]):
    if b is None or not all(math.isfinite(x) and 0<=x<=1 for x in (a,b)):return False
    if a==1 or b==1:return a==1 and b==1
    return abs(a-b)<=max(2e-7,abs(projection_z-b)*DEPTH)

def advance(history,oldraw,raw,olddepth,depth,dtframes=1,dtms=16,exposure=1,mv=(0,0),uv=(.5,.5),edge=False):
    if history is None:return None
    e,frames,ms=history;frames+=dtframes;ms+=dtms
    if frames>MAX_FRAMES or ms>MAX_MS:return None
    if edge or not all(math.isfinite(v) for v in raw+mv+(depth,exposure)):return None
    if any(v<0 or v>=1 for v in (uv[0]+mv[0],uv[1]+mv[1])):return None
    if not surface(olddepth,expected_previous(depth)):return None
    if exposure<=0 or any(abs(math.log2((a+EPS)/(b*exposure+EPS)))>COLOUR for a,b in zip(raw,oldraw)):return None
    return e,frames,ms

def compose(raw,h):
    if h is None:return raw
    return tuple(max((max(v,0)+EPS)*2**e-EPS,0) for v,e in zip(raw[:3],h[0]))+(raw[3],)

def guide_roundtrip(rgb):
    quantized=[round((math.log2(c+EPS)+16)/32*n) for c,n in zip(rgb,(127,255,255))]
    bits=0x3f800000|quantized[0]|quantized[1]<<7|quantized[2]<<15
    stored=struct.unpack('<f',struct.pack('<I',bits))[0]
    retrieved=struct.unpack('<I',struct.pack('<f',stored))[0]
    values=(retrieved&127,(retrieved>>7)&255,(retrieved>>15)&255)
    return stored,tuple(max(2**(q/n*32-16)-EPS,0) for q,n in zip(values,(127,255,255)))

class Boundaries(unittest.TestCase):
    def test_packed_guide_normal_and_colour_precision(self):
        self.assertIn('0x3f800000u|u.x|(u.y<<7)|(u.z<<15)',SHADER)
        for rgb in [(0,0,0),(.001,.03,.1),(1,10,100),(1000,10000,30000)]:
            stored,decoded=guide_roundtrip(rgb)
            self.assertTrue(math.isfinite(stored) and 1<=stored<2)
            for a,b in zip(rgb,decoded):
                self.assertLessEqual(abs(math.log2((a+EPS)/(b+EPS))),32/127/2+1e-10)

    def test_source_contract(self):
        # Validate oracle assumptions against shader actually compiled, not detached constants.
        for text in ['m.zw+=p.age.xy','m=vec4(d,packed(origin),p.age.zw)', 'state.invalid[src]==0u', 'p.flags.w','vec4(rgb,raw.a)','difference','motionImage','p.motion.zw']:
            self.assertIn(text,SHADER)
        self.assertNotIn('m.zw=vec2(0)',SHADER)
    def test_global_fault_propagation_and_cold_stats(self):
        self.assertIn("state.invalid[src]!=0u) atomicOr(state.invalid[dst],state.invalid[src])",SHADER)
        self.assertIn("if(p.reserved1.x>0.)",SHADER)
        self.assertIn("if(p.reserved1.x>0.&&ok",SHADER)
        self.assertIn("float CollectStats{}",HEADER)

    def test_invalid_reason_bits_and_complete_propagation(self):
        names=['PHBadRaw','PHNonfiniteNr','PHNegativeNr','PHBadDepth','PHExcessiveLogEdit','PHBadModelAge','PHGuideOutOfRange','PHNonfiniteLogEdit']
        bits=[int(re.search(name+r'=(\d+)u',HEADER).group(1)) for name in names]
        self.assertEqual(bits,[1<<i for i in range(8)])
        self.assertIn('atomicOr(state.invalid[dst],reason)',SHADER)
        self.assertIn('atomicOr(state.invalid[dst],state.invalid[src])',SHADER)
        # Simultaneous faults on different pixels union across every ping-pong bank.
        captured=bits[1]|bits[2]|bits[4]|bits[5]
        banks=[0,0,captured,0]
        for source,dest in [(2,3),(3,2),(2,3),(3,2)]:
            banks[dest]=0
            banks[dest]|=banks[source]
            self.assertEqual(banks[dest],captured)
        self.assertEqual(captured,54)
        self.assertNotEqual(captured,1)
        self.assertIn('state.invalid[src]==0u',SHADER)

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
    def test_camera_depth_transform_far_disocclusion_and_sky(self):
        self.assertIn('previousZ=dot(p.previousViewZ,view)',SHADER)
        self.assertIn('abs(projectionZ-b)*p.thresholds.y',SHADER)
        self.assertIn('p.reserved1.yz',SHADER)
        # Current surface at view-Z10 was at view-Z15 in previous camera.
        current=PROJECTION[2]+PROJECTION[3]/10
        previous=PROJECTION[2]+PROJECTION[3]/15
        expected=expected_previous(current,transform=(0,0,1,5))
        self.assertAlmostEqual(expected,previous)
        self.assertTrue(surface(previous,expected))
        self.assertFalse(surface(current,expected))
        # Device Z difference .0005 passed old .02 rule despite 2x view distance.
        near=PROJECTION[2]+PROJECTION[3]/50
        far=PROJECTION[2]+PROJECTION[3]/100
        self.assertLess(abs(near-far),.02*far)
        self.assertFalse(surface(near,far))
        self.assertTrue(surface(1,expected_previous(1)))
        self.assertFalse(surface(.999999,1))
        self.assertFalse(surface(1,.999999))
        self.assertIsNone(expected_previous(.5,projection=(0,0,0,0)))
        self.assertIsNone(expected_previous(.5,transform=(0,0,1,-100)))
        self.assertIsNone(expected_previous(math.nan))
        # Same-camera neighbourhood edge uses the same distance-scaled test.
        self.assertFalse(surface(near,far))

    def test_closed_boundaries(self):
        self.assertIsNotNone(capture((1,1,1),(2,2,2),12,200))
        self.assertIsNone(capture((1,1,1),(2,2,2),12,200.001))
        self.assertIsNone(capture((1,1,1),(math.inf,2,2),0,0))
if __name__=='__main__':unittest.main()
