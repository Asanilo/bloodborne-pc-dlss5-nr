// SPDX-License-Identifier: GPL-3.0-or-later
// Native DMA-BUF import follows native_async_pre_sr_origin_owner.cpp. Explicit
// Destroy only after the caller has observed all recorded GPU uses complete.
#pragma once
#include <vulkan/vulkan.h>
#include <filesystem>
#include <fstream>
#include <set>
#include <sstream>
#include <sys/stat.h>
#include <sys/syscall.h>
#include <unistd.h>
#include "dlssg_gpu_contract.h"
namespace DlssgGpu {
inline std::string Proc(const std::string& path){std::ifstream f(path,std::ios::binary);return {std::istreambuf_iterator<char>(f),{}};}
inline uint64_t Ticks(pid_t pid){auto t=Proc("/proc/"+std::to_string(pid)+"/stat");auto p=t.rfind(')');if(p==std::string::npos)return 0;std::istringstream in(t.substr(p+2));std::string v;for(unsigned k=0;k<=19;++k){if(!(in>>v))return 0;if(k==0&&(v=="Z"||v=="X"||v=="x"))return 0;if(k==19)return std::stoull(v);}return 0;}
inline std::string Hex(const uint8_t* data){static constexpr char h[]="0123456789abcdef";std::string s;for(unsigned k=0;k<16;++k){s+=h[data[k]>>4];s+=h[data[k]&15];}return s;}
struct FdPayload{uint64_t dev=0,ino=0;std::string target;bool operator==(const FdPayload& o)const{return dev==o.dev&&ino==o.ino&&target==o.target;}};
inline FdPayload Payload(pid_t pid,int fd){const auto path="/proc/"+std::to_string(pid)+"/fd/"+std::to_string(fd);std::array<char,4096> b{};struct stat st{};auto n=readlink(path.c_str(),b.data(),b.size()-1);Need(n>0&&stat(path.c_str(),&st)==0,"GPU FD payload readable");return {uint64_t(st.st_dev),uint64_t(st.st_ino),std::string(b.data(),n)};}
struct MailboxImport {
    VkInstance instance{};VkPhysicalDevice physical{};VkDevice device{};uint32_t family{};
    PFN_vkGetInstanceProcAddr gipa{};PFN_vkGetDeviceProcAddr gdpa{};
    struct Image{VkImage image{};VkDeviceMemory memory{};VkFormat format{};VkExtent3D extent{};};
    std::array<Image,4> images{};std::vector<FdPayload> raw_payloads;bool destroyed=false,used=false,complete=true;
    uint64_t last_use=0;pid_t exporter=-1;uint64_t exporter_ticks=0;
    MailboxImport(VkInstance in,VkPhysicalDevice ph,VkDevice dev,uint32_t fam,PFN_vkGetInstanceProcAddr gi,PFN_vkGetDeviceProcAddr gd):instance(in),physical(ph),device(dev),family(fam),gipa(gi),gdpa(gd){}
    ~MailboxImport(){if(!destroyed&&images[0].image)std::fprintf(stderr,"DLSSG_MAILBOX_IMPORT_HOLD explicit_GPU_retirement_missing=1\n");}
    template<class T>T I(const char* n)const{auto f=reinterpret_cast<T>(gipa(instance,n));Need(f,n);return f;}
    template<class T>T D(const char* n)const{auto f=reinterpret_cast<T>(gdpa(device,n));Need(f,n);return f;}
    static void Vk(VkResult r,const char* why){Need(r==VK_SUCCESS,why);}
    J Identity()const{VkPhysicalDeviceIDProperties ids{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_ID_PROPERTIES};VkPhysicalDeviceProperties2 p{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2};p.pNext=&ids;I<PFN_vkGetPhysicalDeviceProperties2>("vkGetPhysicalDeviceProperties2")(physical,&p);return {{"device_uuid",Hex(ids.deviceUUID)},{"driver_uuid",Hex(ids.driverUUID)},{"driver_version",p.properties.driverVersion}};}
    void Import(const J& meta,const std::string& session,pid_t pid,uint64_t ticks){
        Session(meta,session);Need(!images[0].image&&B(meta,"completed_GPU")&&U(meta,"linux_pid")==uint64_t(pid)&&U(meta,"handle_type")==512&&U(meta,"initial_release_serial")>0,"GPU initial release/identity");
        const auto local=Identity();for(auto k:{"device_uuid","driver_uuid","driver_version"})Need(meta.at(k)==local.at(k),"GPU physical/driver identity mismatch");
        Need(meta.at("resources").is_array()&&meta.at("resources").size()==4,"four GPU mailbox images");
        exporter=pid;exporter_ticks=ticks;std::set<int> raw_numbers;
        for(unsigned k=0;k<4;++k){const auto& e=meta.at("resources").at(k);Resource(e,k);int raw=int(U(e,"fd"));Need(raw_numbers.insert(raw).second,"unique GPU raw FD numbers");
            Need(Ticks(pid)==ticks&&ticks>0,"GPU exporter starttime before duplication");auto payload=Payload(pid,raw);for(const auto& prior:raw_payloads)Need(!(payload==prior),"independent GPU memory payloads");raw_payloads.push_back(payload);
            int pidfd=int(syscall(SYS_pidfd_open,pid,0));Need(pidfd>=0,"GPU pidfd_open");int dup=int(syscall(SYS_pidfd_getfd,pidfd,raw,0));close(pidfd);Need(dup>=0,"GPU pidfd_getfd");
            try{
                Need(Payload(getpid(),dup)==payload&&Ticks(pid)==ticks,"GPU duplicated exact payload/stable exporter");
                auto& im=images[k];im.format=VkFormat(U(e,"format"));im.extent={uint32_t(U(e,"width")),uint32_t(U(e,"height")),1};
                VkPhysicalDeviceExternalImageFormatInfo extq{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_EXTERNAL_IMAGE_FORMAT_INFO};extq.handleType=VK_EXTERNAL_MEMORY_HANDLE_TYPE_DMA_BUF_BIT_EXT;
                VkPhysicalDeviceImageFormatInfo2 iq{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_IMAGE_FORMAT_INFO_2};iq.pNext=&extq;iq.format=im.format;iq.type=VK_IMAGE_TYPE_2D;iq.tiling=VK_IMAGE_TILING_OPTIMAL;iq.usage=15;
                VkExternalImageFormatProperties eq{VK_STRUCTURE_TYPE_EXTERNAL_IMAGE_FORMAT_PROPERTIES};VkImageFormatProperties2 qp{VK_STRUCTURE_TYPE_IMAGE_FORMAT_PROPERTIES_2};qp.pNext=&eq;
                Vk(I<PFN_vkGetPhysicalDeviceImageFormatProperties2>("vkGetPhysicalDeviceImageFormatProperties2")(physical,&iq,&qp),"Native GPU external image query");
                Need((eq.externalMemoryProperties.externalMemoryFeatures&6)==6&&(eq.externalMemoryProperties.compatibleHandleTypes&512)&&qp.imageFormatProperties.maxExtent.width>=im.extent.width&&qp.imageFormatProperties.maxExtent.height>=im.extent.height,"Native DMA-BUF image capability");
                VkExternalMemoryImageCreateInfo ex{VK_STRUCTURE_TYPE_EXTERNAL_MEMORY_IMAGE_CREATE_INFO};ex.handleTypes=VK_EXTERNAL_MEMORY_HANDLE_TYPE_DMA_BUF_BIT_EXT;
                VkImageCreateInfo ci{VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO};ci.pNext=&ex;ci.imageType=VK_IMAGE_TYPE_2D;ci.format=im.format;ci.extent=im.extent;ci.mipLevels=ci.arrayLayers=1;ci.samples=VK_SAMPLE_COUNT_1_BIT;ci.tiling=VK_IMAGE_TILING_OPTIMAL;ci.usage=15;ci.sharingMode=VK_SHARING_MODE_EXCLUSIVE;
                Vk(D<PFN_vkCreateImage>("vkCreateImage")(device,&ci,nullptr,&im.image),"Native GPU imported image");
                VkMemoryDedicatedRequirements de{VK_STRUCTURE_TYPE_MEMORY_DEDICATED_REQUIREMENTS};VkMemoryRequirements2 req{VK_STRUCTURE_TYPE_MEMORY_REQUIREMENTS_2};req.pNext=&de;VkImageMemoryRequirementsInfo2 ri{VK_STRUCTURE_TYPE_IMAGE_MEMORY_REQUIREMENTS_INFO_2};ri.image=im.image;D<PFN_vkGetImageMemoryRequirements2>("vkGetImageMemoryRequirements2")(device,&ri,&req);
                VkMemoryFdPropertiesKHR fp{VK_STRUCTURE_TYPE_MEMORY_FD_PROPERTIES_KHR};Vk(D<PFN_vkGetMemoryFdPropertiesKHR>("vkGetMemoryFdPropertiesKHR")(device,VK_EXTERNAL_MEMORY_HANDLE_TYPE_DMA_BUF_BIT_EXT,dup,&fp),"Native GPU FD requirements");auto type=uint32_t(U(e,"memory_type_index"));
                Need(req.memoryRequirements.size==U(e,"allocation_size")&&req.memoryRequirements.alignment==U(e,"alignment")&&req.memoryRequirements.memoryTypeBits==U(e,"req_memory_type_bits")&&((fp.memoryTypeBits&req.memoryRequirements.memoryTypeBits)&(1u<<type))&&uint32_t(de.prefersDedicatedAllocation)==U(e,"prefers_dedicated")&&uint32_t(de.requiresDedicatedAllocation)==U(e,"requires_dedicated"),"Native exact imported memory requirements");
                VkMemoryDedicatedAllocateInfo dedicated{VK_STRUCTURE_TYPE_MEMORY_DEDICATED_ALLOCATE_INFO};dedicated.image=im.image;VkImportMemoryFdInfoKHR fi{VK_STRUCTURE_TYPE_IMPORT_MEMORY_FD_INFO_KHR};fi.pNext=&dedicated;fi.handleType=VK_EXTERNAL_MEMORY_HANDLE_TYPE_DMA_BUF_BIT_EXT;fi.fd=dup;
                VkMemoryAllocateInfo ai{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};ai.pNext=&fi;ai.allocationSize=req.memoryRequirements.size;ai.memoryTypeIndex=type;
                Vk(D<PFN_vkAllocateMemory>("vkAllocateMemory")(device,&ai,nullptr,&im.memory),"Native GPU import allocation");dup=-1;
                Vk(D<PFN_vkBindImageMemory>("vkBindImageMemory")(device,im.image,im.memory,0),"Native GPU bind offset0");
            }catch(...){if(dup>=0)close(dup);throw;}
        }
    }
    void AuditRawRetired()const{
        Need(Ticks(exporter)==exporter_ticks,"GPU exporter live for FD audit");
        for(const auto& e:std::filesystem::directory_iterator("/proc/"+std::to_string(exporter)+"/fd")){
            const auto name=e.path().filename().string();if(name.empty()||name.find_first_not_of("0123456789")!=std::string::npos)continue;
            FdPayload value;try{value=Payload(exporter,std::stoi(name));}catch(const std::exception&){continue;}
            for(const auto& raw:raw_payloads)Need(!(raw==value),"Helper original DMA-BUF payload still owned");
        }
    }
    void Ownership(VkCommandBuffer cmd,unsigned role,bool acquire,bool write){
        Need(!destroyed,"GPU mailbox use after destroy");used=true;complete=false;
        VkImageMemoryBarrier b{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER};b.oldLayout=b.newLayout=VK_IMAGE_LAYOUT_GENERAL;
        b.srcQueueFamilyIndex=acquire?VK_QUEUE_FAMILY_EXTERNAL:family;b.dstQueueFamilyIndex=acquire?family:VK_QUEUE_FAMILY_EXTERNAL;
        const auto access=write?VK_ACCESS_TRANSFER_WRITE_BIT:VK_ACCESS_TRANSFER_READ_BIT;b.srcAccessMask=acquire?0:access;b.dstAccessMask=acquire?access:0;
        b.image=images.at(role).image;b.subresourceRange={VK_IMAGE_ASPECT_COLOR_BIT,0,1,0,1};
        D<PFN_vkCmdPipelineBarrier>("vkCmdPipelineBarrier")(cmd,acquire?VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT:VK_PIPELINE_STAGE_TRANSFER_BIT,acquire?VK_PIPELINE_STAGE_TRANSFER_BIT:VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT,0,0,nullptr,0,nullptr,1,&b);
    }
    void Observed(uint64_t tick){Need(used&&tick>last_use,"Native actual completed mailbox tick monotonic");last_use=tick;complete=true;}
    void Destroy(){Need(complete&&!destroyed,"Native GPU complete before imports destroy");for(auto& im:images){if(im.image)D<PFN_vkDestroyImage>("vkDestroyImage")(device,im.image,nullptr);if(im.memory)D<PFN_vkFreeMemory>("vkFreeMemory")(device,im.memory,nullptr);im={};}destroyed=true;}
};
} // namespace DlssgGpu
