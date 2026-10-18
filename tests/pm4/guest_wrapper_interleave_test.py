#!/usr/bin/env python3
"""Private guest-wrapper execution + actual CS builder; CPU only.

The input disassembly is private and its bytes are never stored in this test.
Only the HLE call displacement is relocated. The forced background writer is a
synthetic SET_CONTEXT workload: this proves a mechanism, not the actual run's
unrecorded background writer or the absence of an upstream ownership contract.
"""
from pathlib import Path
import re
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]
PRIVATE = ROOT.parent / 'analysis/pm4-overlap-20261016/guest-wrapper-disassembly.txt'


def block(source, marker):
    start = source.index(marker)
    opening = source.index('{', start)
    at, depth = opening + 1, 1
    while depth:
        depth += (source[at] == '{') - (source[at] == '}')
        at += 1
    return source[start:at]


class GuestWrapperInterleaveTest(unittest.TestCase):
    @unittest.skipUnless(PRIVATE.exists(), 'requires private local guest disassembly')
    def test_actual_wrapper_and_cs_builder(self):
        memory = {}
        for line in PRIVATE.read_text().splitlines():
            match = re.match(r'\s*([0-9a-f]+):\s*((?:[0-9a-f]{2}(?:\s+|$))+)', line)
            if match:
                address = int(match[1], 16)
                for offset, byte in enumerate(bytes.fromhex(match[2])):
                    memory[address + offset] = byte
        wrappers = []
        for start, end, call in ((0x1075840, 0x1075893, 0x1075880),
                                 (0x10753c0, 0x1075416, 0x1075400)):
            code = bytes(memory[at] for at in range(start, end))
            self.assertEqual(code[call-start], 0xe8)
            wrappers.append((code, call-start))
        driver = (ROOT / 'gpu/shadps4/core/libraries/gnmdriver/gnmdriver.cpp').read_text()
        packets = (ROOT / 'gpu/shadps4/video_core/amdgpu/pm4_cmds.h').read_text()
        helpers = '\n'.join(block(packets, marker) + ';' for marker in (
            'enum class PM4ShaderType', 'enum class PM4Predicate', 'union PM4Type3Header'))
        helpers += '\n' + packets[packets.index('// Write the PM4 header'):packets.index('union ContextControlEnable')]
        helpers += '\n' + block(packets, 'struct PM4CmdSetData') + ';\n'
        helpers += '\n' + block(packets, 'struct PM4CmdNop') + ';\n'
        nop_start = driver.index('// Write a special ending NOP packet')
        helpers += driver[nop_start:driver.index('static inline u32* ClearContextState', nop_start)]
        builders = '\n'.join(block(driver, 's32 PS4_SYSV_ABI ' + name + '(')
                             for name in ('sceGnmSetCsShader', 'sceGnmSetPsShader'))
        arrays = '\n'.join('unsigned char code%d[] = {%s};' % (i, ','.join(str(b) for b in code))
                           for i, (code, _) in enumerate(wrappers))
        cpp = r'''
#include <cassert>
#include <array>
#include <cstring>
#include <mutex>
#include <condition_variable>
#include <thread>
#include <cstdio>
#include <sys/mman.h>
#include "common/bit_field.h"
#include "video_core/amdgpu/pm4_opcodes.h"
#include "bbport_pm4_cold_trace.h"
using namespace AmdGpu;
#define LOG_TRACE(...) ((void)0)
#define LOG_ERROR(...) ((void)0)
constexpr s32 ORBIS_OK = 0;
namespace BbGnmHooks {
ProducerContext CurrentProducerContext(const uint32_t*) { return {}; }
DriverWrite::~DriverWrite() {}
}
''' + helpers + builders + arrays + r'''
struct CB {u32* base; u32* end; u32* cursor; void* refill; void* argument;};
static_assert(offsetof(CB,cursor)==0x10);
std::mutex gate;
std::condition_variable cv;
bool entered=false, proceed=false, pause_builder=false;
using Builder = s32(*)(u32*,u32,const u32*);
Builder selected;
u32* observed_cmd;
extern "C" s32 bridge(u32* cmd,u32 size,const u32* regs) {
    observed_cmd=cmd;
    if(pause_builder) {
        std::unique_lock lock(gate);
        entered=true; cv.notify_all();
        cv.wait(lock,[]{return proceed;});
    }
    return selected(cmd,size,regs);
}
using Wrapper=void(*)(CB*,const u32*);
Wrapper load(unsigned char* code,size_t size,size_t call_offset) {
    auto* p=static_cast<unsigned char*>(mmap(nullptr,4096,PROT_READ|PROT_WRITE,
                                           MAP_PRIVATE|MAP_ANONYMOUS,-1,0));
    assert(p!=MAP_FAILED); std::memcpy(p,code,size);
    // Local absolute jump trampoline preserves the original call's return address.
    p[256]=0x48;p[257]=0xb8;
    auto target=reinterpret_cast<uintptr_t>(&bridge);std::memcpy(p+258,&target,8);
    p[266]=0xff;p[267]=0xe0;
    int32_t displacement=256-int32_t(call_offset+5);
    std::memcpy(p+call_offset+1,&displacement,4);
    assert(mprotect(p,4096,PROT_READ|PROT_EXEC)==0);
    return reinterpret_cast<Wrapper>(p);
}
void background(CB& cb) {
    // Forty valid three-DWORD context packets. The sixth is register 0x1e3,
    // matching the residual sequence observed after the CS trailing NOP.
    for(u32 reg=0x1de; reg<0x206; ++reg)
        cb.cursor=PM4CmdSetData::SetContextReg(cb.cursor,reg,0u);
}
void run(Wrapper wrapper, Builder builder, unsigned footprint, int ordering) {
    std::array<u32,512> words;words.fill(0x80000000);
    auto* p=words.data();p=PM4CmdSetData::SetShReg(p,0x216u,0x3fu);
    p=PM4CmdSetData::SetShReg(p,0x217u,0x3fu);
    CB cb{words.data(),words.data()+words.size(),p,nullptr,nullptr};
    std::array<u32,12> regs{};regs[0]=0x10d1b005;regs[2]=0xc0042;regs[3]=0x98;
    regs[4]=0x40;regs[5]=1;regs[6]=1;
    selected=builder;entered=false;proceed=false;pause_builder=ordering==2;
    if(ordering==2) {
        std::thread shader([&]{wrapper(&cb,regs.data());});
        {std::unique_lock lock(gate);cv.wait(lock,[]{return entered;});}
        assert(observed_cmd==words.data()+6 && cb.cursor==words.data()+6);
        background(cb);
        {std::lock_guard lock(gate);proceed=true;}cv.notify_all();shader.join();
        // Original wrapper adds to the *current*, advanced cursor, not cmd.
        assert(cb.cursor==words.data()+6+120+footprint);
        assert(observed_cmd==words.data()+6);
    } else if(ordering==0) {wrapper(&cb,regs.data());background(cb);}
    else {background(cb);wrapper(&cb,regs.data());}
    auto result=BbPm4Cold::Validate(std::span<const u32>(words.data(),cb.cursor));
    if(ordering!=2) assert(result.valid);
    else if(footprint==25) {
        assert(!result.valid && result.offset==31 && result.word==0x1e6);
        assert(words[19]==0xc00a1000 && words[21]==0xc0016900 && words[22]==0x1e3);
        std::printf("actual_CS forced_interleave bad_byte_offset=0x%zx word=%08x cursor_dw=%td\n",
                    result.offset*4,result.word,cb.cursor-words.data());
    } else {
        assert(!result.valid);
        std::printf("actual_PS forced_interleave bad_byte_offset=0x%zx word=%08x\n",result.offset*4,result.word);
    }
}
int main() {
    unsetenv("BB_NR_PM4_COLD_TRACE");
    auto cs=load(code0,sizeof(code0),CALL_CS);
    auto ps=load(code1,sizeof(code1),CALL_PS);
    for(int ordering=0;ordering<3;++ordering) {
        run(cs,sceGnmSetCsShader,25,ordering);
        run(ps,sceGnmSetPsShader,40,ordering);
    }
    puts("both serial orders valid; actual wrappers + actual builders forced interleave invalid");
}
'''
        cpp = cpp.replace('CALL_CS', str(wrappers[0][1])).replace('CALL_PS', str(wrappers[1][1]))
        with tempfile.TemporaryDirectory(prefix='private-pm4-wrapper-') as tmp:
            tmp = Path(tmp)
            (tmp / 'test.cpp').write_text(cpp)
            subprocess.run(['c++', '-std=c++20', '-O2', '-pthread',
                            '-I', str(ROOT / 'gpu/shadps4'), '-I', str(ROOT / 'gpu/shim'),
                            str(tmp / 'test.cpp'), '-o', str(tmp / 'test')], check=True)
            subprocess.run([str(tmp / 'test')], check=True)


if __name__ == '__main__':
    unittest.main()
