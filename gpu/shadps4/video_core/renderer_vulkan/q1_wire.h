// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
// Private Q1 protocol5 only. The canonical contract remains Integration-owned.
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
namespace Vulkan::Q1Wire {
using Json = nlohmann::json;
inline constexpr std::string_view ProfileId="quality-hdr-direct720-raw-renderpx-v1";
inline constexpr std::string_view Layout="frame-buckets-64-v1";
inline Json Profile() {
 return {{"color_width",1920u},{"color_height",1080u},{"guide_width",1280u},{"guide_height",720u},
 {"model_width",1920u},{"model_height",1080u},{"output_width",1920u},{"output_height",1080u},
 {"working_scale",1.0},{"color_domain","scene_hdr"},{"passthrough",false},{"white_point",1.0},
 {"exposure_source","manual_unknown_game_exposure"},{"exposure_valid",false},
 {"stored_mv_units","render_pixels_previous_minus_current"},{"mv_values_jittered",false},
 {"guide_sampling","raw_jittered_raster_same_texel"},{"phase_correction","none"},
 {"nr_phase_status","UNVERIFIED"},{"k_output_status","NOT_IDENTIFIED"},
 {"nr_mvec_scale_x",1.0},{"nr_mvec_scale_y",1.0},{"nr_mvec_scale_status","production_candidate_UNVERIFIED"}};
}
inline uint64_t Uint(const Json& j,std::string_view key) {
 const auto& v=j.at(std::string(key));
 if(v.is_number_unsigned()) return v.get<uint64_t>();
 if(v.is_number_integer()&&v.get<int64_t>()>=0) return static_cast<uint64_t>(v.get<int64_t>());
 throw std::runtime_error("Q1 integral unsigned field required: "+std::string(key));
}
inline double Number(const Json& j,std::string_view key) {
 const auto& v=j.at(std::string(key));
 if(!v.is_number()||!std::isfinite(v.get<double>())) throw std::runtime_error("Q1 finite number required: "+std::string(key));
 return v.get<double>();
}
inline void ValidateProfile(const Json& j) {
 const auto expected=Profile();
 if(!j.is_object()||j.size()!=expected.size()) throw std::runtime_error("Q1 immutable profile shape mismatch");
 for(const auto& [key,v]:expected.items()) {
  if(!j.contains(key)) throw std::runtime_error("Q1 profile missing "+key);
  const auto& a=j.at(key);
  if(v.is_number_float()) {if(Number(j,key)!=v.get<double>()) throw std::runtime_error("Q1 profile numeric mismatch "+key);}
  else if(v.is_number_unsigned()) {if(Uint(j,key)!=v.get<uint64_t>()) throw std::runtime_error("Q1 profile extent mismatch "+key);}
  else if(a.type()!=v.type()||a!=v) throw std::runtime_error("Q1 profile type/value mismatch "+key);
 }
}
inline void CheckFinite(const Json& j) {
 if(j.is_number_float()&&!std::isfinite(j.get<double>())) throw std::runtime_error("Q1 nonfinite JSON number");
 if(j.is_structured()) for(const auto& v:j) CheckFinite(v);
}
inline Json Parse(std::string_view text,std::string_view source) {
 if(text.empty()||text.size()>65536) throw std::runtime_error("Q1 control size: "+std::string(source));
 std::vector<std::unordered_set<std::string>> stack; bool duplicate=false;
 Json::parser_callback_t cb=[&](int,Json::parse_event_t event,Json& value) {
  if(event==Json::parse_event_t::object_start) stack.emplace_back();
  else if(event==Json::parse_event_t::key) {if(stack.empty()||!stack.back().insert(value.get<std::string>()).second) duplicate=true;}
  else if(event==Json::parse_event_t::object_end) {if(!stack.empty()) stack.pop_back();}
  return true;
 };
 Json j=Json::parse(text.begin(),text.end(),cb,true,false);
 if(duplicate||!j.is_object()) throw std::runtime_error("Q1 duplicate JSON key/non-object: "+std::string(source));
 CheckFinite(j);return j;
}
inline std::string Validation(std::string_view mode) {return mode=="noop"?"quality_noop_full":"quality_nr_full";}
inline bool Allowed(std::string_view mode,unsigned count,std::string_view diagnostic) {
 return ((mode=="noop"||mode=="single")&&count==1&&diagnostic==mode)||
 (mode=="skipEvaluate"&&(count==1||count==4)&&diagnostic=="skipEvaluate")||
 (mode=="resident"&&count==4&&diagnostic=="smoke");
}
inline std::string FrameDir(const std::string& run,uint32_t frame,uint32_t count) {
 if((count!=1&&count!=4&&count!=120&&count!=1800)||frame>=count) throw std::runtime_error("Q1 frame resolver bounds");
 char bucket[32];std::snprintf(bucket,sizeof(bucket),"bucket-%06u",frame/64);
 return run+"/frames/"+bucket;
}
inline std::string FramePath(const std::string& run,uint32_t frame,uint32_t count,const std::string& basename) {
 if(basename.empty()||basename=="."||basename==".."||basename.find('/')!=std::string::npos||basename.find('\\')!=std::string::npos)
  throw std::runtime_error("Q1 frame basename invalid");
 return FrameDir(run,frame,count)+"/"+basename;
}
inline void Precreate(const std::string& run,uint32_t count) {
 for(uint32_t f=0;f<count;f+=64) {
  const auto path=FrameDir(run,f,count); std::filesystem::create_directories(path);
  for(const auto& dir:{run+"/frames",path}) {
   if(std::filesystem::is_symlink(dir)||!std::filesystem::is_directory(dir)||chmod(dir.c_str(),0700)!=0)
    throw std::runtime_error("Q1 private bucket unsafe: "+dir);
  }
 }
}
}
