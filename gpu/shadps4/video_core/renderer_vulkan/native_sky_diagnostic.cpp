// SPDX-License-Identifier: GPL-2.0-or-later
#include "native_sky_diagnostic.h"
#include "vk_instance.h"
#include "vk_scheduler.h"
#include <nlohmann/json.hpp>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <stdexcept>
namespace Vulkan {
bool NativeSkyDiagnostic::Enabled()noexcept{
 const char*d=std::getenv("BB_NR_NATIVE_ASYNC_DIAGNOSTIC_ONCE");
 const char*m=std::getenv("BB_NR_NATIVE_ASYNC_MODE");
 return d&&m&&!std::strcmp(d,"1")&&!std::strcmp(m,"localnoop");
}
NativeSkyDiagnostic::NativeSkyDiagnostic(const Instance&i){
 const char*r=std::getenv("BB_NR_NATIVE_ASYNC_RUN_DIR"),*s=std::getenv("BB_NR_NATIVE_ASYNC_SESSION");
 if(!r||!s)throw std::invalid_argument("Native cold diagnostic private directory");
 run=r;session=s;
 if(run!="/home/arinp/Code/nrbridge-design/game-poc-20261011/bbport/runs/native-async-game-"+session)
  throw std::invalid_argument("Native cold diagnostic private root");
 for(auto&b:buffers)b=std::make_unique<VideoCore::Buffer>(i,0,33177600,
  VideoCore::MemoryType::HostCached,"Native Sky ONCE local-noop source/composite audit");
 writer=std::jthread([this](std::stop_token s){Run(s);});
}
NativeSkyDiagnostic::~NativeSkyDiagnostic(){writer.request_stop();if(writer.joinable())writer.join();}
void NativeSkyDiagnostic::RecordOnce(Scheduler&s,vk::Image raw,vk::Image composite,const BbNativeAsync::Frame&f){
 if(recorded.exchange(true))return;
 frame=f;target_tick=s.CurrentTick();
 auto cmd=s.CommandBuffer();
 const std::array images{raw,composite};
 for(unsigned k=0;k<2;k++){
  const vk::ImageMemoryBarrier2 b{.srcStageMask=vk::PipelineStageFlagBits2::eComputeShader,
   .srcAccessMask=vk::AccessFlagBits2::eShaderWrite,.dstStageMask=vk::PipelineStageFlagBits2::eTransfer,
   .dstAccessMask=vk::AccessFlagBits2::eTransferRead,.oldLayout=vk::ImageLayout::eGeneral,
   .newLayout=vk::ImageLayout::eGeneral,.srcQueueFamilyIndex=VK_QUEUE_FAMILY_IGNORED,
   .dstQueueFamilyIndex=VK_QUEUE_FAMILY_IGNORED,.image=images[k],
   .subresourceRange={vk::ImageAspectFlagBits::eColor,0,1,0,1}};
  cmd.pipelineBarrier2({.imageMemoryBarrierCount=1,.pImageMemoryBarriers=&b});
  const vk::BufferImageCopy c{.imageSubresource={vk::ImageAspectFlagBits::eColor,0,0,1},
   .imageExtent={1920,1080,1}};
  cmd.copyImageToBuffer(images[k],vk::ImageLayout::eGeneral,buffers[k]->Handle(),c);
  const vk::BufferMemoryBarrier2 host{.srcStageMask=vk::PipelineStageFlagBits2::eTransfer,
   .srcAccessMask=vk::AccessFlagBits2::eTransferWrite,.dstStageMask=vk::PipelineStageFlagBits2::eHost,
   .dstAccessMask=vk::AccessFlagBits2::eHostRead,.srcQueueFamilyIndex=VK_QUEUE_FAMILY_IGNORED,
   .dstQueueFamilyIndex=VK_QUEUE_FAMILY_IGNORED,.buffer=buffers[k]->Handle(),.offset=0,.size=33177600};
  auto restore=b;restore.srcStageMask=vk::PipelineStageFlagBits2::eTransfer;
  restore.srcAccessMask=vk::AccessFlagBits2::eTransferRead;
  restore.dstStageMask=vk::PipelineStageFlagBits2::eComputeShader;
  restore.dstAccessMask=vk::AccessFlagBits2::eShaderRead;
  cmd.pipelineBarrier2({.bufferMemoryBarrierCount=1,.pBufferMemoryBarriers=&host,
    .imageMemoryBarrierCount=1,.pImageMemoryBarriers=&restore});
 }
 // Registration uses the existing open target. Subsequent merge is recorded to
 // that target; callback means actual completion, not command-recording ready.
 s.DeferPriorityOperation([hold=shared_from_this()]{hold->gpu_ready.store(true,std::memory_order_release);});
 std::printf("NATIVE_SKY_DIAGNOSTIC_RECORDED session=%s source=%llu epoch=%llu target_tick=%llu readback_bytes=66355200 once=1 extra_flush=0\n",
  session.c_str(),(unsigned long long)frame.source,(unsigned long long)frame.epoch,(unsigned long long)target_tick);
}
void NativeSkyDiagnostic::Run(std::stop_token stop){
 while(!stop.stop_requested()&&!gpu_ready.load(std::memory_order_acquire))
  std::this_thread::sleep_for(std::chrono::milliseconds(10));
 if(!gpu_ready.load(std::memory_order_acquire))return;
 try{
  const std::string dir=run+"/native-sky-once";std::filesystem::create_directory(dir);
  const char*names[]{"source.rgba32f","composite.rgba32f"};
  for(unsigned k=0;k<2;k++){
   buffers[k]->Invalidate(0,33177600);
   std::ofstream f(dir+"/"+names[k],std::ios::binary|std::ios::trunc);
   f.write(reinterpret_cast<const char*>(buffers[k]->mapped_data.data()),33177600);
   if(!f)throw std::runtime_error("cold native audit local write failed");
  }
  nlohmann::json j{{"session",session},{"source_game_frame",frame.source},{"origin_source",frame.source},
   {"origin_epoch",frame.epoch},{"origin_history",frame.history},{"params_version",frame.params},
   {"native_target_tick",target_tick},{"actual_native_GPU_complete",true},{"same_frame_merge_tick",target_tick},
   {"format",109u},{"width",1920u},{"height",1080u},{"bytes_per_image",33177600u},
   {"source_edited",false},{"mode","localnoop"},{"model_calls",0u},{"phase","UNVERIFIED"},
   {"Present_proof","REQUIRES_UNIQUE_NATIVE_PRESENT_LEDGER"}};
  std::ofstream meta(dir+"/readback.json.tmp");meta<<j.dump(2)<<"\n";meta.close();
  if(!meta)throw std::runtime_error("cold native metadata write failed");
  std::filesystem::rename(dir+"/readback.json.tmp",dir+"/readback.json");
  std::printf("NATIVE_SKY_DIAGNOSTIC_COMPLETE source=%llu actual_native_tick=%llu files_bytes=66355200 Present_proof=REQUIRES_LEDGER\n",
   (unsigned long long)frame.source,(unsigned long long)target_tick);
 }catch(const std::exception&e){std::fprintf(stderr,"NATIVE_SKY_DIAGNOSTIC_FAILED reason=%s\n",e.what());}
}
}
