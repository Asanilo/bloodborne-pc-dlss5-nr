// CPU/offline validation of the exact protocol used by Native and Helper.
#include <json.hpp>
#include "native_runtime_model.h"
#include <cassert>
#include <functional>
#include <iostream>
using J=nlohmann::json;using namespace NativeRuntimeModel;
static void rejects(const std::function<void()>& fn){bool rejected=false;try{fn();}catch(const std::exception&){rejected=true;}assert(rejected);}
int main(){
 Commit c;c.revision=7;c.previous_generation=0;c.generation=1;c.previous_params=1;c.params=3;c.settings={2,0.f};c.finalized_jobs=5;c.native_target_tick=200;c.native_observed_tick=200;c.packet_leases=0;c.shared_job_finalized=true;c.native_GPU_complete=true;
 auto req=J::parse(Request<J>("session-A",c).dump());auto parsed=Parse(req,"session-A");assert(parsed.settings.style==2&&parsed.settings.intensity==0.f&&parsed.params==3);
 auto ack=J::parse(Ack(req,301,2,1).dump());CheckAck(ack,req,300);
 for(auto field:{"session","style","params","generation","revision","native_target_tick"}){auto bad=ack;if(std::string(field)=="session")bad[field]="another";else bad[field]=uint64_t(99);rejects([&]{CheckAck(bad,req,300);});}
 auto typed=ack;typed["revision"]=7.0;rejects([&]{CheckAck(typed,req,300);});
 auto a=ack;a["helper_gpu_serial"]=uint64_t(300);rejects([&]{CheckAck(a,req,300);});
 a=ack;a["helper_GPU_complete"]=false;rejects([&]{CheckAck(a,req,300);});
 a=ack;a["feature_create_calls"]=uint64_t(1);rejects([&]{CheckAck(a,req,300);});
 for(auto pair:{std::pair{"packet_leases",uint64_t(1)},{"native_observed_tick",uint64_t(199)},{"generation",uint64_t(3)},{"params",uint64_t(1)}}){auto bad=req;bad[pair.first]=pair.second;rejects([&]{Parse(bad,"session-A");});}
 for(auto field:{"native_GPU_complete","shared_job_finalized"}){auto bad=req;bad[field]=false;rejects([&]{Parse(bad,"session-A");});}
 for(float intensity:{-1.f,2.001f}){auto bad=req;bad["intensity"]=intensity;rejects([&]{Parse(bad,"session-A");});}
 for(unsigned style:{3u,UINT32_MAX}){auto bad=req;bad["style"]=style;rejects([&]{Parse(bad,"session-A");});}
 auto bad=req;bad["intensity"]=1;rejects([&]{Parse(bad,"session-A");});bad=req;bad["extra"]=true;rejects([&]{Parse(bad,"session-A");});
 J fields={{"job",uint64_t(5)},{"origin_source",uint64_t(800)},{"origin_epoch",uint64_t(2)},{"origin_history",uint64_t(4)}};
 auto job=J::parse(JobModel("session-A",fields,1,3,c.settings).dump());CheckJob(job,"session-A",fields,1,3,c.settings);
 rejects([&]{CheckJob(job,"session-A",fields,0,3,c.settings);});rejects([&]{CheckJob(job,"session-A",fields,1,2,c.settings);});
 auto stale=fields;stale["origin_source"]=uint64_t(799);rejects([&]{CheckJob(job,"session-A",stale,1,3,c.settings);});
 rejects([&]{CheckJob(job,"session-A",fields,1,3,Settings{1,0.f});});
 std::cout<<"RUNTIME_MODEL_PROTOCOL_CPU_PASS real creation ACK required; no GPU claims\n";
}
