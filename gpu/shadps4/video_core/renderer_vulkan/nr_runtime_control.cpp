// SPDX-License-Identifier: GPL-2.0-or-later
#include "nr_runtime_control.h"
#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <condition_variable>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <mutex>
#include <thread>
#include <vector>
#include <stdexcept>
#include <nlohmann/json.hpp>
namespace BbNR {
namespace {
struct Present {uint64_t source,number,ns;};
struct State {
 std::mutex mutex,initialization;
 std::condition_variable wake;
 Request request{};Snapshot snapshot{};
 std::array<Present,256> presents{};size_t count=0,next=0;
 std::string path;bool dirty=false;
 std::jthread worker;
};
State& S(){static State s;return s;}
bool Fail(std::string* e,const char* message){if(e)*e=message;return false;}
using J=nlohmann::json;
J Encode(const Config& c){return J{{"version",ApiVersion},{"display_bypass",c.display_bypass},{"display_strength",c.display_strength},{"ttl_frames",c.ttl_frames},{"ttl_ms",c.ttl_ms},{"depth_tolerance",c.depth_tolerance},{"colour_tolerance",c.colour_tolerance},{"min_confidence",c.min_confidence},{"model_style",c.model_style},{"model_intensity",c.model_intensity},{"hud",c.hud}};}
Config Decode(const J& j){
 if(!j.is_object()||j.size()!=11||!j.at("version").is_number_integer()||!j.at("ttl_frames").is_number_unsigned()||!j.at("model_style").is_number_unsigned())throw std::runtime_error("runtime config must use exact versioned typed fields");
 if(j.at("version")!=ApiVersion)throw std::runtime_error("unsupported NR runtime config version");
 Config c;c.display_bypass=j.at("display_bypass").get<bool>();c.display_strength=j.at("display_strength").get<float>();c.ttl_frames=j.at("ttl_frames").get<uint32_t>();c.ttl_ms=j.at("ttl_ms").get<float>();c.depth_tolerance=j.at("depth_tolerance").get<float>();c.colour_tolerance=j.at("colour_tolerance").get<float>();c.min_confidence=j.at("min_confidence").get<float>();c.model_style=j.at("model_style").get<uint32_t>();c.model_intensity=j.at("model_intensity").get<float>();c.hud=j.at("hud").get<bool>();return c;
}
void Worker(std::stop_token stop){auto& s=S();for(;;){
 Config c;std::string path;{
  std::unique_lock lock(s.mutex);s.wake.wait_for(lock,std::chrono::milliseconds(100),[&]{return s.dirty||stop.stop_requested();});
  if(!s.dirty){if(stop.stop_requested())return;continue;}if(!stop.stop_requested())s.wake.wait_for(lock,std::chrono::milliseconds(80));c=s.request.config;path=s.path;s.dirty=false;
 }
 try{const auto p=std::filesystem::path(path);if(std::filesystem::is_symlink(p))throw std::runtime_error("symlink runtime config refused");std::filesystem::create_directories(p.parent_path());auto tmp=p;tmp+=".tmp";if(std::filesystem::is_symlink(tmp))throw std::runtime_error("symlink runtime config temporary file refused");std::ofstream out(tmp);out<<Encode(c).dump(2)<<'\n';out.close();if(!out)throw std::runtime_error("NR runtime config write failed");std::filesystem::rename(tmp,p);}
 catch(const std::exception& e){std::scoped_lock lock(s.mutex);s.snapshot.reason=std::string("Config persistence failed: ")+e.what();}
 }}
double Percentile(std::vector<double> a,double q){std::sort(a.begin(),a.end());const double p=(a.size()-1)*q;const size_t i=size_t(p);return a[i]+(a[std::min(i+1,a.size()-1)]-a[i])*(p-i);}
}
bool Validate(const Config& c,std::string* error){
 const auto range=[](float x,float a,float b){return std::isfinite(x)&&x>=a&&x<=b;};
 if(!range(c.display_strength,0,1))return Fail(error,"display strength must be finite in [0,1]");
 if(c.ttl_frames<1||c.ttl_frames>12||!range(c.ttl_ms,1,200))return Fail(error,"visual TTL must be 1..12 frames and 1..200ms");
 if(!range(c.depth_tolerance,.0001f,.25f)||!range(c.colour_tolerance,0,4)||!range(c.min_confidence,0,1))return Fail(error,"invalid finite confidence/depth/colour threshold");
 if(c.model_style>2||!range(c.model_intensity,0,2))return Fail(error,"model style must be 0..2 and intensity finite in [0,2]");
 return true;
}
bool RequiresVisualReset(const Config&a,const Config&b){return a.ttl_frames!=b.ttl_frames||a.ttl_ms!=b.ttl_ms||a.depth_tolerance!=b.depth_tolerance||a.colour_tolerance!=b.colour_tolerance||a.min_confidence!=b.min_confidence;}
bool Submit(const Config& c,Actions actions,std::string* error){
 if(error)error->clear();if(!Validate(c,error))return false;auto& s=S();std::scoped_lock lock(s.mutex);
 if(actions.apply_model&&(s.snapshot.status==Status::Held||s.snapshot.model_restart_required))return Fail(error,"Model HOLD: restart isolated session; in-place Apply is unavailable");
 const auto same_model=[](const Config&a,const Config&b){return a.model_style==b.model_style&&a.model_intensity==b.model_intensity;};
 // Coalesce repeated explicit clicks on the same in-flight target. A failed
 // request may be explicitly retried with a fresh sequence, never auto-retried.
 if(actions.apply_model&&s.snapshot.status!=Status::Failed){
  const bool pending=s.request.model_apply_seq>s.snapshot.model_applied_version;
  if((pending&&same_model(c,s.request.model_config))||(!pending&&s.snapshot.status==Status::Applied&&same_model(c,s.snapshot.actual_config)))actions.apply_model=false;
 }
 const bool changed=!(c==s.request.config);if(!changed&&!actions.reset&&!actions.capture&&!actions.apply_model)return true;
 if(RequiresVisualReset(s.request.config,c)||actions.reset)++s.request.reset_seq;
 if(actions.capture)++s.request.capture_seq;if(actions.apply_model){++s.request.model_apply_seq;s.request.model_config=c;}
 s.request.config=c;++s.request.revision;if(!s.path.empty()){s.dirty=true;s.wake.notify_one();}return true;
}
Request ReadRequest(){auto& s=S();std::scoped_lock lock(s.mutex);return s.request;}
void ShutdownConfig(){auto&s=S();std::scoped_lock init(s.initialization);if(s.worker.joinable()){s.worker.request_stop();s.wake.notify_all();s.worker.join();}}
bool InitializeConfig(const std::string& input,std::string* error){
 ShutdownConfig();auto&s=S();std::scoped_lock init(s.initialization);std::string path=input;
 if(path.empty()){if(const char* p=std::getenv("BB_NR_RUNTIME_CONFIG");p&&*p)path=p;else if(const char* p=std::getenv("BB_DATA_DIR");p&&*p)path=(std::filesystem::path(p)/"nrbridge.json").string();else return Fail(error,"private BB_DATA_DIR or explicit runtime config path required");}
 Config c;bool loaded=true;std::string why;
 try{if(std::filesystem::is_symlink(path))throw std::runtime_error("symlink config refused");if(std::filesystem::exists(path)){std::ifstream f(path);J j;f>>j;c=Decode(j);if(!Validate(c,&why))throw std::runtime_error(why);}}
 catch(const std::exception&e){c=Config{};loaded=false;why=e.what();}
 {std::scoped_lock lock(s.mutex);s.path=path;s.dirty=false;s.request.config=c;++s.request.revision;++s.request.reset_seq;if(!loaded)s.snapshot.reason="Invalid config: exact defaults restored; "+why;else if(c.model_style!=Config{}.model_style||c.model_intensity!=Config{}.model_intensity)s.snapshot.reason="Loaded model settings staged: explicit Apply required; no automatic rebuild";}
 s.worker=std::jthread(Worker);if(!loaded&&error)*error=why;return loaded;
}
bool PublishSnapshot(const Snapshot& snapshot,std::string* error){
 if(snapshot.gpu_timing_available){
  if(!snapshot.gpu_timing_source||!snapshot.gpu_timing_monotonic_ns)return Fail(error,"GPU timing requires actual sample source/time");
  for(double ms:snapshot.history_pass_ms)if(!std::isfinite(ms)||ms<0)return Fail(error,"GPU dispatch timing must be finite and nonnegative");
 }
 if(snapshot.gpu_sample_available&&(!snapshot.gpu_sample_source||!std::isfinite(snapshot.history_valid_rate)||snapshot.history_valid_rate<0||snapshot.history_valid_rate>1||!std::isfinite(snapshot.changed_fraction)||snapshot.changed_fraction<0||snapshot.changed_fraction>1))return Fail(error,"GPU counters require actual source and finite fractions");
 if(snapshot.model_refresh_available&&(!snapshot.model_refresh_source||!std::isfinite(snapshot.model_refresh_ms)||snapshot.model_refresh_ms<=0))return Fail(error,"Model refresh requires measured candidate source/positive interval");
 auto& s=S();std::scoped_lock lock(s.mutex);if(snapshot.version!=ApiVersion||snapshot.applied_revision>s.request.revision||snapshot.model_applied_version>s.request.model_apply_seq||snapshot.reset_applied_seq>s.request.reset_seq||snapshot.capture_applied_seq>s.request.capture_seq)return Fail(error,"renderer acknowledged an unrequested revision/sequence");s.snapshot=snapshot;return true;}
Snapshot ReadSnapshot(){auto& s=S();std::scoped_lock lock(s.mutex);auto out=s.snapshot;out.requested_revision=s.request.revision;out.present_samples=s.count;if(s.count>=2){std::vector<double> intervals;const size_t first=(s.next+256-s.count)%256;for(size_t k=1;k<s.count;k++)intervals.push_back(double(s.presents[(first+k)%256].ns-s.presents[(first+k-1)%256].ns)/1e6);double sum=0;for(double x:intervals)sum+=x;out.fps=1000*intervals.size()/sum;out.p95_ms=Percentile(intervals,.95);out.p99_ms=Percentile(intervals,.99);out.timing_available=true;}return out;}
void NotifyPresent(uint64_t source,uint64_t number,uint64_t ns){if(!source||!number||!ns)return;auto&s=S();std::scoped_lock lock(s.mutex);if(s.count){auto prev=s.presents[(s.next+255)%256];if(source<=prev.source||number<=prev.number||ns<=prev.ns)return;}s.presents[s.next]={source,number,ns};s.next=(s.next+1)%256;s.count=std::min(s.count+1,size_t(256));}
const char* StatusName(Status s){switch(s){case Status::Unavailable:return "Unavailable";case Status::Pending:return "Pending";case Status::Applied:return "Applied";case Status::Failed:return "Failed";case Status::Held:return "HOLD";}return "Unknown";}
}
