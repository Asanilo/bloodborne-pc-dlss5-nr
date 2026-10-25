// SPDX-License-Identifier: GPL-2.0-or-later
#include "render/native_async_pre_sr_policy.h"
#include <cassert>
#include <cstdio>

int main() {
 using namespace BbNativeAsyncPreSr;
 unsetenv("BB_NR_DAILY");
 setenv("BB_NR_NATIVE_ASYNC_PRE_SR_PLAY","1",1);
 setenv("BB_NR_NATIVE_ASYNC_PRE_SR","1",1);
 setenv("BB_NR_NATIVE_ASYNC_MODE","resident",1);
 // A finite diagnostic must still shut down on an unsupported profile.
 assert(ProfileRequiresStop(false,true));
 setenv("BB_NR_DAILY","1",1);
 assert(RetainUnavailableSession());
 assert(!ProfileRequiresStop(false,true));
 assert(!NeedsStop(false,true,false,false));
 assert(!NeedsStop(true,true,false,false));
 assert(NeedsStop(false,true,true,false));
 assert(NeedsStop(false,true,false,true));
 // Suspending the session cannot release an outstanding peer/GPU/unknown use.
 SourceLease lease;
 lease.native.copy=10;lease.sr_read=12;lease.native.peer_read=true;
 lease.CompleteNative(12);
 assert(!lease.CanReuse());
 lease.native.peer_read=false;lease.native.unknown=true;
 assert(!lease.CanReuse());
 lease.native.unknown=false;lease.sr_read=13;
 assert(!lease.CanReuse());
 lease.CompleteNative(13);assert(lease.CanReuse());
 // Returning after a skipped source must use the existing discontinuity path.
 BbNativeAsync::Chain chain;
 BbNativeAsync::Frame origin{.source=100};assert(chain.Append(origin));
 assert(chain.Classify({.source=103})==BbNativeAsync::Chain::Decision::Discontinuity);
 chain.Restart(true);BbNativeAsync::Frame resumed{.source=103};assert(chain.Append(resumed));
 assert(!chain.CompleteTo(100,103,1,1));
 // Daily alone must not relax unsupported-profile handling for finite mode.
 unsetenv("BB_NR_NATIVE_ASYNC_PRE_SR_PLAY");
 assert(!RetainUnavailableSession());assert(ProfileRequiresStop(false,true));
 std::puts("PASS daily preSR profile pause: CPU policy/lease/lineage only; NOT_GPU_PROOF");
}
