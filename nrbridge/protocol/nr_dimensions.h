// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <cstdint>
#include <cstdlib>
#include <stdexcept>
#include <string>

namespace BbNRDimensions {
struct Contract {
 uint32_t width=1280,height=720,output_width=1920,output_height=1080;
 bool enabled=false;
 uint64_t Pixels()const{return uint64_t(width)*height;}
 // 8 GiB profile: bounded NR working set; model allocations are measured independently.
 uint64_t EstimatedImageBytes()const{return Pixels()*768;}
 bool Valid()const{return width>=320&&height>=180&&width<=4096&&height<=4096&&!(width%2)&&!(height%2)&&Pixels()<=uint64_t(1920)*1080&&output_width>=640&&output_height>=360&&output_width<=7680&&output_height<=4320&&!(output_width%2)&&!(output_height%2);}
 bool operator==(const Contract& b)const{return width==b.width&&height==b.height&&output_width==b.output_width&&output_height==b.output_height&&enabled==b.enabled;}
};
inline uint32_t Positive(const char* value){
 if(!value||!*value)throw std::invalid_argument("NR dimension missing");
 uint64_t n=0;for(const char*p=value;*p;++p){if(*p<'0'||*p>'9')throw std::invalid_argument("NR dimension must be decimal");n=n*10+(*p-'0');if(n>7680)throw std::invalid_argument("NR dimension bound");}return uint32_t(n);
}
inline const Contract& Get(){
 static const Contract c=[](){Contract d;const char* v=std::getenv("BB_NR_DIMENSIONS_V3");if(!v)return d;if(std::string(v)!="1")throw std::invalid_argument("NR dimensions v3 opt-in");d.enabled=true;d.width=Positive(std::getenv("BB_NR_MODEL_WIDTH"));d.height=Positive(std::getenv("BB_NR_MODEL_HEIGHT"));d.output_width=Positive(std::getenv("BB_NR_SR_OUTPUT_WIDTH"));d.output_height=Positive(std::getenv("BB_NR_SR_OUTPUT_HEIGHT"));if(!d.Valid())throw std::invalid_argument("NR dimensions exceed validated format/8GiB working-set bounds");return d;}();return c;
}
inline unsigned Width(){return Get().width;}
inline unsigned Height(){return Get().height;}
inline unsigned GroupsX(){return (Width()+7)/8;}
inline unsigned GroupsY(){return (Height()+7)/8;}
inline constexpr const char* RevisionMarker="NRBRIDGE_DIMENSIONS_V3R2";
template<class J>inline J SourceLayout(uint32_t rw,uint32_t rh,uint32_t ow,uint32_t oh){
 return J{{"render_subrect_width",rw},{"render_subrect_height",rh},{"execution_width",Width()},{"execution_height",Height()},{"sr_output_width",ow},{"sr_output_height",oh},{"mvec_domain","execution_pixels_previous_minus_current"}};
}
template<class J>inline void CheckSourceLayout(const J& j){
 if(!j.is_object()||j.size()!=7)throw std::invalid_argument("NR exact source subrect layout");
 for(const char*k:{"render_subrect_width","render_subrect_height","execution_width","execution_height","sr_output_width","sr_output_height"}){
  if(!j.contains(k)||!j.at(k).is_number_unsigned()||j.at(k)==0u||j.at(k)>7680u)throw std::invalid_argument("NR source subrect bounds");
 }
 if(j.at("execution_width")!=Width()||j.at("execution_height")!=Height()||!j.contains("mvec_domain")||j.at("mvec_domain")!="execution_pixels_previous_minus_current")throw std::invalid_argument("NR execution/source pixel domain mismatch");
}
template<class Json>inline Json Spec(Json j){
 const auto& d=Get();if(!d.enabled)return j;
 if(!d.Valid())throw std::invalid_argument("invalid NR extent contract");
 j["profile_id"]="async-pre-sr-dimensions-v3-"+std::to_string(d.width)+"x"+std::to_string(d.height)+"-sr-variable-r2-initial"+std::to_string(d.output_width)+"x"+std::to_string(d.output_height);
 auto& p=j["quality_profile"];
 for(const char*k:{"color_width","guide_width","model_width","output_width"})p[k]=d.width;
 for(const char*k:{"color_height","guide_height","model_height","output_height"})p[k]=d.height;
 p["source_layout_policy"]="per-job actual render subrect and SR output; layout changes reset Native history";
 p["stored_mv_units"]="execution_pixels_previous_minus_current";
 p["source_scale_policy"]="resample render pixels to execution pixels once; NGX MVecScale remains 1/1";
 p["sr_output_width"]=d.output_width;p["sr_output_height"]=d.output_height;
 p["model_input"]="Encode actual immutable HDR NR execution extent";
 p["resolved_output"]="Resolve actual NR execution extent before Native DLSS SR";
 auto& e=j["bootstrap_exact"]["async-config.json"]["exact_sample"];
 for(const char*k:{"width","render_width","model_width"})e[k]=d.width;
 for(const char*k:{"height","render_height","model_height"})e[k]=d.height;
 e["protocol_version"]=3u;e["profile_id"]=j["profile_id"];e["quality_profile"]=p;
 for(auto& r:j["shared_images"]["roles"])r["extent"]={d.width,d.height};
 return j;
}
inline unsigned Protocol(bool daily){return Get().enabled?3u:daily?2u:1u;}
}
