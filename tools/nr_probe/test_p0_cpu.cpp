// SPDX-License-Identifier: GPL-2.0-or-later
#include "video_core/renderer_vulkan/p0_wire.h"
#include "video_core/renderer_vulkan/p0_status.h"
#include "video_core/renderer_vulkan/p0_wait_budget.h"
#include "video_core/renderer_vulkan/p0_timing_math.h"
#include <atomic>
#include <cassert>
#include <iostream>
#include <fstream>
#include <iterator>
#include <limits>
#include <thread>
using namespace Vulkan;
int main(int argc,char**argv) {
 unsigned checks=0;
 auto require=[&](bool b){if(!b)throw std::runtime_error("P0 CPU check failed");++checks;};
 auto rejects=[&](auto f){try{f();}catch(const std::exception&){++checks;return;}throw std::runtime_error("P0 must reject");};
 // Production wait arithmetic/result classification only; synthetic tick/API values
 // are NOT execution of vkWaitSemaphores or a real GPU completion test.
 using BK=P0Wait::BudgetKind;using WO=P0Wait::Outcome;
 require(P0Wait::Remaining(100,100).kind==BK::Expired);
 require(P0Wait::Remaining(99,100).kind==BK::Expired);
 const auto one_ns=P0Wait::Remaining(101,100);require(one_ns.kind==BK::Finite&&one_ns.nanoseconds==1);
 require(P0Wait::Remaining(500000000,0).nanoseconds==500000000);
 require(P0Wait::Remaining(0,1,true).kind==BK::CompletedFast);
 require(P0Wait::Remaining(0,-1).nanoseconds==1);
 const auto largest=P0Wait::Remaining(INT64_MAX,0);require(largest.kind==BK::Finite&&largest.nanoseconds==uint64_t(INT64_MAX)&&largest.nanoseconds!=UINT64_MAX);
 require(P0Wait::Remaining(INT64_MAX,-1).kind==BK::InvalidRange);
 require(P0Wait::Remaining(INT64_MAX,INT64_MIN).kind==BK::InvalidRange);
 require(P0Wait::Remaining(INT64_MIN,INT64_MAX).kind==BK::Expired);
 require(P0Wait::Classify(0,true,true)==WO::Completed);
 require(P0Wait::Classify(0,true,false)==WO::Expired);
 require(P0Wait::Classify(0,false,true)==WO::InvalidSuccess);
 require(P0Wait::Classify(0,false,false)==WO::InvalidSuccess);
 require(P0Wait::Classify(2,false,false)==WO::Expired);
 require(P0Wait::Classify(2,true,true)==WO::Expired);
 require(P0Wait::Classify(-4,false,true)==WO::Error); // DEVICE_LOST
 require(P0Wait::Classify(-4,true,true)==WO::Error); // cached tick cannot mask API error
 require(P0Wait::Classify(-1,false,true)==WO::Error);
 require(P0Wait::Classify(1,true,true)==WO::Error); // unexpected NOT_READY, not completion
 const auto point=std::chrono::steady_clock::time_point{std::chrono::nanoseconds{123}};
 require(P0Wait::EpochNs(point)==123);
 const auto now=std::chrono::steady_clock::now();require(P0Wait::Remaining(P0Wait::EpochNs(now),P0Wait::EpochNs(now)).kind==BK::Expired);
 // Production timestamp arithmetic and finite-slot reuse decision, no actual queries/GPU.
 P0Timing::Slots timing_slots;
 for(unsigned i=0;i<4;++i) require(timing_slots.Claim()==i);
 require(!timing_slots.Claim());require(!timing_slots.Complete(0,false));require(!timing_slots.Claim());
 require(!timing_slots.Complete(4,true));require(timing_slots.Complete(0,true));
 require(!timing_slots.Complete(0,true));require(timing_slots.Claim()==0);
 const std::array<uint64_t,4> q{100,1,150,1};
 require(P0Timing::DurationMs(0,q,64,1000,true)==.05);
 require(P0Timing::DurationMs(0,{250,1,5,1},8,1000,true)==.011);
 require(P0Timing::DurationMs(0,{UINT64_MAX-4,1,5,1},64,1000,true)==.010);
 require(!P0Timing::DurationMs(1,q,64,1,true));require(!P0Timing::DurationMs(-4,q,64,1,true));
 require(!P0Timing::DurationMs(0,q,64,1,false));
 require(!P0Timing::DurationMs(0,{100,0,150,1},64,1,true));
 require(!P0Timing::DurationMs(0,{100,1,150,0},64,1,true));
 require(!P0Timing::DurationMs(0,q,0,1,true));require(!P0Timing::DurationMs(0,q,65,1,true));
 require(!P0Timing::DurationMs(0,q,64,0,true));require(!P0Timing::DurationMs(0,q,64,-1,true));
 require(!P0Timing::DurationMs(0,q,64,std::numeric_limits<double>::infinity(),true));
 require(!P0Timing::DurationMs(0,q,64,std::numeric_limits<double>::quiet_NaN(),true));
 require(P0Timing::PublishOrder(1,2,3,4));require(P0Timing::PublishOrder(1,1,1,1));
 require(!P0Timing::PublishOrder(2,1,3,4));require(!P0Timing::PublishOrder(1,3,2,4));
 require(!P0Timing::PublishOrder(1,2,4,3));
 BbP0::PublishSrTiming({true,9,18,.25,"available"});
 BbP0::PublishSrTiming({true,8,16,.5,"older"});require(BbP0::ReadSrTiming().source_game_frame==9);
 BbP0::PublishSrTiming({true,10,0,{},"pending_slots"});require(!BbP0::ReadSrTiming().commands_gpu_ms);
 // The exact production Present telemetry predicate, every Boolean combination.
 for(unsigned bits=0;bits<32;++bits) {
  const bool game=bits&1,reuse=bits&2,resident=bits&4,known=bits&8,selected=bits&16;
  require(P0Timing::BaseSrPresent(game,reuse,resident,known,selected)==
      (game&&!reuse&&resident&&known&&!selected));
 }
 auto profile=P0Wire::Parse(P0Wire::Profile().dump(),"own wire");P0Wire::ValidateProfile(profile);++checks;
 require(profile["passthrough"].is_boolean()&&!profile["passthrough"].get<bool>());
 require(profile["nr_mvec_scale_status"]=="EXPERIMENTAL_UNVERIFIED");
 for(auto n:{4u,120u,1800u,18000u}) require(P0Wire::Allowed("resident",n,"hot"));
 require(P0Wire::Allowed("skipEvaluate",4,"diagnostic"));require(!P0Wire::Allowed("skipEvaluate",120,"hot"));
 require(!P0Wire::Allowed("resident",120,"diagnostic"));require(!P0Wire::Allowed("noop",4,"hot"));
 rejects([&]{P0Wire::Parse("{\"session\":\"a\",\"session\":\"b\"}","duplicate");});
 rejects([&]{P0Wire::Parse("{\"x\":1e9999}","nonfinite");});
 rejects([&]{P0Wire::Parse(std::string(65537,' '),"oversize");});
 rejects([&]{P0Wire::Uint(P0Wire::Parse("{\"epoch\":1.5}","fraction"),"epoch");});
 rejects([&]{P0Wire::Uint(P0Wire::Parse("{\"epoch\":-1}","negative"),"epoch");});
 profile["passthrough"]=0u;rejects([&]{P0Wire::ValidateProfile(profile);});
 for(auto f:{0u,63u,64u,127u,128u,17999u}) {
  const auto d=P0Wire::FrameDir("/private",f,18000);
  char expected[80];std::snprintf(expected,sizeof(expected),"/private/frames/bucket-%06u",f/64);require(d==expected);
 }
 rejects([&]{P0Wire::FrameDir("/private",18000,18000);});
 rejects([&]{P0Wire::FramePath("/private",0,4,"../escape");});
 require(P0Wire::ParameterApplication().size()==4);
 P0Wire::Json diagnostic{{"helper_pixel_readback_bytes",94003200u},{"gpu_validator_readback_bytes",64u},
  {"audit_readback_bytes",94003264u},{"dump_count",5u},
  {"guide_validation",{{"depth",{{"bytes",3686400u}}},{"mv",{{"bytes",7372800u}}}}}};
 P0Wire::ValidateDiagnosticReadbackAccounting(diagnostic);require(!diagnostic.contains("guide_readback_bytes"));
 for(const char*key:{"helper_pixel_readback_bytes","gpu_validator_readback_bytes","audit_readback_bytes","dump_count"}) {
  auto wrong=diagnostic;wrong[key]=0u;rejects([&]{P0Wire::ValidateDiagnosticReadbackAccounting(wrong);});
  wrong=diagnostic;wrong[key]=1.5;rejects([&]{P0Wire::ValidateDiagnosticReadbackAccounting(wrong);});
  wrong=diagnostic;wrong.erase(key);rejects([&]{P0Wire::ValidateDiagnosticReadbackAccounting(wrong);});
 }
 for(const char*role:{"depth","mv"}) {
  auto wrong=diagnostic;wrong["guide_validation"][role]["bytes"]=0u;rejects([&]{P0Wire::ValidateDiagnosticReadbackAccounting(wrong);});
  wrong=diagnostic;wrong["guide_validation"].erase(role);rejects([&]{P0Wire::ValidateDiagnosticReadbackAccounting(wrong);});
 }
 if(argc==2) {
  std::ifstream in(argv[1],std::ios::binary);const std::string text{std::istreambuf_iterator<char>(in),{}};
  const auto actual=P0Wire::Parse(text,"actual frozen Helper complete");
  P0Wire::ValidateDiagnosticReadbackAccounting(actual);require(!actual.contains("guide_readback_bytes"));
 } else if(argc!=1&&argc!=3) throw std::runtime_error("optional actual complete or --hot-config PATH only");
 if(argc==3) {
  require(std::string(argv[1])=="--hot-config");
  std::ifstream in(argv[2],std::ios::binary);const std::string text{std::istreambuf_iterator<char>(in),{}};
  const auto config=P0Wire::Parse(text,"actual frozen hot game-config");
  require(config.at("execution_mode")=="hot"&&config.at("mode")=="resident");
  const auto plan=P0Wire::SelectBridgeResources(config.at("mode").get<std::string>(),
      unsigned(P0Wire::Uint(config,"frame_count")),config.at("execution_mode").get<std::string>(),
      unsigned(P0Wire::Uint(config,"width")),unsigned(P0Wire::Uint(config,"height")));
  require(!plan.diagnostic&&!plan.full_pixel_buffer_bytes&&!plan.guide_buffer_bytes&&
      !plan.validator_bytes&&!plan.return_validator_bytes&&plan.guard_bytes==4);
 }
 for(auto n:{4u,120u,1800u,18000u}) {
  const auto hot=P0Wire::SelectBridgeResources("resident",n,"hot",1920,1080);
  require(!hot.diagnostic&&!hot.full_pixel_buffer_bytes&&!hot.guide_buffer_bytes&&
      !hot.validator_bytes&&!hot.return_validator_bytes&&hot.guard_bytes==4);
 }
 for(const char*mode:{"resident","skipEvaluate"}) {
  const auto full=P0Wire::SelectBridgeResources(mode,4,"diagnostic",1920,1080);
  require(full.diagnostic&&full.full_pixel_buffer_bytes==99532800&&full.guide_buffer_bytes==18432000&&
      full.validator_bytes==64&&full.return_validator_bytes==16&&full.guard_bytes==4);
 }
 rejects([&]{P0Wire::SelectBridgeResources("resident",4,"bogus",1920,1080);});
 rejects([&]{P0Wire::SelectBridgeResources("noop",4,"hot",1920,1080);});
 rejects([&]{P0Wire::SelectBridgeResources("single",4,"diagnostic",1920,1080);});
 rejects([&]{P0Wire::SelectBridgeResources("srOnly",4,"hot",1920,1080);});
 rejects([&]{P0Wire::SelectBridgeResources("skipEvaluate",4,"hot",1920,1080);});
 rejects([&]{P0Wire::SelectBridgeResources("resident",120,"diagnostic",1920,1080);});
 rejects([&]{P0Wire::SelectBridgeResources("resident",1,"hot",1920,1080);});
 rejects([&]{P0Wire::SelectBridgeResources("resident",4,"hot",1280,720);});
 BbP0::InitializeRequest(true,false);require(!BbP0::ReadRequest().enabled);
 BbP0::RequestEnable(true);auto r=BbP0::ReadRequest();require(r.enabled&&r.revision==1);
 BbP0::RequestEnable(true);require(BbP0::ReadRequest().revision==1);
 BbP0::RequestEnable(false);r=BbP0::ReadRequest();require(!r.enabled&&r.revision==2);
 std::atomic<bool>done{false};std::thread ui([&]{for(unsigned n=0;n<10000;++n){BbP0::RequestEnable(true);BbP0::RequestEnable(false);}done=true;});
 uint64_t seen=2;bool coherent=true;while(!done){auto q=BbP0::ReadRequest();coherent&=q.revision>=seen&&q.enabled==bool(q.revision&1);seen=q.revision;}ui.join();require(coherent);
 BbP0::Snapshot snap;snap.state=BbP0::State::Idle;snap.accepted=3;snap.finalized=4;BbP0::Publish(snap);
 require(BbP0::ReadSnapshot().accepted==3&&BbP0::ReadSnapshot().finalized==4);
 // The same atomic handshake/proof predicate used by renderer/window: synthetic CPU
 // states only, not a claim that GPU retirement or actual peer wait has occurred.
 BbP0::ExitProof no_helper{.helper_or_launch_owned=false,.retirement_audited=false,.peer_reaped=false,
     .current_input_finalized=true,.native_last_use_complete=true,.unknown_gpu_state=false};
 BbP0::ExitHandshake never_launched;
 require(!never_launched.PublishProof(no_helper));
 never_launched.Request();never_launched.Request();require(never_launched.Read()==BbP0::ExitState::Requested);
 require(never_launched.PublishProof(no_helper));require(never_launched.Read()==BbP0::ExitState::NormalComplete);
 BbP0::ExitProof retired{.helper_or_launch_owned=true,.retirement_audited=true,.peer_reaped=true,
     .current_input_finalized=true,.native_last_use_complete=true,.unknown_gpu_state=false};
 BbP0::ExitHandshake already_retired;already_retired.Request();require(already_retired.PublishProof(retired));
 BbP0::ExitHandshake active;active.Request();active.Draining();
 auto unsafe=retired;unsafe.current_input_finalized=false;require(!active.PublishProof(unsafe));
 unsafe=retired;unsafe.native_last_use_complete=false;require(!active.PublishProof(unsafe));
 unsafe=retired;unsafe.retirement_audited=false;require(!active.PublishProof(unsafe));
 unsafe=retired;unsafe.peer_reaped=false;require(!active.PublishProof(unsafe));
 unsafe=retired;unsafe.unknown_gpu_state=true;require(!active.PublishProof(unsafe));
 require(active.Read()==BbP0::ExitState::Draining);require(active.PublishProof(retired));
 require(!active.PublishProof(retired));require(active.Read()==BbP0::ExitState::NormalComplete);
 std::cout<<"{\"status\":\"PASS\",\"scope\":\"CPU wire/types/paths/UI and pure wait/timestamp/slot/publication math only; actual Vulkan/GPU waits/queries NOT_RUN\",\"checks\":"<<checks<<"}\n";
}
