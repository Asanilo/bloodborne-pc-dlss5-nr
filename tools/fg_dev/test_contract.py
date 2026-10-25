"""Compile the production admission contract; no Vulkan instance/device or GPU work."""
from pathlib import Path
import subprocess
import tempfile
import unittest


class InputContractTests(unittest.TestCase):
    def test_unbound_first_frame_and_real_source_identity(self):
        root = Path(__file__).resolve().parents[2]
        code = r'''
#include "gpu/upscale/frame_generation_contract.h"
using namespace Vulkan::FgContract;
static_assert(!CanInterpolate(true, true, 0u, 0u, true)); // original crash
static_assert(!CanInterpolate(true, true, 1u, 0u, true));
static_assert(!CanInterpolate(true, true, 0u, 1u, true));
static_assert(!CanInterpolate(true, true, 2u, 1u, true)); // auxiliary LDR
static_assert(!CanInterpolate(true, false, 1u, 1u, true)); // missing scene
static_assert(!CanInterpolate(false, true, 1u, 1u, true)); // live FG off
static_assert(!CanInterpolate(true, true, 1u, 1u, false));
static_assert(CanInterpolate(true, true, 1u, 1u, true));
static_assert(!SameRealSource(0, 0));
static_assert(!SameRealSource(1816, 1817)); // next real source cannot own old FG
static_assert(!SameRealSource(1816, 0)); // missing actual real-content source
static_assert(SameRealSource(1816, 1816));
static_assert(CompatibleGuideDomain(true,true,1816,1817,1506,848,2560,1440,1506,848,2560,1440));
static_assert(CompatibleGuideDomain(true,true,1816,1816,1506,848,2560,1440,1506,848,2560,1440));
static_assert(!CompatibleGuideDomain(false,true,1816,1817,1506,848,2560,1440,1506,848,2560,1440));
static_assert(!CompatibleGuideDomain(true,false,1816,1817,1506,848,2560,1440,1506,848,2560,1440));
static_assert(!CompatibleGuideDomain(true,true,0,1817,1506,848,2560,1440,1506,848,2560,1440));
static_assert(!CompatibleGuideDomain(true,true,1816,1815,1506,848,2560,1440,1506,848,2560,1440));
static_assert(!CompatibleGuideDomain(true,true,1816,1818,1506,848,2560,1440,1506,848,2560,1440));
static_assert(!CompatibleGuideDomain(true,true,1816,1817,1506,848,2560,1440,1706,960,2560,1440));
static_assert(!CompatibleGuideDomain(true,true,1816,1817,1506,848,2560,1440,1506,848,1920,1080));
static_assert(CanStampDisplay(true,true,1u,1u,1816,1816,2560,1440,2560,1440));
static_assert(!CanStampDisplay(true,false,1u,1u,1816,1816,2560,1440,2560,1440)); // not sampled
static_assert(!CanStampDisplay(true,true,2u,1u,1816,1816,2560,1440,2560,1440)); // auxiliary image
static_assert(!CanStampDisplay(true,true,1u,1u,1815,1816,2560,1440,2560,1440)); // stale capture
static_assert(!CanStampDisplay(true,true,1u,1u,0,1816,2560,1440,2560,1440));
static_assert(!CanStampDisplay(true,true,1u,1u,1816,1816,1920,1080,2560,1440));
constexpr bool display_stamps() {
    // No NR job/ledger is needed to bind a verified sampled HDR->LDR source to
    // the actual native VideoOut image; stale/auxiliary destinations still fail.
    const DisplayStamp pending{77,0x4000,200,0,1920,1080};
    auto native=BindDisplay(pending,0x4000,78,202,123,1920,1080);
    if(native.Consume(0x4000,78,203,123,1920,1080)!=77)return false;
    if(native.Consume(0x4000,78,204,123,1920,1080))return false;
    if(BindDisplay(pending,0x4001,78,202,123,1920,1080).source)return false;
    if(BindDisplay(pending,0x4000,79,202,123,1920,1080).source)return false;
    if(BindDisplay(pending,0x4000,78,199,123,1920,1080).source)return false;
    if(BindDisplay(pending,0x4000,78,202,0,1920,1080).source)return false;
    if(BindDisplay(pending,0x4000,78,202,123,1920,1200).source)return false;
    if(BindDisplay({},0x4000,78,202,123,1920,1080).source)return false;
    const DisplayStamp captured{1816, 0x1000, 99, 4, 2560, 1440};
    auto stamp = captured;
    if (stamp.Consume(0x1000, 1817, 99, 4, 2560, 1440) != 1816) return false;
    if (stamp.Consume(0x1000, 1817, 100, 4, 2560, 1440)) return false; // one-shot
    stamp=captured;
    if (stamp.Consume(0x2000, 1817, 100, 4, 2560, 1440)) return false; // wrong VideoOut
    if (stamp.Consume(0x1000, 1817, 100, 4, 2560, 1440)) return false; // rejection consumed
    stamp=captured;
    if (stamp.Consume(0x1000, 1818, 100, 4, 2560, 1440)) return false; // stale real source
    stamp=captured;
    if (stamp.Consume(0x1000, 1816, 100, 4, 2560, 1440)) return false; // before display epoch
    stamp=captured;
    if (stamp.Consume(0x1000, 1817, 98, 4, 2560, 1440)) return false; // before producer
    stamp=captured;
    if (stamp.Consume(0x1000, 1817, 100, 5, 2560, 1440)) return false; // rebuilt image
    stamp=captured;
    if (stamp.Consume(0x1000, 1817, 100, 4, 1920, 1080)) return false; // old output extent
    stamp={};
    return !stamp.Consume(0x1000, 1817, 100, 4, 2560, 1440);
}
static_assert(display_stamps());
int main() {}
'''
        with tempfile.TemporaryDirectory() as directory:
            executable = str(Path(directory) / 'contract')
            subprocess.run(['c++', '-std=c++20', '-Wall', '-Wextra', '-Werror',
                            '-I', str(root), '-x', 'c++', '-', '-o', executable],
                           input=code, text=True, check=True)
            subprocess.run([executable], check=True)


if __name__ == '__main__':
    unittest.main()
