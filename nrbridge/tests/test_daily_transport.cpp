#include "pre_sr_wire.h"
#include <cassert>
#include <functional>
#include <set>
#include <iostream>
using namespace NativeRuntimeModel;
namespace W=origin_wire;
void rejects(std::function<void()> f){bool yes=false;try{f();}catch(const std::exception&){yes=true;}assert(yes);}
int main(){
 unsetenv("BB_NR_DAILY");assert(JobLimit()==16384);auto old=W::config_expected("session-A","resident",std::string(32,'a'),std::string(32,'b'));
 setenv("BB_NR_DAILY","1",1);assert(Daily()&&DailySlots>=2);rejects([&]{W::config(old,"session-A","resident");});
 auto config=W::config_expected("session-A","resident",std::string(32,'a'),std::string(32,'b'));W::config(config,"session-A","resident");assert(config["max_jobs"]==JobLimit()&&config["service_wall_seconds"]==0);assert(config["protocol_version"]==2&&config["transport_layout"]==DailyLayout);
 std::set<std::string> files;auto create=[&](uint64_t j){for(const char* k:{"input","complete","consumed"})assert(files.insert(W::job_path("r",k,"s",j)).second);for(bool a:{false,true})assert(files.insert("r\\"+JobName(j,a)).second);};
 W::Counts counts;
 for(uint64_t j=0;j<20000;j++){
  create(j);
  // Native can reserve/publish j+1 before cleanup j. 64 slots do not alias.
  assert(W::job_path("r","input","s",j)!=W::job_path("r","input","s",j+1));
  W::J fields={{"job",j},{"origin_source",j+1},{"origin_epoch",1u},{"origin_history",1u}};
  counts.finalize(fields,"captured");W::cleanup_consumed("r","s",j,[&](const auto& p){return files.erase(p)==1;});assert(files.empty());
 }
 assert(counts.finalized==20000);create(20000);size_t calls=0;rejects([&]{W::cleanup_consumed("r","s",20000,[&](const auto& p){if(++calls==2)return false;return files.erase(p)==1;});});assert(calls==2);
 // Exercise the real permitted overlap: Native publishes the next input and
 // sidecar before Helper finishes deleting the previous job. Cleanup must
 // preserve both files, including when the next ordinal wraps to slot zero.
 files.clear();
 auto publish=[&](uint64_t j){
  assert(files.insert(W::job_path("r","input","s",j)).second);
  assert(files.insert("r\\"+JobName(j)).second);
 };
 publish(0);
 for(uint64_t j=0;j<20000;j++){
  assert(files.size()==2);
  assert(files.insert(W::job_path("r","complete","s",j)).second);
  assert(files.insert(W::job_path("r","consumed","s",j)).second);
  assert(files.insert("r\\"+JobName(j,true)).second);
  publish(j+1);assert(files.size()==7);
  W::cleanup_consumed("r","s",j,[&](const auto& p){return files.erase(p)==1;});
  assert(files.size()==2&&files.count(W::job_path("r","input","s",j+1))&&files.count("r\\"+JobName(j+1)));
 }
 auto input=W::common("s","resident");input.update({{"job",1u},{"origin_source",2u},{"origin_epoch",1u},{"origin_history",1u},{"op","input"},{"status","ORIGIN_INPUT_READY"},{"native_input_tick",1u},{"native_input_GPU_complete",true},{"source_reset",false},{"source_jitter_metadata",W::spec()["source_jitter_metadata_exact"]},{"model_reset",1u}});
 for(auto& value:input["source_jitter_metadata"].items())value.value()=0.0;
 W::input(input,W::common("s","resident"),1);
 assert(W::job_path("r","input","s",1)==W::job_path("r","input","s",65));
 rejects([&]{W::input(input,W::common("s","resident"),65);}); // stale slot tuple never accepted
 auto sidecar=JobModel("s",W::job_fields(input),0,1,Settings{});
 auto new_fields=W::job_fields(input);new_fields["job"]=65u;
 rejects([&]{CheckJob(sidecar,"s",new_fields,0,1,Settings{});});
 W::Counts edge;edge.finalized=JobLimit();rejects([&]{edge.finalize(W::J{{"job",JobLimit()}},"captured");});rejects([&]{W::job_path("r","input","s",JobLimit());});
 rejects([&]{U(W::J{{"v",CounterLimit+1}},"v");});
 assert(U(W::J{{"v",CounterLimit}},"v")==CounterLimit);
 Commit commit;commit.revision=CounterLimit;commit.previous_generation=CounterLimit-2;commit.generation=CounterLimit-1;commit.previous_params=CounterLimit-1;commit.params=CounterLimit;commit.shared_job_finalized=true;commit.native_GPU_complete=true;
 assert(Valid(commit));auto request=Request<W::J>("s",commit);
 CheckAck(Ack(request,CounterLimit,CounterLimit,CounterLimit-1),request,CounterLimit-1);
 commit.generation=CounterLimit;commit.previous_generation=CounterLimit-1;assert(!Valid(commit));
 commit.previous_generation=UINT64_MAX;commit.generation=0;assert(!Valid(commit));
 unsetenv("BB_NR_DAILY");rejects([&]{W::config(config,"session-A","resident");});
 std::cout<<"DAILY_TRANSPORT_CPU_PASS wraps=312 jobs=20000 stale_tuple/deletion_failure/counter_limits/mixed_version rejected; no GPU claims\n";
}
