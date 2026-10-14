// SPDX-License-Identifier: GPL-2.0-or-later
#include "video_core/renderer_vulkan/native_pre_sr_sky_policy.h"
#include <cstdio>
#include <stdexcept>
int main(){using namespace BbNativePreSrSkyPolicy;unsigned n=0;const auto c=[&](bool v){++n;if(!v)throw std::runtime_error("Native mask policy failed");};
 c(Parse(nullptr)==Mode::Off);c(Parse("off")==Mode::Off);c(Parse("low_temporal")==Mode::LowTemporal);c(Parse("regional_antipop")==Mode::RegionalAntiPop);c(Parse("OFF")==Mode::Invalid);c(Parse("")==Mode::Invalid);c(Parse("off ")==Mode::Invalid);
 for(unsigned finite=0;finite<2;++finite)for(unsigned whole=0;whole<2;++whole)for(unsigned ctx=0;ctx<2;++ctx)c(MaskValue(finite,whole,ctx)==(finite&&whole?(1|(ctx?2:0)):0));
 c(MaskCount==2&&MaskBytes==4);
 BbNativeAsync::Frame old,cur;old.source=103;old.epoch=cur.epoch=1;old.history=cur.history=1;cur.source=104;
 c(PreviousLegal(true,true,old,cur,100));c(!PreviousLegal(true,false,old,cur,100));c(!PreviousLegal(false,true,old,cur,100));cur.source=105;c(!PreviousLegal(true,true,old,cur,100));cur.source=104;cur.epoch=2;c(!PreviousLegal(true,true,old,cur,100));cur.epoch=1;cur.history=2;c(!PreviousLegal(true,true,old,cur,100));cur.history=1;c(!PreviousLegal(true,true,old,cur,99));c(!PreviousLegal(true,true,old,cur,0));
 FrameClock clock;c(clock.Update(0)==0);c(clock.Update(-1)==0);c(clock.Update(.01)>0);c(std::abs(clock.smoothed-.016)<1e-10);
 std::printf("{\"status\":\"PASS_NATIVE_SELECTOR_MASK_LITERAL_CPU\",\"checks\":%u,\"GPU\":\"NOT_RUN\"}\n",n);
}
