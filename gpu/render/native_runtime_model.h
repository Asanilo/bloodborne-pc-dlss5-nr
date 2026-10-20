// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <cstdint>
#include <set>
#include <stdexcept>
#include <string>
namespace NativeRuntimeModel {
inline constexpr uint64_t CounterLimit=9007199254740991ull;
inline constexpr uint64_t DailySlots=64;
inline bool Daily(){const char* p=std::getenv("BB_NR_DAILY");return p&&!std::strcmp(p,"1");}
inline uint64_t JobLimit(){return Daily()?CounterLimit-2:16384u;}
inline uint64_t FileOrdinal(uint64_t job){return Daily()?job%DailySlots:job;}
inline constexpr const char* DailyLayout="frame-ring-64-daily-v2";

struct Settings {uint32_t style=0;float intensity=1.f;};
inline bool Valid(Settings s) {return s.style<=2&&std::isfinite(s.intensity)&&s.intensity>=0.f&&s.intensity<=2.f;}
struct Commit {
 uint64_t revision=0,previous_generation=0,generation=1,previous_params=1,params=2;
 Settings settings;
 uint64_t finalized_jobs=0,native_target_tick=0,native_observed_tick=0;
 uint64_t packet_leases=0;bool shared_job_finalized=false,native_GPU_complete=false;
};
inline bool Valid(const Commit& c) {
 return c.revision<=CounterLimit&&c.generation<CounterLimit&&c.params<=CounterLimit&&c.finalized_jobs<=CounterLimit&&c.native_target_tick<=CounterLimit&&c.native_observed_tick<=CounterLimit&&Valid(c.settings)&&c.revision>0&&c.generation==c.previous_generation+1&&c.generation>c.previous_generation&&
 c.params>c.previous_params&&c.previous_params>0&&c.packet_leases==0&&c.shared_job_finalized&&c.native_GPU_complete&&
 c.native_observed_tick>=c.native_target_tick;
}
inline void Need(bool yes,const char* text){if(!yes)throw std::runtime_error(text);}
template<class J> uint64_t U(const J& j,const char* k){Need(j.at(k).is_number_unsigned(),"model protocol unsigned");auto v=j.at(k).template get<uint64_t>();Need(v<=CounterLimit,"model uint53 bound");return v;}
template<class J> void Exact(const J& j,std::set<std::string> keys){Need(j.is_object()&&j.size()==keys.size(),"model protocol exact object");for(auto& k:keys)Need(j.contains(k),"model protocol missing field");}
inline std::set<std::string> Keys(){return {"schema","session","revision","previous_generation","generation","previous_params","params","style","intensity","finalized_jobs","native_target_tick","native_observed_tick","packet_leases","shared_job_finalized","native_GPU_complete"};}
template<class J> J Request(const std::string& session,const Commit& c){
 Need(Valid(c)&&!session.empty(),"model commit invalid");return J{{"schema","nr-runtime-model-v1"},{"session",session},{"revision",c.revision},{"previous_generation",c.previous_generation},{"generation",c.generation},{"previous_params",c.previous_params},{"params",c.params},{"style",c.settings.style},{"intensity",c.settings.intensity},{"finalized_jobs",c.finalized_jobs},{"native_target_tick",c.native_target_tick},{"native_observed_tick",c.native_observed_tick},{"packet_leases",c.packet_leases},{"shared_job_finalized",c.shared_job_finalized},{"native_GPU_complete",c.native_GPU_complete}};
}
template<class J> Commit Parse(const J& j,const std::string& session){
 Exact(j,Keys());Need(j.at("schema")=="nr-runtime-model-v1"&&j.at("session")==session,"model session/schema");
 Commit c;c.revision=U(j,"revision");c.previous_generation=U(j,"previous_generation");c.generation=U(j,"generation");c.previous_params=U(j,"previous_params");c.params=U(j,"params");auto style=U(j,"style");Need(style<=2,"model style range");c.settings.style=uint32_t(style);
 Need(j.at("intensity").is_number_float(),"model intensity float");c.settings.intensity=j.at("intensity").template get<float>();
 c.finalized_jobs=U(j,"finalized_jobs");c.native_target_tick=U(j,"native_target_tick");c.native_observed_tick=U(j,"native_observed_tick");c.packet_leases=U(j,"packet_leases");
 Need(j.at("shared_job_finalized").is_boolean()&&j.at("native_GPU_complete").is_boolean(),"model proof bool");c.shared_job_finalized=j.at("shared_job_finalized").template get<bool>();c.native_GPU_complete=j.at("native_GPU_complete").template get<bool>();Need(Valid(c),"model drain/version/range proof");return c;
}
template<class J> J Ack(const J& request,uint64_t helperSerial,uint64_t creates,uint64_t releases){auto j=request;j["op"]="model_ack";j["status"]="FEATURE_REBUILT";j["helper_gpu_serial"]=helperSerial;j["helper_GPU_complete"]=true;j["feature_create_calls"]=creates;j["feature_release_calls"]=releases;return j;}
template<class J> void CheckAck(const J& ack,const J& request,uint64_t previousHelperSerial){auto keys=Keys();for(auto k:{"op","status","helper_gpu_serial","helper_GPU_complete","feature_create_calls","feature_release_calls"})keys.insert(k);Exact(ack,keys);for(const auto& k:Keys())Need(ack.at(k).type()==request.at(k).type()&&ack.at(k)==request.at(k),"model ack exact typed identity");Need(ack.at("op")=="model_ack"&&ack.at("status")=="FEATURE_REBUILT"&&ack.at("helper_GPU_complete").is_boolean()&&ack.at("helper_GPU_complete")==true&&U(ack,"helper_gpu_serial")>previousHelperSerial,"actual creation completion");Need(U(ack,"feature_create_calls")==U(ack,"generation")+1&&U(ack,"feature_release_calls")==U(ack,"generation"),"actual feature create/release counts");}
inline std::string RequestName(uint64_t generation){return "runtime-model-request-"+std::to_string(generation)+".json";}
inline std::string AckName(uint64_t generation){return "runtime-model-ack-"+std::to_string(generation)+".json";}
inline std::string JobName(uint64_t job,bool ack=false){return std::string("runtime-job-model-")+(ack?"ack-":"")+std::to_string(FileOrdinal(job))+".json";}
template<class J> J JobModel(const std::string& session,const J& fields,uint64_t generation,uint64_t params,Settings settings){return J{{"schema","nr-runtime-job-model-v1"},{"session",session},{"job",fields.at("job")},{"origin_source",fields.at("origin_source")},{"origin_epoch",fields.at("origin_epoch")},{"origin_history",fields.at("origin_history")},{"model_generation",generation},{"params",params},{"style",settings.style},{"intensity",settings.intensity}};}
template<class J> void CheckJob(const J& j,const std::string& session,const J& fields,uint64_t generation,uint64_t params,Settings settings){auto expected=JobModel(session,fields,generation,params,settings);Exact(j,{"schema","session","job","origin_source","origin_epoch","origin_history","model_generation","params","style","intensity"});Need(j==expected,"model job exact tuple/generation/parameters");Need(j.at("intensity").is_number_float(),"model job intensity float");for(auto k:{"job","origin_source","origin_epoch","origin_history","model_generation","params","style"})(void)U(j,k);}

}
