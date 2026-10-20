// SPDX-License-Identifier: GPL-2.0-or-later
// CPU-only: the actual upstream decoder must reject malformed register packets atomically.
#include "cp/cp_decoder.h"
#include "gcn/pm4_register_guard.h"
#include "cp/submission_watermark.h"
#include <algorithm>
#include <cassert>
#include <cstdio>
#include <vector>
using namespace BbCp;
namespace Guard = AmdGpu::Pm4RegisterGuard;
static u32 header(Op op, u32 body) { return 0xc0000000u | ((body-1)<<16) | (u32(op)<<8); }
struct SinkProbe : Sink {
    unsigned bad{}, packets{};
    void OnBadPacket(std::span<const u32>, const char*, int) override { ++bad; }
    void OnPacket(const Packet&) override { ++packets; }
};
struct Observer : RegisterObserver {
    unsigned calls{};
    void OnRegisterPacket(std::span<const u32>, std::span<const RegisterWrite>, bool) override {
        ++calls;
    }
};
static void rejected(std::vector<u32> words) {
    Decoder decoder;
    Observer observer;
    SinkProbe sink;
    decoder.SetObserver(&observer);
    for (u32 i=0;i<Bank::End;++i) decoder.Registers().Write(i, i ^ 0xa55aa55au);
    const auto span=decoder.Registers().Words();
    const std::vector<u32> before(span.begin(),span.end());
    decoder.Decode(words,sink);
    assert(sink.bad==1 && sink.packets==0 && observer.calls==0);
    assert(std::equal(before.begin(),before.end(),span.begin()));
}
int main() {
    struct BankCase { Op op; u32 base, count; };
    for (const auto b : {BankCase{Op::SetConfigReg,Bank::Config,0xc00},
                         BankCase{Op::SetShReg,Bank::Sh,0x400},
                         BankCase{Op::SetContextReg,Bank::Context,0x400},
                         BankCase{Op::SetUconfigReg,Bank::Uconfig,0x400}}) {
        rejected({header(b.op,1),0});                       // missing value
        rejected({header(b.op,3),0,1});                     // actual extent too short
        rejected({header(b.op,2),b.count,1});               // starts at bank end
        rejected({header(b.op,3),b.count-1,1,2});           // crosses bank
        rejected({header(b.op,2),0x10000,1});              // reserved offset bits
        rejected({0xffff0000u | (u32(b.op)<<8),0,1});       // 0x3fff count must not wrap
        Decoder decoder; SinkProbe sink; Observer observer; decoder.SetObserver(&observer);
        u32 index=b.count-1;
        if(b.op==Op::SetContextReg || b.op==Op::SetUconfigReg) index|=0xf0000000u;
        const std::vector<u32> valid{header(b.op,2),index,0x12345678};
        decoder.Decode(valid,sink);
        assert(!sink.bad && sink.packets==1 && observer.calls==1);
        assert(decoder.Registers()[b.base+b.count-1]==0x12345678);
    }
    // The compute program is smaller than the SH bank: both start and end matter.
    for (u32 offset=0x1ff;offset<=0x211;++offset)
        for (unsigned values=1;values<=18;++values)
            assert(Guard::FitsWindow(offset,values,0x200,16)==
                   (offset>=0x200 && offset<0x210 && values<=0x210-offset));
    SubmissionWatermark<3> frame{{1,1,0},77};
    assert(!frame.IsDecoded({0,2,20})); // total count is enough, but queue 0 is still waiting
    assert(!frame.IsDecoded({1,0,20}));
    assert(frame.IsDecoded({1,1,0}));
    assert(frame.IsDecoded({4,3,9}));
    std::puts("PASS: actual decoder rejects truncated/reserved/cross-bank writes without observer mutation; compute window; per-queue frame prefix");
}
