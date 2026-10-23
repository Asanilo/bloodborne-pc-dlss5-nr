#pragma once
// SPDX-License-Identifier: GPL-3.0-or-later
// Root's sole pre-SR interface delta; lifecycle inherited from async-origin-v3. No P0 schema changes or alternate canonical.
#include <json.hpp>
#include "nr_dimensions.h"
#include <set>
#include <string>
#include <cmath>
#include <algorithm>
#include "pre_sr_spec.h"
#include "native_runtime_model.h"
namespace origin_wire {
using J=nlohmann::json;
inline void need(bool v,const char*m){if(!v)throw std::invalid_argument(m);}
inline J parse(const std::string&t){std::vector<std::set<std::string>>s;auto cb=[&](int,J::parse_event_t e,J&v){if(e==J::parse_event_t::object_start)s.emplace_back();else if(e==J::parse_event_t::key)need(!s.empty()&&s.back().insert(v.get<std::string>()).second,"duplicate key");else if(e==J::parse_event_t::object_end)s.pop_back();if(e==J::parse_event_t::value&&v.is_number_float())need(std::isfinite(v.get<double>()),"nonfinite");return true;};auto j=J::parse(t,cb);need(j.is_object(),"JSON object");return j;}
inline const J&spec(){static J legacy=parse(pre_sr_spec_json);if(!NativeRuntimeModel::Daily())return legacy;static J daily=[](){auto j=BbNRDimensions::Spec(legacy);j["transport_layout"]=NativeRuntimeModel::DailyLayout;auto&e=j["bootstrap_exact"]["async-config.json"]["exact_sample"];e["protocol_version"]=BbNRDimensions::Protocol(true);e["transport_layout"]=NativeRuntimeModel::DailyLayout;e["max_jobs"]=NativeRuntimeModel::JobLimit();e["service_wall_seconds"]=0u;return j;}();return daily;}
inline uint64_t u(const J&j,const char*k){need(j.contains(k)&&j.at(k).is_number_unsigned(),k);auto v=j.at(k).get<uint64_t>();need(v<=9007199254740991ull,"uint53 bound");return v;}
inline bool b(const J&j,const char*k){need(j.contains(k)&&j.at(k).is_boolean(),k);return j.at(k).get<bool>();}
inline std::string s(const J&j,const char*k){need(j.contains(k)&&j.at(k).is_string(),k);return j.at(k).get<std::string>();}
inline void exact(const J&j,const std::set<std::string>&keys){need(j.is_object()&&j.size()==keys.size(),"exact stage keys");for(auto&k:keys)need(j.contains(k),"missing stage key");}
inline std::set<std::string>keys(const J&j){std::set<std::string>k;for(auto i=j.begin();i!=j.end();++i)k.insert(i.key());return k;}
inline bool same_typed(const J&j,const J&e){if(j.is_object()&&e.is_object()){if(j.size()!=e.size())return false;for(auto i=e.begin();i!=e.end();++i)if(!j.contains(i.key())||!same_typed(j.at(i.key()),i.value()))return false;return true;}if(j.is_array()&&e.is_array()){if(j.size()!=e.size())return false;for(size_t i=0;i<e.size();i++)if(!same_typed(j[i],e[i]))return false;return true;}if(e.is_boolean())return j.is_boolean()&&j==e;if(e.is_number_unsigned())return j.is_number_unsigned()&&j==e;if(e.is_number())return j.is_number()&&!j.is_boolean()&&std::isfinite(j.get<double>())&&j==e;return j.type()==e.type()&&j==e;}
inline bool hex32(const std::string&s){return s.size()==32&&std::all_of(s.begin(),s.end(),[](char c){return(c>='0'&&c<='9')||(c>='a'&&c<='f');});}
inline J config_expected(const std::string&session,const std::string&mode,const std::string&device,const std::string&driver){auto e=spec()["bootstrap_exact"]["async-config.json"]["exact_sample"];e["session"]=session;e["mode"]=mode;e["resource_set"]=session+"-g0";e["device_uuid"]=device;e["driver_uuid"]=driver;return e;}
inline void config(const J&j,const std::string&session,const std::string&mode){need(mode=="resident"||mode=="skipEvaluate","mode");auto device=s(j,"device_uuid"),driver=s(j,"driver_uuid");need(hex32(device)&&hex32(driver),"actual UUID syntax");need(same_typed(j,config_expected(session,mode,device,driver)),"exact config types/literals");}
inline J common(const std::string&session,const std::string&mode){return J{{"protocol_version",BbNRDimensions::Protocol(NativeRuntimeModel::Daily())},{"transport_layout",spec()["transport_layout"]},{"execution_mode","hot"},{"mode",mode},{"session",session},{"generation",0u},{"resource_set",session+"-g0"},{"profile_id",spec()["profile_id"]},{"quality_profile",spec()["quality_profile"]},{"params_version",1u},{"parameter_application",spec()["parameter_application"]}};}
inline void common_check(const J&j,const J&c){for(auto i=c.begin();i!=c.end();++i)need(j.contains(i.key())&&same_typed(j.at(i.key()),i.value()),"common exact identity/profile");}
inline std::set<std::string>stage_keys(const J&common,const J&fields){auto k=keys(common);for(auto i=fields.begin();i!=fields.end();++i)k.insert(i.key());return k;}
inline void description_types(const J&j,const J&fields){for(auto i=fields.begin();i!=fields.end();++i){auto d=i.value().get<std::string>();const auto&v=j.at(i.key());if(d.rfind("uint",0)==0)u(j,i.key().c_str());else if(d.rfind("bool",0)==0)b(j,i.key().c_str());else if(d.rfind("string",0)==0)s(j,i.key().c_str());else if(d.rfind("array",0)==0)need(v.is_array(),"array field");else if(d.rfind("object",0)==0)need(v.is_object(),"object field");else if(d.rfind("number",0)==0)need(v.is_number()&&!v.is_boolean()&&std::isfinite(v.get<double>()),"finite number field");}}
inline void bootstrap(const J&j,const J&c,const char*stage){auto f=spec()["bootstrap_exact"][stage]["required_stage_fields"];exact(j,stage_keys(c,f));common_check(j,c);description_types(j,f);}
inline void jitter(const J&j){exact(j,keys(spec()["source_jitter_metadata_exact"]));for(auto i=j.begin();i!=j.end();++i)need(i.value().is_number()&&!i.value().is_boolean()&&std::isfinite(i.value().get<double>()),"jitter scalar finite actual");}
inline J job_fields(const J&j){J e;for(const char*k:{"job","origin_source","origin_epoch","origin_history"}){auto v=u(j,k);need(k==std::string("job")?v<NativeRuntimeModel::JobLimit():v>0,"job origin identity");e[k]=v;}return e;}
inline void input(const J&j,const J&c,uint64_t expected){auto k=keys(c);for(const char*p:{"job","origin_source","origin_epoch","origin_history","op","status","native_input_tick","native_input_GPU_complete","source_reset","source_jitter_metadata","model_reset"})k.insert(p);if(BbNRDimensions::Get().enabled)k.insert("source_layout");exact(j,k);common_check(j,c);if(BbNRDimensions::Get().enabled)BbNRDimensions::CheckSourceLayout(j.at("source_layout"));job_fields(j);need(u(j,"job")==expected&&s(j,"op")=="input"&&s(j,"status")=="ORIGIN_INPUT_READY"&&u(j,"native_input_tick")>0&&b(j,"native_input_GPU_complete")&&u(j,"model_reset")==1,"exact origin input actual GPU release");b(j,"source_reset");jitter(j.at("source_jitter_metadata"));}
struct Counts {uint64_t finalized=0,captured=0,obsolete=0,skip=0;J last{{"origin_source",0u},{"origin_epoch",0u},{"origin_history",1u}};J fields()const{J j=last;j.update(J{{"finalized_jobs",finalized},{"captured_jobs",captured},{"obsolete_jobs",obsolete},{"skip_jobs",skip},{"job",finalized},{"next_job",finalized},{"source_frame_valid",finalized>0}});return j;}
 void check(const J&j)const{auto f=fields();for(auto i=f.begin();i!=f.end();++i)need(j.contains(i.key())&&same_typed(j.at(i.key()),i.value()),"terminal actual N/counts/origin");need(finalized==captured+obsolete+skip&&finalized<=NativeRuntimeModel::JobLimit(),"count invariant");}
 void finalize(const J&input,const std::string&disposition){need(u(input,"job")==finalized&&finalized<NativeRuntimeModel::JobLimit(),"one exact finalized job");if(disposition=="captured")++captured;else if(disposition=="obsolete_discard")++obsolete;else if(disposition=="expected_skip")++skip;else need(false,"disposition");++finalized;for(const char*k:{"origin_source","origin_epoch","origin_history"})last[k]=input.at(k);}
};
inline void terminal(const J&j,const J&c,const Counts&counts,const char*stage){auto f=spec()["terminal_exact"][stage];auto k=stage_keys(c,f);for(auto&key:keys(counts.fields()))k.insert(key);exact(j,k);common_check(j,c);counts.check(j);description_types(j,f);}
inline void native_last_use(const J&j){const auto proof=s(j,"native_last_use_proof");const auto t=u(j,"native_last_use_tick"),o=u(j,"native_last_use_observed_tick");if(proof=="ACTUAL_NATIVE_GPU_LAST_USE")need(t>0&&o>=t&&b(j,"completed_GPU"),"actual Native max last-use proof");else need(proof=="NOT_REQUIRED_NO_NATIVE_USE"&&t==0&&o==0&&!b(j,"completed_GPU"),"N0 is not a no-use exemption");}
inline std::string base(const std::string&kind,const std::string&session){return "async-"+kind+"-"+session+"-g0.json";}
inline std::string bucket_leaf(uint64_t job){need(job<NativeRuntimeModel::JobLimit(),"checked job ordinal");auto n=std::to_string(NativeRuntimeModel::FileOrdinal(job)/64);return "bucket-"+std::string(6-n.size(),'0')+n;}
inline std::string job_path(const std::string&root,const std::string&kind,const std::string&session,uint64_t job){return root+"\\frames\\"+bucket_leaf(job)+"\\async-"+kind+"-"+session+"-g0-f"+std::to_string(NativeRuntimeModel::FileOrdinal(job))+".json";}
// Caller must validate the exact CONSUMED tuple and actual GPU last-use first.
// Failure quarantines the service before next_input, leaving evidence intact.
template<class Delete> inline void cleanup_consumed(const std::string& root,const std::string& session,uint64_t job,Delete remove){
 need(NativeRuntimeModel::Daily(),"daily cleanup only");
 for(const char* kind:{"input","complete","consumed"})need(remove(job_path(root,kind,session,job)),"daily transport cleanup; HOLD");
 for(bool ack:{false,true})need(remove(root+"\\"+NativeRuntimeModel::JobName(job,ack)),"daily sidecar cleanup; HOLD");
}

}
