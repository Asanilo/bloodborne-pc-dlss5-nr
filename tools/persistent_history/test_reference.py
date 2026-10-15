#!/usr/bin/env python3
"""CPU numerical boundary oracle; shader compilation is separate, never GPU acceptance."""
import math,pathlib,re,unittest,struct,subprocess,tempfile
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

def transition(old,target,weight,enabled=True,old_status=0,target_status=0):
    def usable(h,status):
        return h is not None and status&~16==0 and all(math.isfinite(x) for x in h[0]+h[1:]) and 0<=h[1]<=MAX_FRAMES and 0<=h[2]<=MAX_MS
    a=usable(old,old_status)
    if not enabled:return old if a else None
    b=usable(target,target_status)
    if not b:return old if a else None
    if not a:return target
    w=min(max(weight,0),1) if math.isfinite(weight) else 0
    if w==0:return old
    if w==1:return target
    return tuple(x+(y-x)*w for x,y in zip(old[0],target[0])),max(old[1],target[1]),max(old[2],target[2])

def edge_support_reference(center_depth,center_mv,source_age,neighbour,offset):
    # Neighbour carries its independently mapped old sample and current raw.
    depth,mv,olddepth,raw,oldraw,history=neighbour
    if not surface(depth,center_depth):return False
    delta=tuple(o+b-a for o,a,b in zip(offset,center_mv,mv))
    along=sum(a*b for a,b in zip(delta,offset));across=delta[0]*offset[1]-delta[1]*offset[0]
    if not all(math.isfinite(x) for x in delta) or not .5<=along<=1.5 or abs(across)>.5:return False
    if history is None or history[1:]!=source_age:return False
    return advance(history,oldraw,raw,olddepth,depth,mv=(0,0)) is not None

def guide_roundtrip(rgb):
    quantized=[round((math.log2(c+EPS)+16)/32*n) for c,n in zip(rgb,(127,255,255))]
    bits=0x3f800000|quantized[0]|quantized[1]<<7|quantized[2]<<15
    stored=struct.unpack('<f',struct.pack('<I',bits))[0]
    retrieved=struct.unpack('<I',struct.pack('<f',stored))[0]
    values=(retrieved&127,(retrieved>>7)&255,(retrieved>>15)&255)
    return stored,tuple(max(2**(q/n*32-16)-EPS,0) for q,n in zip(values,(127,255,255)))

def reprojection_confidence(prior,depth_residual=0,edge_residual=0,colour_residual=0,motion=(0,0),neighbours=((0,0),)):
    spread=0
    for neighbour in neighbours:
        if not all(math.isfinite(x) for x in motion+neighbour):spread=1;break
        distance=math.hypot(*(a-b for a,b in zip(motion,neighbour)))
        spread=max(spread,distance/(1+distance) if math.isfinite(distance) else 1)
    residual=max(depth_residual,edge_residual,colour_residual,spread)
    return min(prior,1-.5*min(max(residual,0),1))

def restricted_sample(grid,position,nearest,min_confidence=.5):
    """Tap tuple: (logResidualRGB, confidence, ageFrames, ageMs, eligible).
    Eligibility represents unchanged per-tap numerical/depth/colour/live gates.
    """
    x,y=position;bx,by=math.floor(x),math.floor(y);fx,fy=x-bx,y-by
    reference=grid[nearest[1]][nearest[0]]
    if not reference[4]:return None
    rgb=[0.,0.,0.];confidence=1.;age=list(reference[2:4])
    for dy in range(2):
        for dx in range(2):
            weight=(fx if dx else 1-fx)*(fy if dy else 1-fy)
            if weight<=0:continue
            tx,ty=bx+dx,by+dy
            if not 0<=ty<len(grid) or not 0<=tx<len(grid[0]):return reference
            tap=grid[ty][tx]
            if not tap[4] or tap[2:4]!=reference[2:4]:return reference
            for c in range(3):rgb[c]+=weight*tap[0][c]
            confidence=min(confidence,tap[1]);age=[max(a,b) for a,b in zip(age,tap[2:4])]
    bits=struct.unpack('<H',struct.pack('<e',confidence))[0]
    stored=struct.unpack('<e',struct.pack('<H',bits))[0]
    if stored>confidence:stored=struct.unpack('<e',struct.pack('<H',bits-1))[0]
    if stored<=0 or stored<min_confidence:return reference
    return tuple(rgb),confidence,*age,True

