// SPDX-License-Identifier: GPL-3.0-or-later
// Queue timestamp intervals, not a claim to measure internal CUDA/OFA alone.
#pragma once
struct FgGpuTiming {
  VkDevice device{};VkQueryPool pool{};PFN_vkGetDeviceProcAddr gdpa{};
  double period{};unsigned bits{};bool safe=true;
  template<class T>T fn(const char* n){return reinterpret_cast<T>(gdpa(device,n));}
  FgGpuTiming(VkDevice dev,VkPhysicalDevice physical,PFN_vkGetDeviceProcAddr get,
              PFN_vkGetPhysicalDeviceProperties props,PFN_vkGetPhysicalDeviceQueueFamilyProperties families,unsigned family)
      :device(dev),gdpa(get){
    if(!getenv("BB_FG_PROFILE"))return;
    VkPhysicalDeviceProperties p{};props(physical,&p);period=p.limits.timestampPeriod;
    unsigned count=0;families(physical,&count,nullptr);std::vector<VkQueueFamilyProperties> q(count);families(physical,&count,q.data());
    require(family<count&&q[family].timestampValidBits>0,"FG queue supports timestamps");bits=q[family].timestampValidBits;
    VkQueryPoolCreateInfo ci{VK_STRUCTURE_TYPE_QUERY_POOL_CREATE_INFO};ci.queryType=VK_QUERY_TYPE_TIMESTAMP;ci.queryCount=4;
    api(fn<PFN_vkCreateQueryPool>("vkCreateQueryPool")(device,&ci,nullptr,&pool),"FG profiling query pool");
  }
  ~FgGpuTiming(){if(pool&&!safe){std::fprintf(stderr,"FG_TIMING_HOLD query_GPU_completion_unknown=1\n");return;}if(pool)fn<PFN_vkDestroyQueryPool>("vkDestroyQueryPool")(device,pool,nullptr);}
  void Mark(VkCommandBuffer cmd,unsigned index){if(!pool)return;if(index==0)safe=false;if(index==0)fn<PFN_vkCmdResetQueryPool>("vkCmdResetQueryPool")(cmd,pool,0,4);fn<PFN_vkCmdWriteTimestamp>("vkCmdWriteTimestamp")(cmd,VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,pool,index);}
  nlohmann::json Read(){
    if(!pool)return nlohmann::json::object();
    uint64_t t[4]{};api(fn<PFN_vkGetQueryPoolResults>("vkGetQueryPoolResults")(device,pool,0,4,sizeof t,t,sizeof(uint64_t),VK_QUERY_RESULT_64_BIT),"FG profiling actual-completed timestamps");
    const uint64_t mask=bits==64?~uint64_t(0):(uint64_t(1)<<bits)-1;
    auto ms=[&](unsigned a,unsigned b){return double((t[b]-t[a])&mask)*period/1e6;};
    return {{"input_transfer_gpu_ms",ms(0,1)},{"evaluate_queue_gpu_ms",ms(1,2)},{"output_transfer_gpu_ms",ms(2,3)},
            {"gpu_timing_scope","Wine Vulkan queue intervals; internal CUDA/OFA not separately isolated"}};
  }
};
