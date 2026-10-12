// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
// Private P0 protocol6 only. The canonical contract remains Integration-owned.
#include <nlohmann/json.hpp>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <set>
#include <stdexcept>
#include <string>
#include <string_view>
#include <unordered_set>
#include <vector>
#include <sys/stat.h>
namespace Vulkan::P0Wire {
using Json = nlohmann::json;
inline constexpr std::string_view ProfileId="p0-quality-hdr-direct720-experimental-v1";
inline constexpr std::string_view Layout="frame-buckets-64-v1";
inline Json Profile() {
 return {{"color_width",1920u},{"color_height",1080u},{"guide_width",1280u},{"guide_height",720u},
 {"model_width",1920u},{"model_height",1080u},{"output_width",1920u},{"output_height",1080u},
 {"working_scale",1.0},{"color_domain","scene_hdr"},{"passthrough",false},{"white_point",1.0},
 {"exposure_source","manual_unknown_game_exposure"},{"exposure_valid",false},
 {"stored_mv_units","render_pixels_previous_minus_current"},{"mv_values_jittered",false},
 {"guide_sampling","raw_jittered_raster_same_texel"},{"phase_correction","none"},
 {"nr_phase_status","UNVERIFIED"},{"k_output_status","NOT_IDENTIFIED"},
 {"nr_mvec_scale_x",1.0},{"nr_mvec_scale_y",1.0},{"nr_mvec_scale_status","EXPERIMENTAL_UNVERIFIED"}};
}
inline uint64_t Uint(const Json& j,std::string_view key) {
 const auto& v=j.at(std::string(key));
 if(v.is_number_unsigned()) return v.get<uint64_t>();
 if(v.is_number_integer()&&v.get<int64_t>()>=0) return static_cast<uint64_t>(v.get<int64_t>());
 throw std::runtime_error("P0 integral unsigned field required: "+std::string(key));
}
inline double Number(const Json& j,std::string_view key) {
 const auto& v=j.at(std::string(key));
 if(!v.is_number()||!std::isfinite(v.get<double>())) throw std::runtime_error("P0 finite number required: "+std::string(key));
 return v.get<double>();
}
inline void ValidateProfile(const Json& j) {
 const auto expected=Profile();
 if(!j.is_object()||j.size()!=expected.size()) throw std::runtime_error("P0 immutable profile shape mismatch");
 for(const auto& [key,v]:expected.items()) {
  if(!j.contains(key)) throw std::runtime_error("P0 profile missing "+key);
  const auto& a=j.at(key);
  if(v.is_number_float()) {if(Number(j,key)!=v.get<double>()) throw std::runtime_error("P0 profile numeric mismatch "+key);}
  else if(v.is_number_unsigned()) {if(Uint(j,key)!=v.get<uint64_t>()) throw std::runtime_error("P0 profile extent mismatch "+key);}
  else if(a.type()!=v.type()||a!=v) throw std::runtime_error("P0 profile type/value mismatch "+key);
 }
}
inline void CheckFinite(const Json& j) {
 if(j.is_number_float()&&!std::isfinite(j.get<double>())) throw std::runtime_error("P0 nonfinite JSON number");
 if(j.is_structured()) for(const auto& v:j) CheckFinite(v);
}
inline Json Parse(std::string_view text,std::string_view source) {
 if(text.empty()||text.size()>65536) throw std::runtime_error("P0 control size: "+std::string(source));
 std::vector<std::unordered_set<std::string>> stack; bool duplicate=false;
 Json::parser_callback_t cb=[&](int,Json::parse_event_t event,Json& value) {
  if(event==Json::parse_event_t::object_start) stack.emplace_back();
  else if(event==Json::parse_event_t::key) {if(stack.empty()||!stack.back().insert(value.get<std::string>()).second) duplicate=true;}
  else if(event==Json::parse_event_t::object_end) {if(!stack.empty()) stack.pop_back();}
  return true;
 };
 Json j=Json::parse(text.begin(),text.end(),cb,true,false);
 if(duplicate||!j.is_object()) throw std::runtime_error("P0 duplicate JSON key/non-object: "+std::string(source));
 CheckFinite(j);return j;
}
inline std::string Validation(std::string_view execution) {return execution=="diagnostic"?"p0_diagnostic":"p0_hot_path";}
inline Json ParameterApplication() {
 return {{"frame_ms","HOST_FIXTURE_DECLARATION_NOT_SENT_TO_NR_API"},
 {"exposure","HOST_FIXTURE_DECLARATION_NOT_SENT_TO_NR_API"},
 {"pre_exposure","HOST_FIXTURE_DECLARATION_NOT_SENT_TO_NR_API"},{"runtime_default_values","UNVERIFIED"}};
}
// Actual P0 diagnostic wire accounts all five buffers in one pixel total. There is
// no separate guide_readback_bytes field; per-role bytes remain hard evidence.
inline void ValidateDiagnosticReadbackAccounting(const Json& j) {
 if(Uint(j,"helper_pixel_readback_bytes")!=94003200u||
    Uint(j,"gpu_validator_readback_bytes")!=64u||Uint(j,"audit_readback_bytes")!=94003264u||
    Uint(j,"dump_count")!=5u||Uint(j.at("guide_validation").at("depth"),"bytes")!=3686400u||
    Uint(j.at("guide_validation").at("mv"),"bytes")!=7372800u)
  throw std::runtime_error("P0 actual diagnostic pixel/validator/per-role readback accounting mismatch");
}
inline bool Allowed(std::string_view mode,unsigned count,std::string_view execution) {
 if(execution=="diagnostic") return count==4&&(mode=="resident"||mode=="skipEvaluate");
 return execution=="hot"&&(mode=="resident"||mode=="srOnly")&&
  (count==4||count==120||count==1800||count==18000);
}
struct BridgeResourcePlan {
 bool diagnostic = false;
 uint64_t full_pixel_buffer_bytes = 0, guide_buffer_bytes = 0;
 uint64_t validator_bytes = 0, return_validator_bytes = 0, guard_bytes = 4;
};
// This is the actual allocation decision, shared with CPU tests. It is not a GPU
// availability claim. The Helper bridge accepts only the two declared NR modes.
inline BridgeResourcePlan SelectBridgeResources(std::string_view mode,unsigned count,
                                               std::string_view execution,unsigned width,unsigned height) {
 if((mode!="resident"&&mode!="skipEvaluate")||!Allowed(mode,count,execution)||width!=1920||height!=1080)
  throw std::runtime_error("P0 unsupported bridge mode/count/execution/extent");
 BridgeResourcePlan p;
 p.diagnostic=execution=="diagnostic";
 if(p.diagnostic) {
  p.full_pixel_buffer_bytes=uint64_t(width)*height*48;
  p.guide_buffer_bytes=18432000;
  p.validator_bytes=64;p.return_validator_bytes=16;
 }
 return p;
}
inline std::string FrameDir(const std::string& run,uint32_t frame,uint32_t count) {
 if((count!=1&&count!=4&&count!=120&&count!=1800&&count!=18000)||frame>=count) throw std::runtime_error("P0 frame resolver bounds");
 char bucket[32];std::snprintf(bucket,sizeof(bucket),"bucket-%06u",frame/64);
 return run+"/frames/"+bucket;
}
inline std::string FramePath(const std::string& run,uint32_t frame,uint32_t count,const std::string& basename) {
 if(basename.empty()||basename=="."||basename==".."||basename.find('/')!=std::string::npos||basename.find('\\')!=std::string::npos)
  throw std::runtime_error("P0 frame basename invalid");
 return FrameDir(run,frame,count)+"/"+basename;
}
inline void Precreate(const std::string& run,uint32_t count) {
 for(uint32_t f=0;f<count;f+=64) {
  const auto path=FrameDir(run,f,count); std::filesystem::create_directories(path);
  for(const auto& dir:{run+"/frames",path}) {
   if(std::filesystem::is_symlink(dir)||!std::filesystem::is_directory(dir)||chmod(dir.c_str(),0700)!=0)
    throw std::runtime_error("P0 private bucket unsafe: "+dir);
  }
 }
}
}
