#pragma once
// SPDX-License-Identifier: MIT
// Local Pre-SR720 copy of proved public DMA_BUF producer. No private Wine ABI; old producer frozen.
#include "fixture_util.h"
#include <algorithm>
#define D(n) device_fn<PFN_##n>(#n)
#define I(n) instance_fn<PFN_##n>(#n)
struct PreSrTransport {
 HMODULE loader=nullptr;PFN_vkGetInstanceProcAddr gipa=nullptr;PFN_vkGetDeviceProcAddr gdpa=nullptr;
 VkInstance instance=nullptr;VkPhysicalDevice physical=nullptr;VkDevice device=nullptr;VkQueue queue=nullptr;uint32_t family=0;
 VkPhysicalDeviceMemoryProperties memory{};VkPhysicalDeviceIDProperties ids{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_ID_PROPERTIES};VkPhysicalDeviceProperties properties{};
 VkCommandPool pool=nullptr;VkCommandBuffer cmd=nullptr;VkFence fence=nullptr;bool in_flight=false;uint64_t serial=0;
 struct Image {std::string role;VkFormat format;uint32_t width=0,height=0;VkImage image=nullptr;VkImageView view=nullptr;VkDeviceMemory allocation=nullptr;VkMemoryRequirements req{};VkMemoryDedicatedRequirements dedicated{VK_STRUCTURE_TYPE_MEMORY_DEDICATED_REQUIREMENTS};VkExternalMemoryProperties external{};uint32_t type=0;int fd=-1;};
 std::vector<Image> images;std::string closed_fds="[]";uint32_t width=0,height=0,gpu_timeout_ms=10000;uint64_t generation=0;std::string session,dir;uint32_t linux_pid=0;
 template<class T>T instance_fn(const char*n){auto f=reinterpret_cast<T>(gipa(instance,n));require(f,n,3);return f;}
 template<class T>T device_fn(const char*n){auto f=reinterpret_cast<T>(gdpa(device,n));require(f,n,3);return f;}
 uint32_t type(uint32_t bits,VkMemoryPropertyFlags flags){for(uint32_t i=0;i<memory.memoryTypeCount;i++)if((bits&(1u<<i))&&(memory.memoryTypes[i].propertyFlags&flags)==flags)return i;throw Stop(3,"memory type unavailable");}
 std::string resource_set()const{return session+"-g"+std::to_string(generation);}
 std::string name(const std::string&op,uint64_t frame,bool numbered=true)const{return op+"-"+session+"-g"+std::to_string(generation)+(numbered?"-f"+std::to_string(frame):"")+".json";}
 VkExternalMemoryProperties query(VkFormat format,const char*role,bool shared,uint32_t w,uint32_t h){
  VkPhysicalDeviceExternalImageFormatInfo external{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_EXTERNAL_IMAGE_FORMAT_INFO};external.handleType=VK_EXTERNAL_MEMORY_HANDLE_TYPE_DMA_BUF_BIT_EXT;VkPhysicalDeviceImageFormatInfo2 qi{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_IMAGE_FORMAT_INFO_2};qi.pNext=shared?&external:nullptr;qi.format=format;qi.type=VK_IMAGE_TYPE_2D;qi.tiling=VK_IMAGE_TILING_OPTIMAL;qi.usage=15;
  VkExternalImageFormatProperties ep{VK_STRUCTURE_TYPE_EXTERNAL_IMAGE_FORMAT_PROPERTIES};VkImageFormatProperties2 result{VK_STRUCTURE_TYPE_IMAGE_FORMAT_PROPERTIES_2};result.pNext=shared?&ep:nullptr;api(I(vkGetPhysicalDeviceImageFormatProperties2)(physical,&qi,&result),"ImageFormatProperties2");
  VkFormatProperties fp{};I(vkGetPhysicalDeviceFormatProperties)(physical,format,&fp);printf("RESOURCE_QUERY role=%s format=%u usage=15 optimal_features=0x%x external_features=0x%x compatible=0x%x shared=%d shader_tested=0\n",role,format,fp.optimalTilingFeatures,ep.externalMemoryProperties.externalMemoryFeatures,ep.externalMemoryProperties.compatibleHandleTypes,shared);
  require(result.imageFormatProperties.maxExtent.width>=w&&result.imageFormatProperties.maxExtent.height>=h&&(result.imageFormatProperties.sampleCounts&VK_SAMPLE_COUNT_1_BIT),"image limits",3);
  if(shared){require((ep.externalMemoryProperties.externalMemoryFeatures&(VK_EXTERNAL_MEMORY_FEATURE_EXPORTABLE_BIT|VK_EXTERNAL_MEMORY_FEATURE_IMPORTABLE_BIT))==(VK_EXTERNAL_MEMORY_FEATURE_EXPORTABLE_BIT|VK_EXTERNAL_MEMORY_FEATURE_IMPORTABLE_BIT),"DMA_BUF import/export properties",3);require(ep.externalMemoryProperties.compatibleHandleTypes&VK_EXTERNAL_MEMORY_HANDLE_TYPE_DMA_BUF_BIT_EXT,"DMA_BUF compatibility",3);}
  const auto need=VK_FORMAT_FEATURE_SAMPLED_IMAGE_BIT|VK_FORMAT_FEATURE_STORAGE_IMAGE_BIT|VK_FORMAT_FEATURE_TRANSFER_SRC_BIT|VK_FORMAT_FEATURE_TRANSFER_DST_BIT;
  require((fp.optimalTilingFeatures&need)==need,"sampled/storage/transfer format properties",3);
  if(shared&&strcmp(role,"output"))require(fp.optimalTilingFeatures&VK_FORMAT_FEATURE_SAMPLED_IMAGE_FILTER_LINEAR_BIT,"linear sampled input format",3);return ep.externalMemoryProperties;
 }
 VkImageCreateInfo image_ci(VkFormat format,VkExternalMemoryImageCreateInfo&ext,uint32_t w,uint32_t h){ext={VK_STRUCTURE_TYPE_EXTERNAL_MEMORY_IMAGE_CREATE_INFO};ext.handleTypes=VK_EXTERNAL_MEMORY_HANDLE_TYPE_DMA_BUF_BIT_EXT;VkImageCreateInfo ci{VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO};ci.pNext=&ext;ci.imageType=VK_IMAGE_TYPE_2D;ci.format=format;ci.extent={w,h,1};ci.mipLevels=ci.arrayLayers=1;ci.samples=VK_SAMPLE_COUNT_1_BIT;ci.tiling=VK_IMAGE_TILING_OPTIMAL;ci.usage=15;ci.sharingMode=VK_SHARING_MODE_EXCLUSIVE;ci.initialLayout=VK_IMAGE_LAYOUT_UNDEFINED;return ci;}
 void create_images(){
  for(auto spec:std::vector<std::pair<const char*,VkFormat>>{{"color",VK_FORMAT_R32G32B32A32_SFLOAT},{"depth",VK_FORMAT_R32_SFLOAT},{"mv",VK_FORMAT_R32G32_SFLOAT},{"output",VK_FORMAT_R32G32B32A32_SFLOAT}}){
   Image im;im.role=spec.first;im.format=spec.second;im.width=1280;im.height=720;im.external=query(im.format,spec.first,true,im.width,im.height);VkExternalMemoryImageCreateInfo ex{};auto ci=image_ci(im.format,ex,im.width,im.height);api(D(vkCreateImage)(device,&ci,nullptr,&im.image),"CreateSharedImage");
   VkImageMemoryRequirementsInfo2 ri{VK_STRUCTURE_TYPE_IMAGE_MEMORY_REQUIREMENTS_INFO_2};ri.image=im.image;VkMemoryRequirements2 r{VK_STRUCTURE_TYPE_MEMORY_REQUIREMENTS_2};r.pNext=&im.dedicated;D(vkGetImageMemoryRequirements2)(device,&ri,&r);im.req=r.memoryRequirements;im.type=type(im.req.memoryTypeBits,VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
   VkMemoryDedicatedAllocateInfo ded{VK_STRUCTURE_TYPE_MEMORY_DEDICATED_ALLOCATE_INFO};ded.image=im.image;VkExportMemoryAllocateInfo ei{VK_STRUCTURE_TYPE_EXPORT_MEMORY_ALLOCATE_INFO};ei.pNext=&ded;ei.handleTypes=VK_EXTERNAL_MEMORY_HANDLE_TYPE_DMA_BUF_BIT_EXT;VkMemoryAllocateInfo ai{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};ai.pNext=&ei;ai.allocationSize=im.req.size;ai.memoryTypeIndex=im.type;api(D(vkAllocateMemory)(device,&ai,nullptr,&im.allocation),"AllocateExportImage");api(D(vkBindImageMemory)(device,im.image,im.allocation,0),"BindExportImage");
   VkImageViewCreateInfo vi{VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};vi.image=im.image;vi.viewType=VK_IMAGE_VIEW_TYPE_2D;vi.format=im.format;vi.subresourceRange={VK_IMAGE_ASPECT_COLOR_BIT,0,1,0,1};api(D(vkCreateImageView)(device,&vi,nullptr,&im.view),"SharedView");VkMemoryGetFdInfoKHR fi{VK_STRUCTURE_TYPE_MEMORY_GET_FD_INFO_KHR};fi.memory=im.allocation;fi.handleType=VK_EXTERNAL_MEMORY_HANDLE_TYPE_DMA_BUF_BIT_EXT;api(D(vkGetMemoryFdKHR)(device,&fi,&im.fd),"GetMemoryFd");require(im.fd>=0,"export fd",3);printf("EXPORT role=%s fd=%d size=%llu req_bits=0x%x type=%u dedicated=1\n",im.role.c_str(),im.fd,(unsigned long long)im.req.size,im.req.memoryTypeBits,im.type);images.push_back(im);
  }
 }
 void begin(){require(!in_flight,"record before GPU completion");api(D(vkResetCommandBuffer)(cmd,0),"ResetCommand");VkCommandBufferBeginInfo b{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};b.flags=VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;api(D(vkBeginCommandBuffer)(cmd,&b),"BeginCommand");}
 void submit(){
  require(serial<9007199254740991ull,"transport serial exhausted before submit",3);require(!in_flight,"submit while GPU outstanding");api(D(vkEndCommandBuffer)(cmd),"EndCommand");api(D(vkResetFences)(device,1,&fence),"ResetFence");
  VkSubmitInfo si{VK_STRUCTURE_TYPE_SUBMIT_INFO};si.commandBufferCount=1;si.pCommandBuffers=&cmd;in_flight=true;api(D(vkQueueSubmit)(queue,1,&si,fence),"QueueSubmit");
  VkResult result=D(vkWaitForFences)(device,1,&fence,VK_TRUE,uint64_t(gpu_timeout_ms)*1000000ull);
  printf("GPU_WAIT result=%d deadline_ms=%u actual_fence=1\n",int(result),gpu_timeout_ms);
  if(result!=VK_SUCCESS)throw Stop(result==VK_TIMEOUT?3:2,"GPU fence incomplete; HOLD resources, abnormal private helper exit only");
  api(D(vkGetFenceStatus)(device,fence),"FenceObserved");in_flight=false;require(serial<9007199254740991ull,"transport serial exhausted",3);++serial;
  printf("GPU_COMPLETE serial=%llu actual_fence=1 external_semaphore=NOT_USED\n",(unsigned long long)serial);
 }
 void barrier(Image&im,VkImageLayout old,uint32_t src,uint32_t dst,VkAccessFlags sa,VkAccessFlags da,VkPipelineStageFlags ss,VkPipelineStageFlags ds){VkImageMemoryBarrier b{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER};b.oldLayout=old;b.newLayout=VK_IMAGE_LAYOUT_GENERAL;b.srcQueueFamilyIndex=src;b.dstQueueFamilyIndex=dst;b.srcAccessMask=sa;b.dstAccessMask=da;b.image=im.image;b.subresourceRange={VK_IMAGE_ASPECT_COLOR_BIT,0,1,0,1};D(vkCmdPipelineBarrier)(cmd,ss,ds,0,0,nullptr,0,nullptr,1,&b);}
 void initial_release(){begin();for(auto&im:images){barrier(im,VK_IMAGE_LAYOUT_UNDEFINED,VK_QUEUE_FAMILY_IGNORED,VK_QUEUE_FAMILY_IGNORED,0,VK_ACCESS_TRANSFER_WRITE_BIT,VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,VK_PIPELINE_STAGE_TRANSFER_BIT);VkClearColorValue zero{};VkImageSubresourceRange range{VK_IMAGE_ASPECT_COLOR_BIT,0,1,0,1};D(vkCmdClearColorImage)(cmd,im.image,VK_IMAGE_LAYOUT_GENERAL,&zero,1,&range);barrier(im,VK_IMAGE_LAYOUT_GENERAL,family,VK_QUEUE_FAMILY_EXTERNAL,VK_ACCESS_TRANSFER_WRITE_BIT,0,VK_PIPELINE_STAGE_TRANSFER_BIT,VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT);}submit();}
 std::string metadata()const{
  std::string s="{\"session\":"+quote(session)+",\"generation\":"+std::to_string(generation)+",\"resource_set\":"+quote(resource_set())+",\"status\":\"READY\",\"completed_GPU\":true,\"linux_pid\":"+std::to_string(linux_pid)+",\"win_pid\":"+std::to_string(GetCurrentProcessId())+",\"device_uuid\":"+quote(hex(ids.deviceUUID))+",\"driver_uuid\":"+quote(hex(ids.driverUUID))+",\"handle_type\":512,\"queue_family\":"+std::to_string(family)+",\"resources\":[";
  for(size_t i=0;i<images.size();i++){auto&im=images[i];if(i)s+=",";s+="{\"role\":"+quote(im.role)+",\"fd\":"+std::to_string(im.fd)+",\"allocation_size\":"+std::to_string(im.req.size)+",\"memory_type_index\":"+std::to_string(im.type)+",\"req_memory_type_bits\":"+std::to_string(im.req.memoryTypeBits)+",\"alignment\":"+std::to_string(im.req.alignment)+",\"width\":"+std::to_string(im.width)+",\"height\":"+std::to_string(im.height)+",\"format\":"+std::to_string(im.format)+",\"usage\":15,\"tiling\":0,\"image_type\":1,\"samples\":1,\"mip_levels\":1,\"array_layers\":1,\"flags\":0,\"sharing_mode\":0,\"dedicated\":true,\"offset\":0,\"runtime_layout\":1,\"external_features\":"+std::to_string(im.external.externalMemoryFeatures)+",\"compatible_handle_types\":"+std::to_string(im.external.compatibleHandleTypes)+",\"prefers_dedicated\":"+std::to_string(im.dedicated.prefersDedicatedAllocation)+",\"requires_dedicated\":"+std::to_string(im.dedicated.requiresDedicatedAllocation)+"}";}return s+"]}\n";
 }
 void retire_fds(){
  closed_fds="[";bool first=true;
  for(auto&im:images){require(im.fd>=0,"already-consumed FD");int original_fd=im.fd;VkExternalMemoryImageCreateInfo ex{};auto ci=image_ci(im.format,ex,im.width,im.height);VkImage temp=nullptr;api(D(vkCreateImage)(device,&ci,nullptr,&temp),"CreateFDConsumerImage");VkMemoryFdPropertiesKHR fp{VK_STRUCTURE_TYPE_MEMORY_FD_PROPERTIES_KHR};auto fr=D(vkGetMemoryFdPropertiesKHR)(device,VK_EXTERNAL_MEMORY_HANDLE_TYPE_DMA_BUF_BIT_EXT,im.fd,&fp);if(fr!=VK_SUCCESS){D(vkDestroyImage)(device,temp,nullptr);throw Stop(3,"FD properties failed; rawFD remains caller-owned");}
   VkMemoryRequirements req{};D(vkGetImageMemoryRequirements)(device,temp,&req);if(!(req.memoryTypeBits&fp.memoryTypeBits&(1u<<im.type))||req.size!=im.req.size){D(vkDestroyImage)(device,temp,nullptr);throw Stop(3,"FD consumer incompatible; rawFD remains caller-owned");}
   VkMemoryDedicatedAllocateInfo ded{VK_STRUCTURE_TYPE_MEMORY_DEDICATED_ALLOCATE_INFO};ded.image=temp;VkImportMemoryFdInfoKHR imp{VK_STRUCTURE_TYPE_IMPORT_MEMORY_FD_INFO_KHR};imp.pNext=&ded;imp.handleType=VK_EXTERNAL_MEMORY_HANDLE_TYPE_DMA_BUF_BIT_EXT;imp.fd=im.fd;VkMemoryAllocateInfo ai{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};ai.pNext=&imp;ai.allocationSize=im.req.size;ai.memoryTypeIndex=im.type;VkDeviceMemory imported=nullptr;auto r=D(vkAllocateMemory)(device,&ai,nullptr,&imported);printf("FD_CONSUME role=%s fd=%d allocate_result=%d req_bits=0x%x fd_bits=0x%x\n",im.role.c_str(),original_fd,int(r),req.memoryTypeBits,fp.memoryTypeBits);
   if(r!=VK_SUCCESS){D(vkDestroyImage)(device,temp,nullptr);throw Stop(3,"rawFD import failed; original rawFD remains caller-owned, no persistent reclamation PASS");}
   if(!first)closed_fds+=",";first=false;closed_fds+="{\"role\":"+quote(im.role)+",\"fd\":"+std::to_string(original_fd)+"}";
   im.fd=-1; // Public Vulkan API transferred ownership on SUCCESS, no CRT/Unix/private call.
   D(vkDestroyImage)(device,temp,nullptr);D(vkFreeMemory)(device,imported,nullptr);printf("FD_CONSUMED role=%s original_fd=%d import_allocation_freed=1 temporary_image_destroyed=1 host_close_strategy=PUBLIC_VULKAN_IMPORT_TRANSFER native_proc_audit_required=1\n",im.role.c_str(),original_fd);
  }
  closed_fds+="]";
 }
 void free_shared(){
  require(!in_flight,"cannot free images with incomplete GPU fence");
  for(auto&im:images){require(im.fd==-1,"original raw FD still caller-owned; no normal retirement",3);D(vkDestroyImageView)(device,im.view,nullptr);D(vkDestroyImage)(device,im.image,nullptr);D(vkFreeMemory)(device,im.allocation,nullptr);}images.clear();
  puts("SHARED_RETIRED images=4 export_fd_owned=0 native_release_ack_required=1");
 }
 void shutdown_transport(){
  require(!in_flight&&images.empty(),"normal shutdown requires GPU quiescence and no shared allocations");
  D(vkDestroyFence)(device,fence,nullptr);D(vkDestroyCommandPool)(device,pool,nullptr);D(vkDestroyDevice)(device,nullptr);device=nullptr;I(vkDestroyInstance)(instance,nullptr);instance=nullptr;
  require(FreeLibrary(loader),"Vulkan loader FreeLibrary");loader=nullptr;
  puts("NORMAL_RELEASE Vulkan=1 model_init=0 evaluate_calls=0 resolve_calls=0 process_exit_not_raw_fd_strategy=1");
 }
};