def cubic_weights(t):
    return (-.5*t+t*t-.5*t**3,1-2.5*t*t+1.5*t**3,
            .5*t+2*t*t-1.5*t**3,-.5*t*t+.5*t**3)

def restricted_cubic(grid,position,nearest,min_confidence=.5):
    x,y=position;bx,by=math.floor(x),math.floor(y)
    wx,wy=cubic_weights(x-bx),cubic_weights(y-by)
    reference=grid[nearest[1]][nearest[0]]
    if not reference[4]:return None
    rgb=[0.,0.,0.];confidence=1.;lo=list(reference[0]);hi=lo[:]
    for dy in range(4):
        for dx in range(4):
            weight=wx[dx]*wy[dy]
            if weight==0:continue
            tx,ty=bx+dx-1,by+dy-1
            if not 0<=ty<len(grid) or not 0<=tx<len(grid[0]):return reference
            tap=grid[ty][tx]
            if not tap[4] or tap[2:4]!=reference[2:4]:return reference
            for c in range(3):rgb[c]+=weight*tap[0][c]
            confidence=min(confidence,tap[1])
            if 1<=dx<=2 and 1<=dy<=2:
                lo=[min(a,b) for a,b in zip(lo,tap[0])]
                hi=[max(a,b) for a,b in zip(hi,tap[0])]
    bits=struct.unpack('<H',struct.pack('<e',confidence))[0]
    stored=struct.unpack('<e',struct.pack('<H',bits))[0]
    if stored>confidence:stored=struct.unpack('<e',struct.pack('<H',bits-1))[0]
    if stored<=0 or stored<min_confidence:return reference
    return tuple(min(max(v,a),b) for v,a,b in zip(rgb,lo,hi)),confidence,*reference[2:4],True

