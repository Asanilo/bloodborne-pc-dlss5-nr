"""Actual frame snapshot/readiness source; deterministic FIFO completion interleavings."""
from pathlib import Path
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[2]
header = (ROOT / 'gpu/shadps4/video_core/amdgpu/liverpool.h').read_text()
source = (ROOT / 'gpu/shadps4/video_core/amdgpu/liverpool.cpp').read_text()


def block(text, marker):
    start = text.index(marker)
    opening = text.index('{', start)
    at, depth = opening + 1, 1
    while depth:
        depth += (text[at] == '{') - (text[at] == '}')
        at += 1
    return text[start:at]


# Use the production queue bounds (five Num* constants plus GfxQueueId).
queue_constants = '\n'.join(
    line.strip().replace('u32', 'uint32_t')
    for line in header.splitlines()
    if 'static constexpr u32' in line and any(
        name in line for name in ('GfxQueueId', 'NumGfxRings', 'NumComputePipes',
                                  'NumQueuesPerPipe', 'NumComputeRings', 'NumTotalQueues'))
)
assert queue_constants.count('static constexpr') == 6
frame_type = block(header, '    struct FrameEnd') + ';'
snapshot = next(line.strip() for line in header.splitlines() if 'frame_ends.push_back(' in line)
start = source.index('void Liverpool::SignalDecodedFrames()')
end = source.index('    if (frame == 0 || !rasterizer)', start)
signal = source[start:end].replace('void Liverpool::SignalDecodedFrames()', 'u64 SignalDecodedFrames()') + 'return frame;\n}\n'
# Exercise the actual publication and completion counter statements, not a replacement model.
assert '++queue_submitted[GfxQueueId];' in source
assert '++queue_submitted[gnm_vqid];' in source
assert '++queue_decoded[curr_qid];' in source
assert 'frame_ends.front().IsDecoded(queue_decoded)' in source
# The consumer advances round-robin even when a task resumes but does not finish.
loop = source[source.index('        while (num_submits || num_commands)'):source.index('        // Consume before doing work')]
assert loop.index('curr_qid = (curr_qid + 1)') < loop.index('task.resume();') < loop.index('if (task.done())')
assert 'queue.submits.pop();' in loop
prefix = r'''
#include <array>
#include <deque>
#include <mutex>
#include <cstdint>
#include <cassert>
#include <cstdio>
using u64=uint64_t;
QUEUE_CONSTANTS
std::array<u64,NumTotalQueues> queue_submitted{},queue_decoded{};
u64 decoded_total=0,submissions_total=0;
std::mutex submit_mutex;
'''
prefix = prefix.replace('QUEUE_CONSTANTS', queue_constants)
helpers = r'''
void submit(size_t q){++submissions_total;if(q==GfxQueueId){++queue_submitted[GfxQueueId];}else{size_t gnm_vqid=q;++queue_submitted[gnm_vqid];}}
void complete(size_t curr_qid){++decoded_total;++queue_decoded[curr_qid];}
void done(u64 frame){SNAPSHOT}
void reset(){queue_submitted={};queue_decoded={};frame_ends.clear();decoded_total=submissions_total=0;}
'''.replace('SNAPSHOT', snapshot)
main = r'''
int main(){
// F1 Gfx A yields, ASC B completes. F2 ASC C then completes on the same ASC FIFO.
submit(0);submit(1);done(1);submit(1);done(2);
complete(1);assert(SignalDecodedFrames()==0);
complete(1);assert(SignalDecodedFrames()==0); // A still outstanding
assert(decoded_total==2); // legacy target 2 incorrectly considers F1 finished
complete(0);assert(SignalDecodedFrames()==2);
// Multiple queues/frames: younger work cannot substitute for another queue's prefix.
reset();submit(0);submit(2);done(1);submit(2);submit(3);done(2);submit(3);done(3);
complete(2);complete(2);complete(3);complete(3);assert(SignalDecodedFrames()==0);
complete(0);assert(SignalDecodedFrames()==3);
// Ready old frame may retire while a later frame still owns a different queue.
reset();submit(0);done(1);submit(NumTotalQueues-1);done(2);
complete(0);assert(SignalDecodedFrames()==1);assert(SignalDecodedFrames()==0);
complete(NumTotalQueues-1);assert(SignalDecodedFrames()==2);
// Same FIFO normal completion; empty frame shares preceding targets.
reset();submit(0);done(1);done(2);submit(0);done(3);
complete(0);assert(SignalDecodedFrames()==2);complete(0);assert(SignalDecodedFrames()==3);
// Initial/unused queues are zero targets and must not hold an empty frame.
reset();done(1);assert(SignalDecodedFrames()==1);assert(SignalDecodedFrames()==0);
std::puts("actual per-queue frame retirement PASS");
}
'''
# Negative control is the previous total-count criterion on the same source block.
legacy = signal.replace('frame_ends.front().IsDecoded(queue_decoded)', 'total(frame_ends.front().targets) <= decoded_total')
negative = r'''
int main(){submit(0);submit(1);done(1);submit(1);complete(1);complete(1);
assert(queue_decoded[0]==0);assert(SignalDecodedFrames()==1);
std::puts("legacy premature retirement reproduced");}
'''
with tempfile.TemporaryDirectory(prefix='nr-submit-frame-') as directory:
    directory = Path(directory)
    for name, body, tests in [('actual', signal, main), ('legacy', legacy, negative)]:
        text = prefix + frame_type + '\nstd::deque<FrameEnd> frame_ends;\n'
        text += 'u64 total(const std::array<u64,NumTotalQueues>& a){u64 n=0;for(auto v:a)n+=v;return n;}\n'
        cpp = directory / (name + '.cpp')
        exe = directory / name
        cpp.write_text(text + body + helpers + tests)
        subprocess.run(['c++', '-std=c++20', '-O2', '-pthread', str(cpp), '-o', str(exe)], check=True)
        subprocess.run([str(exe)], check=True)
