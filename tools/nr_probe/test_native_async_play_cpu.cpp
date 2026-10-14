// SPDX-License-Identifier: GPL-2.0-or-later
#include "video_core/renderer_vulkan/native_origin_play_wire.h"
#include "video_core/renderer_vulkan/native_origin_wire.h"
#include "video_core/renderer_vulkan/native_async_state.h"
#include <cstdio>
#include <cstdlib>
#include <stdexcept>
namespace P=Vulkan::NativeOriginPlayWire;namespace L=Vulkan::NativeOriginWire;
unsigned checks=0;
void check(bool ok){++checks;if(!ok)throw std::runtime_error("play CPU check failed");}
template<class F>void rejects(F f){bool bad=false;try{f();}catch(...){bad=true;}check(bad);}
int main(){
 unsetenv("BB_NR_NATIVE_ASYNC_PLAY");setenv("BB_NR_NATIVE_ASYNC_MODE","resident",1);
 check(!BbNativeAsyncPlay::Enabled()&&BbNativeAsyncPlay::MaxJobs()==4&&BbNativeAsyncPlay::SourceLimit()==120);
 check(P::common("cpu-play","resident")==L::common("cpu-play","resident"));
 check(P::job_path("/tmp/cpu-play","input","cpu-play",3)==L::job_path("/tmp/cpu-play","input","cpu-play",3));
 rejects([]{L::job_path("/tmp/cpu-play","input","cpu-play",4);});
 setenv("BB_NR_NATIVE_ASYNC_PLAY","1",1);
 check(BbNativeAsyncPlay::Enabled()&&BbNativeAsyncPlay::MaxJobs()==16384&&BbNativeAsyncPlay::SourceLimit()==36000&&BbNativeAsyncPlay::ServiceSeconds()==600&&BbNativeAsyncPlay::BucketCount()==256);
 check(BbNativeAsync::NodeCount==5&&BbNativeAsync::MaxLinks==4);
 const std::string uuid(32,'a');auto config=P::parse(P::config_expected("cpu-play","resident",uuid,uuid).dump());P::config(config,"cpu-play","resident");check(P::u(config,"max_jobs")==16384&&P::u(config,"service_wall_seconds")==600);
 check(P::s(config,"profile_id")=="native-async-continuous-origin720-current-sr1080-experimental-v1");
 for(uint64_t j:{0u,63u,64u,127u,128u,16383u}){
  const auto n=std::to_string(j/64);check(P::job_path("/tmp/cpu-play","input","cpu-play",j)=="/tmp/cpu-play/frames/bucket-"+std::string(6-n.size(),'0')+n+"/async-input-cpu-play-g0-f"+std::to_string(j)+".json");
 }
 rejects([]{P::job_path("/tmp/cpu-play","input","cpu-play",16384);});
 auto job=P::parse("{\"job\":16383,\"origin_source\":8,\"origin_epoch\":1,\"origin_history\":1}");check(P::u(P::job_fields(job),"job")==16383);
 for(const char*x:{"true","-1","16384","1.0"}){
  auto bad=P::parse(std::string("{\"job\":")+x+",\"origin_source\":8,\"origin_epoch\":1,\"origin_history\":1}");rejects([&]{P::job_fields(bad);});
 }
 P::Counts counts;counts.check(P::parse(counts.fields().dump()));check(counts.finalized==0);
 counts.finalized=16383;counts.captured=16383;job["job"]=uint64_t(16383);counts.finalize(job,"captured");counts.check(P::parse(counts.fields().dump()));check(counts.finalized==16384&&counts.captured==16384);
 rejects([&]{counts.finalize(job,"captured");});
 auto bad=config;bad["max_jobs"]=4u;rejects([&]{P::config(P::parse(bad.dump()),"cpu-play","resident");});
 bad=config;bad["profile_id"]="old";rejects([&]{P::config(bad,"cpu-play","resident");});
 bad=config;bad["max_jobs"]=true;rejects([&]{P::config(bad,"cpu-play","resident");});
 // Root's old validators stay strict even while the private new route is enabled.
 rejects([&]{L::config(config,"cpu-play","resident");});rejects([&]{L::job_fields(job);});
 std::printf("{\"status\":\"PASS\",\"checks\":%u,\"scope\":\"CPU_private_exact_limits_types_buckets_N_and_frozen_legacy_only\",\"GPU\":\"NOT_RUN\"}\n",checks);
}