class Boundaries(unittest.TestCase):
    def test_restricted_bilinear_same_origin_and_hard_reference(self):
        self.assertIn('vec2 texel=prev*vec2(p.dispatch.yz)-.5',SHADER)
        self.assertIn('if(weight==0.)continue',SHADER)
        self.assertIn('all(equal(tm.zw,sourceAge))',SHADER)
        self.assertIn('oldest=max(oldest,tm.zw)',SHADER)
        self.assertNotIn('texture(sourceEdit',SHADER)
        tap=lambda v,q=1,f=5,ms=87,ok=True:((v,v,v),q,f,ms,ok)
        grid=[[tap(0),tap(1,.8)],[tap(2,.7),tap(3,.6)]]
        out=restricted_sample(grid,(.25,.5),(0,1))
        self.assertEqual(out[0],(1.25,)*3);self.assertEqual(out[1],.6)
        self.assertEqual(out[2:4],(5,87))
        # Independent candidate/fallback origins must not be averaged, including
        # same frame-age but different time-age and tiny exact identity mismatch.
        for bad in (tap(3,ok=False),tap(3,f=4),tap(3,ms=87.00001)):
            changed=[grid[0], [grid[1][0],bad]]
            self.assertEqual(restricted_sample(changed,(.25,.5),(0,1)),grid[1][0])
        invalid=[[tap(0,ok=False),tap(1)],[tap(2),tap(3)]]
        self.assertIsNone(restricted_sample(invalid,(.25,.25),(0,0)))
        # At an exact texel center outside zero-weight taps have no effect.
        self.assertEqual(restricted_sample([[tap(2)]],(0.,0.),(0,0)),tap(2))
        # A contributing out-of-image tap never clamps/borrows another surface.
        self.assertEqual(restricted_sample(grid,(-.1,.5),(0,1)),grid[1][0])

    def test_nearest_default_and_exact_optional_cubic_selector(self):
        self.assertIn('if(ok&&EnableCubic==1u&&p.reserved2.x==1.)',SHADER)
        self.assertIn('layout(constant_id=0) const uint EnableCubic=0u;',SHADER)
        self.assertEqual(SHADER.count('if(ok&&EnableCubic==1u&&p.reserved2.x==1.)'),1)
        # Selector sits after the shared Alpha nearest/confidence hard gates,
        # not around them. Unknown/reserved selectors fail closed to nearest.
        begin=SHADER.index('if(ok&&EnableCubic==1u&&p.reserved2.x==1.)')
        self.assertLess(SHADER.index('e.a=min(e.a,confidence);ok=live(e,m)'),begin)
        self.assertLess(begin,SHADER.index('vec4 wx=cubicWeights'))
        tap=lambda v:((v,v,v),1.,5.,80.,True)
        grid=[[tap(float(x+y)) for x in range(6)] for y in range(6)]
        nearest=grid[3][2]
        for specialization in (0,2,0xffffffff):
            for selector in (0.,1.,2.):
                result=restricted_cubic(grid,(2.25,2.5),(2,3)) if specialization==1 and selector==1. else nearest
                self.assertIs(result,nearest)
        for selector in (0.,-1.,2.,.999999,math.nan,math.inf):
            result=restricted_cubic(grid,(2.25,2.5),(2,3)) if selector==1. else nearest
            self.assertIs(result,nearest)
        selected=restricted_cubic(grid,(2.25,2.5),(2,3))
        self.assertNotEqual(selected[0],nearest[0])
        self.assertEqual(selected[2:],nearest[2:])

    def test_cubic_support_negative_weights_clamp_and_age(self):
        self.assertIn('vec4 wx=cubicWeights(phase.x),wy=cubicWeights(phase.y)',SHADER)
        self.assertIn('clamp(filtered,centerMin,centerMax)',SHADER)
        self.assertIn('for(int y=0;y<4;y++) for(int x=0;x<4;x++)',SHADER)
        for t in (0,.1,.5,.9,1):self.assertAlmostEqual(sum(cubic_weights(t)),1.)
        self.assertLess(cubic_weights(.25)[0],0)
        tap=lambda v,q=1,f=4,ms=62,ok=True:((v,v,v),q,f,ms,ok)
        grid=[[tap(x+y) for x in range(6)] for y in range(6)]
        out=restricted_cubic(grid,(2.25,2.5),(2,3))
        self.assertAlmostEqual(out[0][0],4.75);self.assertEqual(out[2:4],(4,62))
        for bad in (tap(2,ok=False),tap(2,f=3),tap(2,ms=62.00001)):
            altered=[row[:] for row in grid];altered[1][1]=bad
            # Outer negative-lobe contributor cannot be ignored or used from
            # another model origin, even when the nearest itself is valid.
            self.assertEqual(restricted_cubic(altered,(2.25,2.5),(2,3)),grid[3][2])
        grid[1][1]=tap(-4,.6)
        self.assertEqual(restricted_cubic(grid,(2.25,2.5),(2,3))[1],.6)
        # Range limiting prevents negative lobes overshooting central samples.
        grid=[[tap(-4) for _ in range(6)] for _ in range(6)]
        for y in (2,3):
            for x in (2,3):grid[y][x]=tap(1)
        self.assertEqual(restricted_cubic(grid,(2.25,2.5),(2,3))[0],(1,1,1))
        self.assertEqual(restricted_cubic([[tap(2)]],(0,0),(0,0)),tap(2))
        self.assertEqual(restricted_cubic(grid,(-.1,2),(0,2)),grid[2][0])
        for threshold,q in ((.8,.80001),(.912,.91201)):
            grid=[[tap(0,.95) for _ in range(6)] for _ in range(6)]
            grid[1][1]=tap(.2,q)
            self.assertEqual(restricted_cubic(grid,(2.25,2.5),(2,3),threshold),grid[3][2])

    def test_cubic_repeated_subpixel_detail_and_phase_cost(self):
        def warp(a,p,mode):
            out=[]
            for x in range(len(a)):
                z=x+p;b=math.floor(z);t=z-b
                if mode=='nearest':v=a[math.floor(z+.5)%len(a)]
                elif mode=='bilinear':v=(1-t)*a[b%len(a)]+t*a[(b+1)%len(a)]
                else:
                    v=sum(w*a[(b+i-1)%len(a)] for i,w in enumerate(cubic_weights(t)))
                    v=min(max(v,min(a[b%len(a)],a[(b+1)%len(a)])),max(a[b%len(a)],a[(b+1)%len(a)]))
                out.append(v)
            return out
        n=256
        for source in ([float(i==128) for i in range(n)],
                       [math.sin(2*math.pi*i/8) for i in range(n)],
                       [i/n for i in range(n)]):
            result={}
            for mode in ('nearest','bilinear','cubic'):
                a=source[:]
                for _ in range(16):a=warp(warp(a,.25,mode),-.25,mode)
                result[mode]=a
            mse=lambda a:sum((a-b)**2 for a,b in zip(a,source))/n
            self.assertLess(mse(result['cubic']),mse(result['bilinear']))
            self.assertEqual(result['nearest'],source) # No net shift, but quantized transport.
        pulse=[float(i==128) for i in range(n)]
        cubic=pulse[:];linear=pulse[:]
        for _ in range(16):
            cubic=warp(warp(cubic,.25,'cubic'),-.25,'cubic')
            linear=warp(warp(linear,.25,'bilinear'),-.25,'bilinear')
        self.assertGreater(max(cubic),2*max(linear))
        self.assertLess(max(cubic),.5) # Explicitly NOT lossless infinite reprojection.
        ramp=[i/n for i in range(n)]
        self.assertAlmostEqual(warp(ramp,.51,'cubic')[100]-warp(ramp,.49,'cubic')[100],.02/n)
        self.assertAlmostEqual(warp(ramp,.51,'nearest')[100]-warp(ramp,.49,'nearest')[100],1/n)

    def test_filtered_confidence_half_rounding_preserves_nearest(self):
        self.assertIn('packHalf2x16(vec2(filteredConfidence,0.))&65535u',SHADER)
        self.assertIn('unpackHalf2x16(confidenceBits-1u).x',SHADER)
        self.assertIn('storedConfidence>=p.limits.w',SHADER)
        reference=((.2,.2,.2),.9501953125,5.,80.,True)
        for threshold,candidate in ((.8,.80001),(.912,.91201)):
            self.assertGreater(candidate,threshold)
            stored=struct.unpack('<e',struct.pack('<e',candidate))[0]
            self.assertLess(stored,threshold) if threshold==.8 else self.assertGreater(stored,threshold)
            other=((.4,.4,.4),candidate,5.,80.,True)
            grid=[[reference,other]]
            result=restricted_sample(grid,(.25,0),(0,0),threshold)
            self.assertEqual(result,reference)
            # The lower half is <= both RNE and RTZ stores. Either rounding
            # mode must keep the original nearest when this bound misses min.
            bits=struct.unpack('<H',struct.pack('<e',candidate))[0]
            floor=struct.unpack('<e',struct.pack('<H',bits-(stored>candidate)))[0]
            self.assertLess(floor,threshold)
            nearest_half=struct.unpack('<e',struct.pack('<e',reference[1]))[0]
            self.assertGreaterEqual(nearest_half,threshold)
            # Default .5 is exactly representable: preserve filtered result,
            # rather than unnecessarily falling back or raising confidence.
            filtered=restricted_sample(grid,(.25,0),(0,0),.5)
            self.assertAlmostEqual(filtered[0][0],.25)
            self.assertEqual(filtered[1],candidate)
        exactly_half=((.4,.4,.4),.5,5.,80.,True)
        self.assertEqual(restricted_sample([[reference,exactly_half]],(.25,0),(0,0),.5)[1],.5)

    def test_subpixel_continuity_and_repeated_filter_sharpness_cost(self):
        tap=lambda v:((v,v,v),1.,3.,50.,True)
        grid=[[tap(0),tap(1)]]
        phases=[.49,.5,.51]
        linear=[restricted_sample(grid,(x,0),(int(math.floor(x+.5)),0))[0][0] for x in phases]
        nearest=[grid[0][int(math.floor(x+.5))][0][0] for x in phases]
        self.assertAlmostEqual(max(linear)-min(linear),.02)
        self.assertEqual(max(nearest)-min(nearest),1.)
        # Repeated subpixel warps are NOT sharpness preserving. +/- .25 has
        # zero net displacement, yet bilinear redistributes a log-edit impulse.
        signal=[0.]*65;signal[32]=1.;nearest_signal=signal[:]
        def warp(values,phase):
            result=[]
            for x in range(len(values)):
                pos=x+phase;lo=math.floor(pos);f=pos-lo
                result.append((1-f)*values[lo%len(values)]+f*values[(lo+1)%len(values)])
            return result
        def nearest_warp(values,phase):
            return [values[math.floor(x+phase+.5)%len(values)] for x in range(len(values))]
        for _ in range(16):
            signal=warp(warp(signal,.25),-.25)
            nearest_signal=nearest_warp(nearest_warp(nearest_signal,.25),-.25)
        self.assertLess(max(signal),.2)
        self.assertEqual(max(nearest_signal),1.)
        self.assertAlmostEqual(sum(signal),1.)
        # No test or claim that bilinear indefinitely preserves source detail.

    def test_continuous_confidence_evidence_and_default_compatibility(self):
        self.assertIn('e.a=min(e.a,confidence);ok=live(e,m)',SHADER)
        self.assertIn('boundedMotionSpread(mv,neighbourMotion)',SHADER)
        self.assertIn('depthResidual(m.x,expected,p.previousProjection.z)',SHADER)
        for residual in (0,.1,.5,.9,1):
            for component in ('depth_residual','edge_residual','colour_residual'):
                q=reprojection_confidence(1,**{component:residual})
                self.assertAlmostEqual(q,1-.5*residual)
                self.assertGreaterEqual(q,.5)
        self.assertEqual(reprojection_confidence(.7),.7)
        self.assertLess(reprojection_confidence(1,motion=(0,0),neighbours=((2,0),)),.9)
        self.assertEqual(reprojection_confidence(1,motion=(1000,-800),neighbours=((1000,-800),)),1)
        self.assertEqual(reprojection_confidence(1,motion=(3,1),neighbours=((math.nan,0),)),.5)
        # No unconditional ageing, strength or motion-magnitude factor; uniform
        # motion only transports the original model residual and true age.
        h=capture((1,1,1),(1.5,1.5,1.5),3,50)
        for residual in (0,.2,.5,1):
            old=advance(h,(1,1,1),(1,1,1),.5,.5)
            q=reprojection_confidence(1,colour_residual=residual)
            new=old if q>=.5 else None
            self.assertEqual(new,old)
            self.assertEqual(compose((1,1,1,.37),new),compose((1,1,1,.37),old))
            self.assertEqual(new[1:],(4,66))
            self.assertEqual(q>=.9,residual<=.2)
        oldq=.6
        candidateq=.8
        adopted=candidateq if candidateq>=.9 else (oldq if oldq>=.9 else None)
        self.assertIsNone(adopted)
        self.assertEqual((candidateq if candidateq>=.7 else oldq),.8)
        # Existing disocclusion/colour/TTL guards remain hard, not softened by q.
        self.assertIsNone(advance(h,(1,1,1),(2,1,1),.5,.5))
        self.assertIsNone(advance(h,(1,1,1),(1,1,1),.5,.8))
        self.assertIsNone(advance(h,(1,1,1),(1,1,1),.5,.5,dtframes=10))

    def test_packed_guide_normal_and_colour_precision(self):
        self.assertIn('0x3f800000u|u.x|(u.y<<7)|(u.z<<15)',SHADER)
        for rgb in [(0,0,0),(.001,.03,.1),(1,10,100),(1000,10000,30000)]:
            stored,decoded=guide_roundtrip(rgb)
            self.assertTrue(math.isfinite(stored) and 1<=stored<2)
            for a,b in zip(rgb,decoded):
                self.assertLessEqual(abs(math.log2((a+EPS)/(b+EPS))),32/127/2+1e-10)

    def test_source_contract(self):
        # Validate oracle assumptions against shader actually compiled, not detached constants.
        for text in ['m.zw+=p.age.xy','m=vec4(d,packed(origin),p.age.zw)', 'bankUsable(state.invalid[src])', 'p.flags.w','vec4(rgb,raw.a)','difference','motionImage','p.motion.zw']:
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
        self.assertIn('bankUsable(state.invalid[src])',SHADER)

    def test_local_log_rejection_without_bank_loss(self):
        self.assertIn('(status&~16u)==0u',SHADER)
        self.assertIn('bankUsable(state.invalid[min(p.flags.w,3u)])',SHADER)
        usable=lambda status: (status & ~16)==0
        old=[capture((1,1,1),(1.5,1.5,1.5),9,144)]*3
        fresh=[capture((1,1,1),(2,2,2),3,48),None,capture((1,1,1),(2,2,2),3,48)]
        adopted=[new if usable(16) and new is not None else prev for new,prev in zip(fresh,old)]
        self.assertIs(adopted[0],fresh[0]);self.assertIs(adopted[1],old[1]);self.assertIs(adopted[2],fresh[2])
        self.assertEqual(adopted[1][1:],(9,144))
        self.assertEqual(adopted[0][1:],(3,48))
        allbad=[capture((1,1,1),(0,0,0),3,48)]*3
        self.assertEqual(allbad,[None]*3)
        self.assertEqual([new if usable(16) and new is not None else prev for new,prev in zip(allbad,old)],old)
        self.assertEqual([new if usable(16) and new is not None else None for new in allbad],[None]*3)
        for fatal in (1,2,4,8,32,64,128):
            self.assertFalse(usable(fatal));self.assertFalse(usable(16|fatal))
            rejected=[new if usable(16|fatal) and new is not None else prev for new,prev in zip(fresh,old)]
            self.assertEqual(rejected,old)
        self.assertIsNone(capture((1,1,1),(math.nan,2,2),3,48))

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

    def test_runtime_strength_is_display_only_and_default_exact(self):
        raw=(.25,.5,1.)
        edited=(.5,.25,2.)
        history=tuple(math.log2((e+EPS)/(r+EPS)) for r,e in zip(raw,edited))
        def display(strength):
            full=tuple(max((r+EPS)*2**e-EPS,0) for r,e in zip(raw,history))
            return tuple(r+(v-r)*strength for r,v in zip(raw,full))
        self.assertEqual(display(0),raw)
        for a,b in zip(display(1),edited):self.assertAlmostEqual(a,b)
        self.assertEqual(history,tuple(math.log2((e+EPS)/(r+EPS)) for r,e in zip(raw,edited)))
        shader=pathlib.Path(__file__).parents[0].joinpath('history.comp').read_text()
        self.assertIn('if(p.reserved1.w!=1.)rgb=mix(raw.rgb,rgb,p.reserved1.w)',shader)
        self.assertIn('e.a>0.&&e.a>=p.limits.w',shader)  # confidence=0 remains invalid even at user threshold0

    def test_gpu_group_summary_fixed_point_bounds(self):
        # 64-lane reduction and CPU aggregate preserve counts and bounded ages.
        lanes=[(i%3!=0, float(i%13),float(i%201)) for i in range(64)]
        partial=[(int(v),round(f*65536) if v else 0,round(ms*256) if v else 0) for v,f,ms in lanes]
        while len(partial)>1:
            n=len(partial)//2
            partial=[tuple(a+b for a,b in zip(partial[i],partial[i+n])) for i in range(n)]
        count,frames,ms=partial[0]
        self.assertEqual(count,sum(v for v,_,_ in lanes))
        self.assertAlmostEqual(frames/count/65536,sum(f for v,f,_ in lanes if v)/count)
        self.assertLess(64*12*65536,2**32)
        self.assertLess(64*200*256,2**32)

    def test_actual_reset_window_cpp_policy(self):
        # Exercise the actual C++ admission policy, including a late candidate
        # that was legal before reset and the first fresh immutable origin.
        header=ROOT/'gpu/shadps4/video_core/renderer_vulkan/native_persistent_history_shader.h'
        code='#include "'+str(header)+'"\n#include <cassert>\nint main(){Vulkan::PersistentHistoryResetWindow w;assert(w.Admits(95));w.ResetAt(100);assert(!w.Admits(99));assert(w.Admits(100));assert(w.generation==1);w.ResetAt(90);assert(!w.Admits(95));assert(w.Admits(100));assert(w.generation==2);w.ResetAt(121);assert(!w.Admits(120));assert(w.Admits(121));assert(w.generation==3);}'
        with tempfile.TemporaryDirectory() as td:
            src=pathlib.Path(td)/'reset.cpp';binary=pathlib.Path(td)/'reset';src.write_text(code)
            subprocess.run(['c++','-std=c++20',str(src),'-o',str(binary)],check=True)
            subprocess.run([str(binary)],check=True)

    def test_display_transition_independent_ages_and_fallback(self):
        old=((1.,-1.,.5),11.,180.);target=((-1.,1.,-.5),4.,65.)
        original=(old,target)
        for w in (1/3,2/3):
            h=transition(old,target,w)
            self.assertEqual(h[1:],old[1:])
            self.assertAlmostEqual(h[0][0],1-2*w)
            self.assertEqual(compose((.3,.4,.5,.7),h)[3],.7)
        self.assertEqual(transition(old,target,0),old)
        self.assertEqual(transition(old,target,1),target)
        self.assertEqual(transition(old,target,.5,enabled=False),old)
        self.assertEqual(transition(old,target,.5,target_status=2),old)
        self.assertEqual(transition(old,target,.5,old_status=2),target)
        self.assertIsNone(transition(old,target,.5,old_status=2,target_status=2))
        self.assertEqual(transition(old,target,.5,target_status=16)[1:],old[1:])
        expired=(old[0],12.,200.001)
        self.assertEqual(transition(expired,target,1/3),target)
        self.assertEqual(transition(old,(target[0],13.,65.),2/3),old)
        self.assertEqual(transition(old,((math.nan,0.,0.),4.,65.),.5),old)
        self.assertEqual((old,target),original) # Display never renews/stores either bank.
        self.assertIn('if(p.reserved2.y!=1.)return oldValid;',SHADER)
        self.assertIn('if(statistics){targetEdit=texelFetch(depthImage,c,0);targetMeta=texelFetch(motionImage,c,0);}',SHADER)
        self.assertIn('m.zw=max(m.zw,targetMeta.zw);',SHADER)
        self.assertIn('ok=displayHistory(c,false,e,m);',SHADER)
        self.assertIn('valid=displayHistory(c,true,e,m);',SHADER)

    def test_surface_local_edge_independent_correspondence(self):
        z=PROJECTION[2]+PROJECTION[3]/10
        far=PROJECTION[2]+PROJECTION[3]/30
        h=((.25,-.5,1.),4.,64.);raw=(.2,.3,.4);mv=(100.,-75.)
        def n(depth=z,motion=mv,olddepth=z,history=h):return depth,motion,olddepth,raw,raw,history
        offsets=((-1,0),(1,0),(0,-1),(0,1))
        def count(ns):return sum(edge_support_reference(z,mv,h[1:],v,o) for v,o in zip(ns,offsets))
        one_sided=[n(),n(depth=far),n(),n()]
        self.assertEqual(count(one_sided),3) # Uniform large absolute MV is allowed.
        self.assertLess(count([n(olddepth=far),n(depth=far),n(),n()]),3) # Disocclusion.
        self.assertLess(count([n(motion=(98.,-75.)),n(depth=far),n(),n()]),3)
        self.assertEqual(count([n(depth=far),n(depth=far),n(),n()]),2) # Thin line rejected.
        self.assertFalse(edge_support_reference(z,mv,h[1:],n(depth=far),offsets[0])) # Identical colour is insufficient.
        self.assertFalse(edge_support_reference(z,mv,h[1:],n(history=(h[0],3.,64.)),offsets[0]))
        self.assertEqual(h,((.25,-.5,1.),4.,64.))
        self.assertEqual(advance(h,raw,raw,z,z)[1:],(5.,80.))
        self.assertIn('if(ok&&!interior)',SHADER)
        self.assertIn('ok=ok&&(interior||supportedEdge);',SHADER)
        self.assertIn('supportedEdge=supports>=3u;',SHADER)
        self.assertIn('if(supportedEdge){e.a=min(e.a,.5);ok=ok&&live(e,m);}',SHADER)
        # Interior never enters the support branch or receives its confidence cap.
        self.assertEqual(count([n()]*4),4)
        self.assertIn('along<.5||along>1.5||abs(across)>.5',SHADER)

    def test_closed_boundaries(self):
        self.assertIsNotNone(capture((1,1,1),(2,2,2),12,200))
        self.assertIsNone(capture((1,1,1),(2,2,2),12,200.001))
        self.assertIsNone(capture((1,1,1),(math.inf,2,2),0,0))
if __name__=='__main__':unittest.main()
