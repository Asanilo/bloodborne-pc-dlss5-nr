#pragma once
// SPDX-License-Identifier: GPL-3.0-or-later
// True Pre-SR: original HDR720 Encode720 -> Evaluate720 -> Resolve720; Native SR1080 follows.
// Hot immutable-origin jobs only. No cache algorithm, CPU pixels, new API or jitter setter.
#include "runtime_types.h"
#include "lowres_production_adapter.h"
class PreSrFrame {
 public:
 using N=LowresProduction;
 using Evaluate=int(*)(void*,void*,void*,void*,void*,void*,void*,unsigned,unsigned,unsigned,unsigned,int,int,float,int,float,float,float,int,float,float);
 float model_intensity=1.f;unsigned model_style=0;
 N&n;N::Image full,raw,keep,dummy;N::Buffer enc,res;
 VkQueryPool queries{};uint32_t bits;double period,command_bound=0;uint64_t expected=0;bool recorded=false;
 unsigned evaluate_calls=0,resolve_calls=0;int evaluate_result=0;double evaluate_cpu_ms=0;
 PreSrFrame(N&adapter,uint32_t validbits,double ns):n(adapter),bits(validbits),period(ns){
  require(n.safe&&validbits<=64,"prior GPU and valid timestamp width");full=n.image(VK_FORMAT_R32G32B32A32_SFLOAT,false,1280,720);raw=n.image(VK_FORMAT_R16G16B16A16_SFLOAT,true,1280,720);keep=n.image(VK_FORMAT_R32G32B32A32_SFLOAT,true,1280,720);dummy=n.image(VK_FORMAT_R32G32B32A32_SFLOAT,true,1280,720);
  enc=n.buffer(sizeof(DlssNrConstants),VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT);res=n.buffer(sizeof(DlssNrConstants),VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT);
  if(bits){require(std::isfinite(ns)&&ns>0,"timestamp period");VkQueryPoolCreateInfo q{VK_STRUCTURE_TYPE_QUERY_POOL_CREATE_INFO};q.queryType=VK_QUERY_TYPE_TIMESTAMP;q.queryCount=6;check(n.api<PFN_vkCreateQueryPool>("vkCreateQueryPool")(n.device,&q,nullptr,&queries),"origin query pool");}
 }
 static N::Image wrap(VkImage image,VkImageView view,VkFormat format,bool writable,unsigned w,unsigned h){N::Image i{};i.handle=image;i.view=view;i.format=format;i.layout=VK_IMAGE_LAYOUT_GENERAL;i.ngx.Type=NVSDK_NGX_RESOURCE_VK_TYPE_VK_IMAGEVIEW;i.ngx.ReadWrite=writable;i.ngx.Resource.ImageViewInfo={view,image,{VK_IMAGE_ASPECT_COLOR_BIT,0,1,0,1},format,w,h};return i;}
 void stamp(unsigned k){if(queries)n.api<PFN_vkCmdWriteTimestamp>("vkCmdWriteTimestamp")(n.command,k?VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT:VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,queries,k);}
 void start(){require(n.safe&&!recorded,"no slot or descriptor reuse before prior fence+consumed");n.reset_frame_descriptors();require(n.serial<9007199254740991ull,"frame serial exhausted");expected=n.serial+1;evaluate_calls=resolve_calls=0;evaluate_result=0;evaluate_cpu_ms=0;if(queries)n.api<PFN_vkCmdResetQueryPool>("vkCmdResetQueryPool")(n.command,queries,0,6);stamp(0);}
 void record(N::Image&color,N::Image&depth,N::Image&mv,N::Image&output,bool skip,void*feature,void*params,Evaluate evaluate){
  require(n.safe&&feature&&params&&evaluate,"real live720 feature");
  auto dims=[](const N::Image&i,VkFormat f,unsigned w,unsigned h){auto v=i.ngx.Resource.ImageViewInfo;require(i.format==f&&v.Width==w&&v.Height==h,"exact actual shared role CI");};dims(color,VK_FORMAT_R32G32B32A32_SFLOAT,1280,720);dims(output,VK_FORMAT_R32G32B32A32_SFLOAT,1280,720);dims(depth,VK_FORMAT_R32_SFLOAT,1280,720);dims(mv,VK_FORMAT_R32G32_SFLOAT,1280,720);require(color.handle!=output.handle,"immutable origin Color != Output");
  n.transition(color,VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);n.transition(depth,VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);n.transition(mv,VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);n.clear(raw,NAN);n.clear(output,-1234.f);n.transition(full,VK_IMAGE_LAYOUT_GENERAL);n.transition(keep,VK_IMAGE_LAYOUT_GENERAL);n.transition(dummy,VK_IMAGE_LAYOUT_GENERAL);stamp(1);
  DlssNrConstants c{};c.Mode=DlssNrMode_Encode;c.Width=1280;c.Height=720;c.GuideWidth=1280;c.GuideHeight=720;c.WhitePoint=1;c.TransferStrength=c.ColourStrength=1;c.MaxRatio=2;c.Transfer=1;
  n.dispatch(c,color,color,color,color,full,keep,&enc);n.transition(full,VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);stamp(2);
  if(!skip){++evaluate_calls;auto t=c4_cpu_now_ms();evaluate_result=evaluate(n.command,feature,params,&full.ngx,&depth.ngx,&mv.ngx,&raw.ngx,1280,720,1280,720,0,1,model_intensity,int(model_style),1,1,1,1,1,1);evaluate_cpu_ms=c4_cpu_now_ms()-t;}stamp(3);n.memoryBarrier();
  if(!skip&&evaluate_result==1){n.transition(raw,VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);n.transition(keep,VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);c.Mode=DlssNrMode_Resolve;n.dispatch(c,full,raw,keep,mv,output,dummy,&res);++resolve_calls;}stamp(4);
  for(auto i:{&color,&depth,&mv,&output})n.transition(*i,VK_IMAGE_LAYOUT_GENERAL);recorded=true;
 }
 void finish(){require(recorded,"actual record before release stamp");stamp(5);}
 void complete(){require(recorded&&n.safe&&n.serial==expected,"exact current actual fence");recorded=false;}
 Json gpu_times(){require(n.safe&&n.serial==expected&&!recorded,"current query read after fence");Json j{{"status","NOT_AVAILABLE"},{"available",false},{"timestamp_valid_bits",bits},{"timestamp_period_ns",period},{"scope","same Helper queue commands/dependencies; Evaluate interval not pure inference"},{"query_count",6u},{"query_readback_bytes",queries?96u:0u},{"wait_bit",false}};const char*names[]={"acquire_poison_ms","encode720_ms","evaluate_commands_ms","resolve720_ms","release_ms"};for(auto k:names)j[k]=nullptr;j["total_ms"]=nullptr;if(!queries)return j;
  std::array<std::array<uint64_t,2>,6>q{};auto r=n.api<PFN_vkGetQueryPoolResults>("vkGetQueryPoolResults")(n.device,queries,0,6,sizeof q,q.data(),16,VK_QUERY_RESULT_64_BIT|VK_QUERY_RESULT_WITH_AVAILABILITY_BIT);j["vk_result"]=int(r);bool ready=r==VK_SUCCESS;Json availability=Json::array();for(auto a:q){ready=ready&&a[1]!=0;availability.push_back(a[1]!=0);}j["availability"]=availability;if(!ready)return j;
  double wrap=std::ldexp(period,int(bits))/1e6,bound=command_bound*1.01+1;j["wrap_proof"]={{"own_command_record_to_fence_QPC_upper_bound_ms",bound},{"wrap_period_ms",wrap},{"sufficient",bits==64||bound<wrap}};if(bits<64&&!(std::isfinite(bound)&&bound>=0&&bound<wrap)){j["status"]="NOT_MEASURED_WRAP_UNPROVEN";return j;}uint64_t mask=bits==64?UINT64_MAX:((uint64_t(1)<<bits)-1);for(unsigned k=0;k<5;k++)j[names[k]]=double((q[k+1][0]-q[k][0])&mask)*period/1e6;j["total_ms"]=double((q[5][0]-q[0][0])&mask)*period/1e6;j["available"]=true;j["status"]="MEASURED_SCOPED";return j;
 }
 Json evaluate_parameters()const{return Json{{"model_extent",{1280u,720u}},{"guide_extent",{1280u,720u}},{"color_format",109u},{"raw_format",97u},{"DepthInverted",0u},{"Reset",1u},{"Intensity",model_intensity},{"Style",model_style},{"LocalStructure",1.0},{"LocalTone",1.0},{"SkinStructure",1.0},{"AutoMask",1u},{"MVecScale",{1.0,1.0}},{"jitter_setter","NONE"}};}
 ~PreSrFrame(){if(n.safe&&queries)n.api<PFN_vkDestroyQueryPool>("vkDestroyQueryPool")(n.device,queries,nullptr);}
};
