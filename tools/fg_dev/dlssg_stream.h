// SPDX-License-Identifier: GPL-3.0-or-later
// CPU single-slot transport. GPU images are private to this process, never shared.
#include "../../gpu/upscale/dlssg_cpu_slot.h"
#include "dlssg_gpu_timing.h"
#include "dlssg_mailbox_export.h"
#include "../../gpu/upscale/dlssg_gpu_contract.h"
#include <sstream>
void Probe::stream(const char* root) {
  require(snippet&&feature&&create_result==1,"created FG feature for stream");
  using J=nlohmann::json;
  auto vt=*reinterpret_cast<void***>(params);
  auto ui=[&](const char* k,unsigned v){reinterpret_cast<void(__thiscall*)(void*,const char*,unsigned)>(vt[3])(params,k,v);};
  auto ull=[&](const char* k,unsigned long long v){reinterpret_cast<void(__thiscall*)(void*,const char*,unsigned long long)>(vt[0])(params,k,v);};
  auto ptr=[&](const char* k,const void* v){ull(k,reinterpret_cast<unsigned long long>(v));};
  auto fp=[&](const char* k,float v){reinterpret_cast<void(__thiscall*)(void*,const char*,float)>(vt[6])(params,k,v);};
  fp("bbport.FG.FloatProbe",.375f);alignas(16) unsigned char canary[64];memset(canary,0xa5,sizeof canary);
  int gr=reinterpret_cast<int(__thiscall*)(void*,const char*,void*)>(vt[14])(params,"bbport.FG.FloatProbe",canary+16);float fv=0;memcpy(&fv,canary+16,4);
  for(unsigned j=0;j<64;++j)require((j>=16&&j<20)||canary[j]==0xa5,"stream float ABI redzone");require(gr==1&&fv==.375f,"stream float ABI roundtrip");
  auto read_json=[&](const char* name){std::ifstream file(std::string(root)+"/"+name);require(bool(file),"stream metadata open");file.seekg(0,std::ios::end);auto size=file.tellg();require(size>0&&size<131072,"bounded stream metadata");file.seekg(0);J result;file>>result;return result;};
  auto exists=[&](const char* name){return GetFileAttributesA((std::string(root)+"/"+name).c_str())!=INVALID_FILE_ATTRIBUTES;};
  auto write_bytes=[&](const char* name,const void* data,size_t bytes){FILE* f=fopen((std::string(root)+"/"+name).c_str(),"wb");require(f,"stream output open");bool ok=fwrite(data,1,bytes,f)==bytes;ok=fclose(f)==0&&ok;require(ok,"stream output complete write");};
  auto read_bytes=[&](const char* name,void* data,size_t bytes){FILE* f=fopen((std::string(root)+"/"+name).c_str(),"rb");require(f,"stream input open");bool ok=fread(data,1,bytes,f)==bytes;ok=fgetc(f)==EOF&&ok;ok=fclose(f)==0&&ok;require(ok,"stream input exact length");};
  const auto config=read_json("stream-config.json");
  const bool gpu=config.value("transport",std::string{})=="gpu-dmabuf-v1";
  const std::string schema=gpu?DlssgGpu::Schema:"dlssg-cpu-stream-v1";
  require(config.at("schema")==schema,"stream config schema");
  const std::string transport=config.value("transport",std::string("files-v1"));
  require(transport=="files-v1"||transport=="mapped-v1"||gpu,"known CPU stream transport");
  const bool mapped=transport=="mapped-v1";DlssgCpu::MappedSlot payload;
  if(mapped)payload.Open(std::string(root)+"/slot.bin");
  const std::string session=config.at("session");require(!session.empty(),"stream session");
  auto proc_ticks=[](uint64_t pid){std::ifstream f("Z:/proc/"+std::to_string(pid)+"/stat");std::string t;std::getline(f,t);auto end=t.rfind(')');require(end!=std::string::npos,"GPU peer proc identity");std::istringstream in(t.substr(end+2));std::string v;for(unsigned k=0;k<=19;++k){require(bool(in>>v),"GPU peer proc fields");if(k==0)require(v!="Z"&&v!="X"&&v!="x","GPU peer live");if(k==19)return std::stoull(v);}return 0ull;};
  double peer_checked=-1000;uint64_t linux_exporter=0,exporter_ticks=0;
  auto check_peer=[&](){if(!gpu||cpu_ms()-peer_checked<250)return;require(proc_ticks(DlssgGpu::U(config,"native_pid"))==DlssgGpu::U(config,"native_start_ticks"),"GPU Native exact live process");if(linux_exporter)require(proc_ticks(linux_exporter)==exporter_ticks,"GPU Helper Linux identity");peer_checked=cpu_ms();};
  auto wait_gpu=[&](const char* name,auto accept){auto deadline=cpu_ms()+30000;while(cpu_ms()<deadline){check_peer();if(exists(name)){auto j=read_json(name);DlssgGpu::Session(j,session);if(accept(j))return j;}Sleep(1);}throw Stop(3,std::string("GPU bounded control timeout: ")+name);};
  std::unique_ptr<FgMailboxExport> mailbox;
  if(gpu){
    atomic_text(std::string(root)+"/gpu-pid-request.json",J{{"schema",schema},{"session",session},{"exe",module(GetModuleHandleA(nullptr))},{"win_pid",GetCurrentProcessId()}}.dump()+"\n");
    auto identity=wait_gpu("gpu-identity.json",[](const J&){return true;});
    require(identity.at("native_pid")==config.at("native_pid")&&identity.at("native_start_ticks")==config.at("native_start_ticks"),"GPU exact Native identity echo");linux_exporter=DlssgGpu::U(identity,"exporter_pid");exporter_ticks=DlssgGpu::U(identity,"exporter_start_ticks");check_peer();
    shared_active=true;mailbox=std::make_unique<FgMailboxExport>(instance,physical,device,queue,family,gipa,gdpa,command,fence);mailbox->session=session;mailbox->linux_pid=uint32_t(linux_exporter);
    require(config.at("device_uuid")==hex(mailbox->ids.deviceUUID)&&config.at("driver_uuid")==hex(mailbox->ids.driverUUID)&&DlssgGpu::U(config,"driver_version")==mailbox->properties.driverVersion,"GPU exact physical/driver match");
    mailbox->create_mailbox();mailbox->initial_release();auto meta=J::parse(mailbox->metadata());meta.update(J{{"schema",schema},{"initial_release_serial",mailbox->serial},{"driver_version",mailbox->properties.driverVersion}});
    atomic_text(std::string(root)+"/gpu-meta.json",meta.dump()+"\n");J pairs=J::array();for(const auto& im:mailbox->images)pairs.push_back({{"role",im.role},{"fd",im.fd}});
    auto imported=wait_gpu("gpu-imported.json",[](const J&){return true;});require(imported.at("raw_fd_pairs")==pairs&&DlssgGpu::U(imported,"initial_release_serial_echo")==mailbox->serial&&DlssgGpu::U(imported,"imported_images")==4&&DlssgGpu::U(imported,"owned_duplicate_fd")==0,"GPU exact import ACK");
    mailbox->retire_fds();atomic_text(std::string(root)+"/gpu-fd-retired.json",J{{"schema",schema},{"session",session},{"closed_fd",J::parse(mailbox->closed_fds)},{"export_raw_fd_owned",0}}.dump()+"\n");
  }

  fixture=std::make_unique<FgFixture>(device,physical,command,gdpa,i<PFN_vkGetPhysicalDeviceMemoryProperties>("vkGetPhysicalDeviceMemoryProperties"));auto& g=*fixture;
  constexpr unsigned W=1920,H=1080,RW=1280,RH=720;
  FgGpuTiming timing(device,physical,gdpa,i<PFN_vkGetPhysicalDeviceProperties>("vkGetPhysicalDeviceProperties"),i<PFN_vkGetPhysicalDeviceQueueFamilyProperties>("vkGetPhysicalDeviceQueueFamilyProperties"),family);
  struct Slot{FgFixture::Image color,depth,motion,output;FgFixture::Buffer color_upload,depth_upload,motion_upload,readback,disabled;};
  auto descriptors=std::make_shared<std::array<Slot,2>>();input_descriptors=descriptors;auto& slots=*descriptors;
  for(auto& s:slots){s.color=g.image(VK_FORMAT_R8G8B8A8_UNORM,false);s.depth=g.image(VK_FORMAT_R32_SFLOAT,false,RW,RH);s.motion=g.image(VK_FORMAT_R16G16_SFLOAT,false,RW,RH);s.output=g.image(VK_FORMAT_R8G8B8A8_UNORM,true);s.disabled=g.buffer(4,true);if(!gpu){s.color_upload=g.buffer(size_t(W)*H*4);s.depth_upload=g.buffer(size_t(RW)*RH*4);s.motion_upload=g.buffer(size_t(RW)*RH*4);s.readback=g.buffer(size_t(W)*H*4,true);}}
  for(auto k:{"DLSSG.MVecsSubrectWidth","DLSSG.DepthSubrectWidth","DLSSG.InternalWidth"})ui(k,RW);
  for(auto k:{"DLSSG.MVecsSubrectHeight","DLSSG.DepthSubrectHeight","DLSSG.InternalHeight"})ui(k,RH);
  for(auto k:{"DLSSG.InputBackbufferSubrectWidth","DLSSG.OutputInterpolatedSubrectWidth","DLSSG.BackbufferSubrectWidth"})ui(k,W);
  for(auto k:{"DLSSG.InputBackbufferSubrectHeight","DLSSG.OutputInterpolatedSubrectHeight","DLSSG.BackbufferSubrectHeight"})ui(k,H);
  for(auto k:{"DLSSG.MVecsSubrectBaseX","DLSSG.MVecsSubrectBaseY","DLSSG.DepthSubrectBaseX","DLSSG.DepthSubrectBaseY","DLSSG.InputBackbufferSubrectBaseX","DLSSG.InputBackbufferSubrectBaseY","DLSSG.OutputInterpolatedSubrectBaseX","DLSSG.OutputInterpolatedSubrectBaseY","DLSSG.BackbufferSubrectBaseX","DLSSG.BackbufferSubrectBaseY","DLSSG.ColorBuffersHDR","DLSSG.DepthInverted","DLSSG.NotRenderingGameFrames","DLSSG.OrthoProjection","DLSSG.MenuDetectionEnabled","DLSSG.MvecDilated","DLSSG.MvecJittered","DLSSG.AutomodeOverrideReset"})ui(k,0);
  ui("DLSSG.CameraMotionIncluded",1);ui("DLSSG.MultiFrameCount",1);ui("DLSSG.MultiFrameIndex",1);
  fp("DLSSG.MvecScaleX",1.f/RW);fp("DLSSG.MvecScaleY",1.f/RH);fp("DLSSG.CameraAspectRatio",float(W)/H);
  fp("DLSSG.CameraPinholeOffsetX",0);fp("DLSSG.CameraPinholeOffsetY",0);fp("DLSSG.MvecInvalidValue",3.402823466e38f);fp("DLSSG.MinRelativeLinearDepthObjectSeparation",40.f);
  for(auto k:{"DLSSG.HUDLess","DLSSG.UI","DLSSG.UIAlpha","DLSSG.OutputReal","DLSSG.BidirectionalDistortionField"})ptr(k,nullptr);
  std::array<float,16> identity{};for(unsigned k=0;k<4;++k)identity[k*5]=1;ptr("DLSSG.ClipToLensClip",identity.data());
  api(d<PFN_vkDeviceWaitIdle>("vkDeviceWaitIdle")(device),"stream initialization idle");
  atomic_text(std::string(root)+"/stream-ready.json",J{{"schema",schema},{"session",session},{"transport",transport},{"actual_GPU_complete",true},{"private_image_sets",2},{"native_gpu_readers",0},{"helper_serial",gpu?mailbox->serial:0}}.dump()+"\n");
  uint64_t sequence=0,last_source=0,epoch=0,last_native_tick=0;bool initialized[2]{};J last_request{{"schema",schema},{"session",session},{"sequence",0},{"epoch",0},{"source_game_frame",0},{"previous_source_game_frame",0},{"display",0}};
  while(true){
    check_peer();
    if(exists("stream-stop.json")){auto stop=read_json("stream-stop.json");require(stop.at("session")==session&&stop.at("sequence")==sequence,"stop exact consumed sequence");
      if(gpu){require(DlssgGpu::Pair(stop)==DlssgGpu::Pair(last_request)&&DlssgGpu::U(stop,"helper_serial_echo")==mailbox->serial,"GPU exact stop tuple");auto ready=DlssgGpu::Pair(stop);ready.update(J{{"helper_serial",mailbox->serial},{"helper_GPU_complete",true}});atomic_text(std::string(root)+"/gpu-stop-ready.json",ready.dump()+"\n");
        auto released=wait_gpu("gpu-native-released.json",[](const J&){return true;});require(DlssgGpu::Pair(released)==DlssgGpu::Pair(stop)&&DlssgGpu::B(released,"native_GPU_complete")&&DlssgGpu::U(released,"native_last_use_tick")>=last_native_tick&&DlssgGpu::U(released,"native_imported_images_owned")==0&&DlssgGpu::U(released,"native_duplicate_fd_owned")==0,"GPU Native imported images actually released");require(DlssgGpu::B(released,"native_shared_used")==bool(DlssgGpu::U(released,"native_last_use_tick")),"GPU zero-use or actual last-use proof");last_native_tick=DlssgGpu::U(released,"native_last_use_tick");mailbox->native_released=true;mailbox->destroy_released();shared_active=false;
      }break;}

    if(!exists("request.json")){Sleep(1);continue;}
    auto request=read_json("request.json");uint64_t next=request.at("sequence");
    if(gpu)DlssgGpu::Session(request,session);
    if(next==sequence){Sleep(1);continue;}
    require(next==sequence+1&&request.at("schema")==schema&&request.at("session")==session,"request exact session/sequence");
    uint64_t source=request.at("source_game_frame"),previous=request.at("previous_source_game_frame"),generation=request.at("epoch");bool reset=request.at("reset");
    require(source>last_source&&previous==last_source&&generation>0&&((generation==epoch&&source==last_source+1&&!reset)||(generation>epoch&&reset)),"real source pair/epoch/reset contract");
    require(request.value("transport",std::string("files-v1"))==transport,"exact CPU transport request");
    if(gpu)DlssgGpu::Request(request,session,sequence,epoch,last_source,last_native_tick);
    const double started=cpu_ms();auto& s=slots[sequence%2];
    if(gpu){}else if(mapped){
      payload.Acquire();
      memcpy(s.color_upload.mapped,payload.At(DlssgCpu::ColorOffset,DlssgCpu::ColorBytes),DlssgCpu::ColorBytes);
      memcpy(s.depth_upload.mapped,payload.At(DlssgCpu::DepthOffset,DlssgCpu::GuideBytes),DlssgCpu::GuideBytes);
      memcpy(s.motion_upload.mapped,payload.At(DlssgCpu::MotionOffset,DlssgCpu::GuideBytes),DlssgCpu::GuideBytes);
    }else{read_bytes("input-color.raw",s.color_upload.mapped,size_t(W)*H*4);read_bytes("input-depth.raw",s.depth_upload.mapped,size_t(RW)*RH*4);read_bytes("input-motion.raw",s.motion_upload.mapped,size_t(RW)*RH*4);}
    const auto input_copied=cpu_ms();
    std::array<std::array<float,16>,4> matrices{};const auto& m=request.at("camera");
    const char* source_keys[]={"camera_view_to_clip","clip_to_camera_view","clip_to_previous_clip","previous_clip_to_clip"};const char* target_keys[]={"DLSSG.CameraViewToClip","DLSSG.ClipToCameraView","DLSSG.ClipToPrevClip","DLSSG.PrevClipToClip"};
    for(unsigned k=0;k<4;++k){matrices[k]=m.at(source_keys[k]).get<std::array<float,16>>();for(float v:matrices[k])require(std::isfinite(v),"finite camera matrix");ptr(target_keys[k],matrices[k].data());}
    const char* vector_keys[]={"camera_position","camera_up","camera_right","camera_forward"};const char* bases[]={"DLSSG.CameraPos","DLSSG.CameraUp","DLSSG.CameraRight","DLSSG.CameraFwd"};
    for(unsigned k=0;k<4;++k)for(unsigned j=0;j<3;++j){std::string key=std::string(bases[k])+"XYZ"[j];float v=m.at(vector_keys[k]).at(j);require(std::isfinite(v),"finite camera vector");fp(key.c_str(),v);}
    fp("DLSSG.CameraNear",m.at("near"));fp("DLSSG.CameraFar",m.at("far"));fp("DLSSG.CameraFOV",m.at("vertical_fov"));fp("DLSSG.JitterOffsetX",m.at("jitter").at(0));fp("DLSSG.JitterOffsetY",m.at("jitter").at(1));
    ull("DLSSG.BackbufferFrameID",source);ui("DLSSG.Reset",reset);
    ptr("DLSSG.Backbuffer",&s.color.ngx);ptr("DLSSG.Depth",&s.depth.ngx);ptr("DLSSG.MVecs",&s.motion.ngx);ptr("DLSSG.OutputInterpolated",&s.output.ngx);ptr("DLSSG.OutputDisableInterpolation",&s.disabled.ngx);
    api(d<PFN_vkResetCommandBuffer>("vkResetCommandBuffer")(command,0),"stream reset command");VkCommandBufferBeginInfo begin{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};api(d<PFN_vkBeginCommandBuffer>("vkBeginCommandBuffer")(command,&begin),"stream begin");
    timing.Mark(command,0);if(gpu){mailbox->copy_to_private(g,s.color,0);mailbox->copy_to_private(g,s.depth,1);mailbox->copy_to_private(g,s.motion,2);}else{g.upload(s.color,s.color_upload);g.upload(s.depth,s.depth_upload);g.upload(s.motion,s.motion_upload);}
    g.barrier(s.output,initialized[sequence%2]?VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL:VK_IMAGE_LAYOUT_UNDEFINED,VK_IMAGE_LAYOUT_GENERAL);
    d<PFN_vkCmdFillBuffer>("vkCmdFillBuffer")(command,s.disabled.buffer,0,4,0xffffffffu);
    VkMemoryBarrier memory{VK_STRUCTURE_TYPE_MEMORY_BARRIER};memory.srcAccessMask=VK_ACCESS_MEMORY_WRITE_BIT;memory.dstAccessMask=VK_ACCESS_MEMORY_READ_BIT|VK_ACCESS_MEMORY_WRITE_BIT;d<PFN_vkCmdPipelineBarrier>("vkCmdPipelineBarrier")(command,VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,0,1,&memory,0,nullptr,0,nullptr);
    timing.Mark(command,1);const auto evaluate_record_start=cpu_ms();
    const int result=sym<int(*)(void*,const void*,const void*)>(bridge,"fg_evaluate")(command,feature,params);
    const auto evaluate_record_ms=cpu_ms()-evaluate_record_start;timing.Mark(command,2);
    if(gpu)mailbox->copy_output(g,s.output);else g.readback(s.output,s.readback);g.host(s.disabled,false);timing.Mark(command,3);api(d<PFN_vkEndCommandBuffer>("vkEndCommandBuffer")(command),"stream end");api(d<PFN_vkResetFences>("vkResetFences")(device,1,&fence),"stream reset fence");VkSubmitInfo submit{VK_STRUCTURE_TYPE_SUBMIT_INFO};submit.commandBufferCount=1;submit.pCommandBuffers=&command;
    const auto submit_start=cpu_ms();inflight=true;g.safe=false;api(d<PFN_vkQueueSubmit>("vkQueueSubmit")(queue,1,&submit,fence),"stream submit");const auto wait_start=cpu_ms();api(d<PFN_vkWaitForFences>("vkWaitForFences")(device,1,&fence,VK_TRUE,10000000000ull),"stream actual GPU fence");const auto idle_start=cpu_ms();if(!gpu)api(d<PFN_vkDeviceWaitIdle>("vkDeviceWaitIdle")(device),"stream actual device idle");const auto idle_end=cpu_ms();inflight=false;g.safe=true;timing.safe=true;if(gpu)++mailbox->serial;initialized[sequence%2]=true;
    unsigned disabled=0;memcpy(&disabled,s.disabled.mapped,4);bool eligible=result==1&&(disabled&255)==0;
    const auto gpu_completed=cpu_ms();
    if(eligible&&!gpu){if(mapped){memcpy(payload.At(DlssgCpu::OutputOffset,DlssgCpu::ColorBytes),s.readback.mapped,DlssgCpu::ColorBytes);payload.Publish();}else write_bytes("output-color.raw",s.readback.mapped,size_t(W)*H*4);}
    J response=request;response.erase("camera");response.update(J{{"result",unsigned(result)},{"disable_word",disabled},{"eligible",eligible},{"actual_GPU_complete",true},{"output_bytes",eligible?size_t(W)*H*4:0},{"input_copy_ms",input_copied-started},{"record_fence_idle_ms",gpu_completed-input_copied},{"output_copy_ms",cpu_ms()-gpu_completed},{"helper_service_ms",cpu_ms()-started},{"native_gpu_readers",0}});
    if(gpu){response.erase("native_gpu_readers");response.update(J{{"helper_GPU_complete",true},{"helper_serial",mailbox->serial},{"output_released",true},{"pixel_host_transfer_bytes",0}});}
    if(timing.pool){response.update(timing.Read());response.update(J{{"evaluate_record_cpu_ms",evaluate_record_ms},{"submit_cpu_ms",wait_start-submit_start},{"fence_wait_cpu_ms",idle_start-wait_start},{"device_idle_cpu_ms",idle_end-idle_start}});}
    atomic_text(std::string(root)+"/response.json",response.dump()+"\n");
    // Do not overwrite the CPU response until its exact consumer ACK (copied or discarded).
    const auto consume_deadline=cpu_ms()+30000;
    while(true){check_peer();if(gpu)require(cpu_ms()<consume_deadline,"GPU consumed timeout; HOLD");if(exists("consumed.json")){auto ack=read_json("consumed.json");if(gpu)DlssgGpu::Session(ack,session);if(ack.at("sequence")==next){if(gpu){DlssgGpu::Consumed(ack,request,mailbox->serial);last_native_tick=DlssgGpu::U(ack,"native_last_use_tick");}require(ack.at("session")==session&&ack.at("epoch")==generation&&ack.at("source_game_frame")==source,"exact CPU consumer ACK");break;}}Sleep(1);}
    sequence=next;last_source=source;epoch=generation;last_request=request;
    if(sequence<=3||sequence%60==0)printf("DLSSG_STREAM_EVALUATED sequence=%llu source=%llu previous=%llu epoch=%llu result=0x%08x disable=%u service_ms=%.3f GPU_complete=1 native_presents=0\n",sequence,source,previous,epoch,unsigned(result),disabled,cpu_ms()-started);
  }
  printf("DLSSG_STREAM_DRAINED session=%s sequence=%llu source=%llu cpu_slot_consumed=1 submitted_queue_GPU_complete=1 native_gpu_readers=0\n",session.c_str(),sequence,last_source);
  if(gpu){stream_terminal=DlssgGpu::Pair(last_request);stream_terminal.update(J{{"helper_serial",mailbox->serial},{"native_last_use_tick",last_native_tick},{"native_imports_released",true},{"shared_images_owned",0},{"export_raw_fd_owned",0}});}
  else stream_terminal=J{{"schema","dlssg-cpu-stream-v1"},{"session",session},{"sequence",sequence},{"epoch",epoch},{"source_game_frame",last_source},{"cpu_slot_consumed",true}};
  // Main performs real Release/Shutdown/device destruction before writing the terminal record.
}
