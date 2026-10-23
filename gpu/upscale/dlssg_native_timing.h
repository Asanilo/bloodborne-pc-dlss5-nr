// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <chrono>
#include "vulkan/instance.h"
namespace Vulkan {
struct DlssgNativeTiming {
    using Clock=std::chrono::steady_clock;
    static double Ms(Clock::time_point a,Clock::time_point b){return std::chrono::duration<double,std::milli>(b-a).count();}
    vk::Device device;vk::UniqueQueryPool pool;double period=0;unsigned bits=0;
    u64 source=0,upload_source=0;bool upload_pending=false;Clock::time_point guides{};
    explicit DlssgNativeTiming(const Instance& i):device(i.GetDevice()){
        if(!std::getenv("BB_FG_PROFILE"))return;
        const auto q=i.GetPhysicalDevice().getQueueFamilyProperties();bits=q.at(i.GetGraphicsQueueFamilyIndex()).timestampValidBits;
        if(!bits)throw std::runtime_error("Native FG profiling requires timestamps");
        period=i.GetPhysicalDevice().getProperties().limits.timestampPeriod;
        pool=Check<"FG timing pool">(device.createQueryPoolUnique({.queryType=vk::QueryType::eTimestamp,.queryCount=6}));
    }
    void Mark(vk::CommandBuffer cmd,unsigned index){if(!pool)return;if(index==0)cmd.resetQueryPool(*pool,0,4);if(index==4)cmd.resetQueryPool(*pool,4,2);cmd.writeTimestamp2(vk::PipelineStageFlagBits2::eAllCommands,*pool,index);}
    double Delta(u64 a,u64 b)const{const u64 mask=bits==64?~u64(0):(u64(1)<<bits)-1;return double((b-a)&mask)*period/1e6;}
    // Service worker only, after the exact input tick and BEFORE publishing the
    // consumer submission prerequisite. The preceding consumer is earlier on this same draw
    // timeline; the current consumer cannot reset 4/5 until we publish the response.
    void ReadAsyncInput(u64 input_source,u64 previous_output,double record_ms,
                        double flush_ms,double wait_ms,const char* pipeline) const {
        if(!pool)return;
        u64 t[4]{};
        Check<"FG async completed inputs">(device.getQueryPoolResults(*pool,0,4,sizeof t,t,sizeof(u64),vk::QueryResultFlagBits::e64));
        if(input_source%31==0)
            std::printf("DLSSG_GPU_INPUT_PROFILE source=%llu guides_copy_gpu_ms=%.3f guides_to_final_color_gpu_ms=%.3f color_copy_gpu_ms=%.3f record_cpu_ms=%.3f producer_flush_cpu_ms=%.3f producer_completion_wait_cpu_ms=%.3f pipeline=%s\n",
                (unsigned long long)input_source,Delta(t[0],t[1]),Delta(t[1],t[2]),Delta(t[2],t[3]),record_ms,flush_ms,wait_ms,pipeline);
        if(previous_output){u64 u[2]{};
            Check<"FG async preceding completed output">(device.getQueryPoolResults(*pool,4,2,sizeof u,u,sizeof(u64),vk::QueryResultFlagBits::e64));
            if(previous_output%31==0)
                std::printf("DLSSG_GPU_OUTPUT_COPY_PROFILE source=%llu GPU_copy_ms=%.3f actual_GPU_complete=1\n",(unsigned long long)previous_output,Delta(u[0],u[1]));
        }
    }
    void ReadInput(double record_ms,double finish_ms,double invalidate_ms,bool gpu_mailbox=false){
        if(!pool)return;u64 t[4]{};
        Check<"FG input completed queries">(device.getQueryPoolResults(*pool,0,4,sizeof t,t,sizeof(u64),vk::QueryResultFlagBits::e64));
        if(source%31==0&&gpu_mailbox)std::printf("DLSSG_GPU_INPUT_PROFILE source=%llu guides_copy_gpu_ms=%.3f guides_to_final_color_gpu_ms=%.3f color_copy_gpu_ms=%.3f record_cpu_ms=%.3f producer_finish_cpu_ms=%.3f\n",(unsigned long long)source,Delta(t[0],t[1]),Delta(t[1],t[2]),Delta(t[2],t[3]),record_ms,finish_ms);
        if(source%31==0&&!gpu_mailbox)std::printf("DLSSG_INPUT_PROFILE source=%llu guides_gpu_ms=%.3f guides_to_final_color_gpu_ms=%.3f color_readback_gpu_ms=%.3f final_record_cpu_ms=%.3f readback_finish_cpu_ms=%.3f invalidate_cpu_ms=%.3f guides_to_color_cpu_ms=%.3f\n",(unsigned long long)source,Delta(t[0],t[1]),Delta(t[1],t[2]),Delta(t[2],t[3]),record_ms,finish_ms,invalidate_ms,Ms(guides,Clock::now()));
        if(upload_pending){u64 u[2]{};Check<"FG output completed queries">(device.getQueryPoolResults(*pool,4,2,sizeof u,u,sizeof(u64),vk::QueryResultFlagBits::e64));if(upload_source%31==0)std::printf("%s source=%llu %s=%.3f actual_GPU_complete=1\n",gpu_mailbox?"DLSSG_GPU_OUTPUT_COPY_PROFILE":"DLSSG_UPLOAD_PROFILE",(unsigned long long)upload_source,gpu_mailbox?"GPU_copy_ms":"GPU_upload_ms",Delta(u[0],u[1]));upload_pending=false;}
    }
};
} // namespace Vulkan
