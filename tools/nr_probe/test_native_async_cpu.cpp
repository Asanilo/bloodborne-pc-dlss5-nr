// SPDX-License-Identifier: GPL-2.0-or-later
#include "video_core/renderer_vulkan/native_async_owner.h"
#include "video_core/renderer_vulkan/native_origin_lease.h"
#include <chrono>
#include <bit>
#include <cstdio>
#include <cstdlib>
#include <limits>
#include <memory>
#include <thread>
using namespace BbNativeAsync;
static unsigned checks=0;
void check(bool ok){++checks;if(!ok)std::abort();}
int main(){
 for(unsigned bits=0;bits<16;++bits){bool supported=bits&1,snapshots=bits&2,exit=bits&4,rejected=bits&8;check(NeedsHold(supported,snapshots,exit,rejected)==(exit||rejected||(!supported&&snapshots)));}
 Chain chain;Frame f{.source=10,.copy_tick=25,.sr_reset=true};
 check(chain.Classify(f)==Chain::Decision::Origin);check(chain.Append(f));check(f.previous==0&&f.epoch==1&&f.history==1);
 for(unsigned i=1;i<=MaxLinks;++i){Frame n{.source=10+i,.copy_tick=25+i};check(chain.Classify(n)==Chain::Decision::Link);check(chain.Append(n));check(n.previous==9+i);}
 check(chain.CompleteTo(10,14,1,1));check(!chain.CompleteTo(10,15,1,1));check(!chain.CompleteTo(10,14,2,1));check(!chain.CompleteTo(10,14,1,2));
 Frame edited{.source=15,.source_edited=true};check(!Finite(edited));
 Frame next{.source=15,.copy_tick=30};check(chain.Size()==5);check(chain.Classify(next)==Chain::Decision::Full);check(!chain.Append(next));
 Frame duplicate{.source=14};check(chain.Classify(duplicate)==Chain::Decision::Reject);
 chain.Restart(false);check(chain.Append(next));check(next.epoch==2&&next.history==1&&next.previous==14);
 Frame gap{.source=17};check(chain.Classify(gap)==Chain::Decision::Discontinuity);check(!chain.Append(gap));
 chain.Restart(true);check(chain.Append(gap));check(gap.history==2);
 Frame reset{.source=18,.sr_reset=true};check(chain.Classify(reset)==Chain::Decision::Discontinuity);
 Frame invalid{.source=19};invalid.viewport[0]=std::numeric_limits<float>::infinity();check(chain.Classify(invalid)==Chain::Decision::Reject);
 invalid.viewport[0]=std::numeric_limits<float>::quiet_NaN();check(!Finite(invalid));
 CompletionNotice notice{.origin=f.source,.epoch=f.epoch,.history=f.history,.native_copy_observed_tick=25,.real_gpu_completed=true};check(NoticeMatches(f,notice));
 for(unsigned i=0;i<5;++i){auto bad=notice;if(i==0)bad.origin++;if(i==1)bad.epoch++;if(i==2)bad.history++;if(i==3)bad.native_copy_observed_tick--;if(i==4)bad.real_gpu_completed=false;check(!NoticeMatches(f,bad));}
 for(unsigned round=0;round<50;++round){
  auto owner=std::make_shared<Owner>();auto node=std::make_shared<Node>(f);bool posted=false;
  const auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(2);
  while(!posted&&std::chrono::steady_clock::now()<deadline){posted=owner->TryPost(node);std::this_thread::yield();}
  check(posted);check(owner->ReadyCount()==0);
  std::thread completion([node,owner]{node->gpu_ready.store(true,std::memory_order_release);owner->Signal();});
  while(owner->ReadyCount()!=1&&std::chrono::steady_clock::now()<deadline)std::this_thread::yield();
  completion.join();check(owner->ReadyCount()==1&&owner->LatestReadySource()==10);
  bool seen=false;while(!seen&&std::chrono::steady_clock::now()<deadline){seen=owner->TryObserve(f,notice);std::this_thread::yield();}
  check(seen);check(owner->ObservedNoticeCount()==1);
  owner->Stop();check(!owner->TryPost(node));check(!owner->TryObserve(f,notice));
 }
 for(unsigned bits=0;bits<65536;bits++){
  if((bits&0x7c00)==0x7c00)continue; // finite only, exact raw r2 identity domain
  const auto half=std::bit_cast<_Float16>(static_cast<uint16_t>(bits));
  const float wide=static_cast<float>(half);const auto back=static_cast<_Float16>(wide);
  check(std::bit_cast<uint16_t>(back)==bits);
 }
 OriginLease lease;check(!lease.CanReuse());lease.copy=20;lease.CompleteNative(20);check(lease.CanReuse());
 lease.peer_read=true;check(!lease.CanReuse());lease.peer_read=false;
 lease.capture=21;check(!lease.CanReuse());lease.CompleteNative(21);check(lease.CanReuse());
 lease.catchup=22;lease.merge=24;lease.CompleteNative(23);check(!lease.CanReuse());lease.CompleteNative(24);check(lease.CanReuse());
 lease.unknown=true;lease.CompleteNative(999);check(!lease.CanReuse());lease.unknown=false;
 lease.CompleteNative(1);check(lease.observed.load()==999);check(lease.LastUse()==24);
 // A recorded same-queue producer permits recording input, never publication.
 Frame raw{.source=100,.epoch=3,.history=2,.copy_tick=44};
 RecordedOrigin recorded{100,3,2,1,44,44,1000};check(recorded.Matches(raw,1000,44));
 check(recorded.Matches(raw,1000,45));check(!recorded.Matches(raw,1001,44));check(!recorded.Matches(raw,1000,43));
 for(unsigned i=0;i<7;++i){auto bad=recorded;
  if(i==0){bad.source++;}
  if(i==1){bad.epoch++;}
  if(i==2){bad.history++;}
  if(i==3){bad.params++;}
  if(i==4){bad.copy_tick=0;}
  if(i==5){bad.conversion_tick=0;}
  if(i==6){bad.scheduler_identity=0;}
  check(!bad.Matches(raw,1000,44));}
 raw.source_edited=true;check(!recorded.Matches(raw,1000,44));
 // Shared ownership does NOT authorize mutation; actual per-domain lease does.
 auto owned=std::make_shared<OriginLease>();auto alias=owned;owned->copy=10;check(!alias->CanReuse());
 check(!ExitRequested());RequestExit();check(ExitRequested()&&!ExitComplete());PublishExitComplete();check(ExitComplete());
 std::printf("{\"status\":\"PASS\",\"checks\":%u,\"thread_rounds\":50,\"scope\":\"CPU numeric model/publication/lifetime only; mocked readiness NOT_GPU_PROOF\"}\n",checks);
}
