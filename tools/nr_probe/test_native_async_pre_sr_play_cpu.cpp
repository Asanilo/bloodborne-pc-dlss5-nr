// SPDX-License-Identifier: GPL-2.0-or-later
#include "video_core/renderer_vulkan/native_async_pre_sr_play_wire.h"
#include "video_core/renderer_vulkan/native_async_pre_sr_diagnostic_policy.h"
#include <iostream>
namespace W=Vulkan::NativeAsyncPreSrPlayWire;namespace P=BbNativeAsyncPreSrPlay;
int main(){unsigned checks=0;auto need=[&](bool ok){if(!ok)throw std::runtime_error("continuous preSR CPU check failed");++checks;};
 unsetenv("BB_NR_NATIVE_ASYNC_PRE_SR_PLAY");need(P::MaxJobs()==4&&P::SourceLimit()==120&&P::ServiceSeconds()==300&&P::BucketCount()==1);
 need(W::spec()==Vulkan::NativeAsyncPreSrWire::spec());bool denied=false;try{W::job_path("root","input","cpu",4);}catch(...){denied=true;}need(denied);
 setenv("BB_NR_NATIVE_ASYNC_PRE_SR","1",1);setenv("BB_NR_NATIVE_ASYNC_PRE_SR_PLAY","1",1);setenv("BB_NR_NATIVE_ASYNC_MODE","resident",1);setenv("BB_NR_NATIVE_ASYNC_SESSION","cpu",1);
 need(P::Enabled()&&P::MaxJobs()==16384&&P::SourceLimit()==36000&&P::ServiceSeconds()==600&&P::BucketCount()==256);
 need(W::spec()["profile_id"]=="async-pre-sr-continuous-hdr720-j1-experimental-v1");
 auto cfg=W::config_expected("cpu","resident","0123456789abcdef0123456789abcdef","0123456789abcdef0123456789abcdef");W::config(W::parse(cfg.dump()),"cpu","resident");need(cfg["max_jobs"]==16384&&cfg["service_wall_seconds"]==600&&cfg["width"]==1280);
 auto bad=cfg;bad["max_jobs"]=4u;denied=false;try{W::config(bad,"cpu","resident");}catch(...){denied=true;}need(denied);
 for(uint64_t n:{0u,63u,64u,127u,128u,16383u}){
  const auto b=std::to_string(n/64);auto path=W::job_path("root","complete","cpu",n);need(path=="root/frames/bucket-"+std::string(6-b.size(),'0')+b+"/async-complete-cpu-g0-f"+std::to_string(n)+".json");
 }
 denied=false;try{W::job_path("root","input","cpu",16384);}catch(...){denied=true;}need(denied);
 W::Counts counts;need(counts.fields()["source_frame_valid"]==false);counts.check(W::parse(counts.fields().dump()));
 for(uint64_t n=0;n<67;++n){counts.finalize(W::parse(W::J{{"job",n},{"origin_source",100u+n},{"origin_epoch",1u},{"origin_history",1u}}.dump()),"expected_skip");}
 need(counts.finalized==67&&counts.skip==67);counts.check(W::parse(counts.fields().dump()));
 counts.finalized=16384;counts.skip=16384;counts.check(W::parse(counts.fields().dump()));need(true);
 counts.finalized=16385;counts.skip=16385;denied=false;try{counts.check(W::parse(counts.fields().dump()));}catch(...){denied=true;}need(denied);
 BbNativeAsync::Chain chain;BbNativeAsync::Frame raw{.source=100};need(chain.Append(raw));need(BbNativeAsyncPreSr::CacheLineageMatches(chain,{16383,100,raw.epoch,raw.history,1,100}));need(!BbNativeAsyncPreSr::CacheLineageMatches(chain,{16384,100,raw.epoch,raw.history,1,100}));
 setenv("BB_NR_NATIVE_ASYNC_PRE_SR_DIAGNOSTIC_ONCE","1",1);need(!BbNativeAsyncPreSr::DiagnosticEnabled());
 setenv("BB_NR_NATIVE_ASYNC_PRE_SR_PLAY","01",1);need(!P::Enabled()&&P::MaxJobs()==4);setenv("BB_NR_NATIVE_ASYNC_PRE_SR_PLAY","1",1);setenv("BB_NR_NATIVE_ASYNC_MODE","bad",1);need(!P::Enabled());
 std::cout<<"{\"status\":\"PASS_CPU_PRIVATE_CONTINUOUS_LIMITS_TYPES_BUCKETS_ACTUALN_LEGACY_ONLY\",\"checks\":"<<checks<<",\"GPU\":\"NOT_RUN\"}\n";
}
