// SPDX-License-Identifier: GPL-2.0-or-later
#include "video_core/renderer_vulkan/native_async_pre_sr_policy.h"
#include "video_core/renderer_vulkan/native_async_pre_sr_wire.h"
#include <iostream>
#include <stdexcept>
using namespace BbNativeAsyncPreSr;
int main(){int checks=0;auto need=[&](bool x){if(!x)throw std::runtime_error("preSR source CPU check failed");++checks;};
 need(RawPacketBytes==18432000&&RawPacketsBytes==92160000);need(BbNativeAsync::NodeCount==5&&BbNativeAsync::MaxLinks==4);
 BbNativeAsync::Chain c;BbNativeAsync::Frame f{.source=100};need(c.Append(f));
 CacheIdentity e{.origin=f.source,.epoch=f.epoch,.history=f.history,.current=f.source};need(CacheLineageMatches(c,e));
 for(uint64_t n=101;n<=104;n++){BbNativeAsync::Frame next{.source=n};need(c.Append(next));e.current=n;need(CacheLineageMatches(c,e));}
 e.current=105;need(!CacheLineageMatches(c,e));e.current=104;e.origin=101;need(!CacheLineageMatches(c,e));e.origin=100;e.history++;need(!CacheLineageMatches(c,e));e.history--;e.job=4;need(!CacheLineageMatches(c,e));e.job=0;e.params=2;need(!CacheLineageMatches(c,e));
 BbNativeAsync::Frame bad{.source=105,.source_edited=true};need(c.Classify(bad)==BbNativeAsync::Chain::Decision::Reject);bad.source_edited=false;bad.source=106;need(c.Classify(bad)==BbNativeAsync::Chain::Decision::Discontinuity);bad.source=105;bad.sr_reset=true;need(c.Classify(bad)==BbNativeAsync::Chain::Decision::Discontinuity);
 SourceLease l;l.native.copy=5;l.sr_read=9;l.native.merge=10;l.CompleteNative(9);need(!l.CanReuse());l.CompleteNative(10);need(l.CanReuse());l.native.peer_read=true;need(!l.CanReuse());l.native.peer_read=false;l.native.unknown=true;need(!l.CanReuse());
 InputDomain d;need(!d.Change(false));need(d.Change(true));need(!d.Change(true));need(d.Change(false));need(!InputDomain::AppliedReset(false,false));need(InputDomain::AppliedReset(false,true));need(InputDomain::AppliedReset(true,false));
 namespace W=Vulkan::NativeAsyncPreSrWire;auto cfg=W::config_expected("cpu-source-only","resident","0123456789abcdef0123456789abcdef","0123456789abcdef0123456789abcdef");W::config(cfg,"cpu-source-only","resident");need(cfg["width"]==1280&&cfg["height"]==720&&cfg["native_soft_deadline_ms"]==500);need(W::spec()["quality_profile"]["color_width"]==1280&&W::spec()["quality_profile"]["sr_output_width"]==1920);need(W::spec()["quality_profile"]["route"]=="async_pre_sr_nr_origin_to_native_cache_then_dlss_sr");auto badcfg=cfg;badcfg["native_soft_deadline_ms"]=true;bool rejected=false;try{W::config(badcfg,"cpu-source-only","resident");}catch(...){rejected=true;}need(rejected);
 setenv("BB_NR_NATIVE_ASYNC_PRE_SR","1",1);setenv("BB_NR_NATIVE_ASYNC_MODE","resident",1);setenv("BB_NR_NATIVE_ASYNC_SESSION","cpu-source-only",1);need(Enabled());setenv("BB_NR_NATIVE_ASYNC","1",1);need(!Enabled());unsetenv("BB_NR_NATIVE_ASYNC");setenv("BB_NR_NATIVE_ASYNC_PRE_SR","2",1);need(!Enabled());setenv("BB_NR_NATIVE_ASYNC_PRE_SR","1",1);setenv("BB_NR_NATIVE_ASYNC_SESSION","bad/session",1);need(!Enabled());
 std::cout<<"{\"status\":\"PASS_CPU_ONLY_LINEAGE_SOURCE_LEASE\",\"checks\":"<<checks<<",\"GPU\":\"NOT_RUN\",\"model_cache\":\"NOT_EXERCISED_CPU\"}\n";}
