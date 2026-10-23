// SPDX-License-Identifier: MIT
// Adapt the already-proved NRBridge public Vulkan DMA-BUF exporter. No Wine ABI.
#pragma once
#include "../../nrbridge/runtime/transport.h"
struct FgMailboxExport : PreSrTransport {
  bool native_released=false;
  FgMailboxExport(VkInstance in,VkPhysicalDevice ph,VkDevice dev,VkQueue qu,unsigned fam,
      PFN_vkGetInstanceProcAddr gi,PFN_vkGetDeviceProcAddr gd,VkCommandBuffer cb,VkFence fe){
    instance=in;physical=ph;device=dev;queue=qu;family=fam;gipa=gi;gdpa=gd;cmd=cb;fence=fe;
    I(vkGetPhysicalDeviceMemoryProperties)(physical,&memory);
    VkPhysicalDeviceProperties2 p{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2};p.pNext=&ids;
    I(vkGetPhysicalDeviceProperties2)(physical,&p);properties=p.properties;
  }
  void create_mailbox(){
    struct Spec{const char* role;VkFormat format;unsigned w,h;};
    for(auto spec:{Spec{"color",VK_FORMAT_R8G8B8A8_UNORM,1920,1080},Spec{"depth",VK_FORMAT_R32_SFLOAT,1280,720},
                   Spec{"motion",VK_FORMAT_R16G16_SFLOAT,1280,720},Spec{"output",VK_FORMAT_R8G8B8A8_UNORM,1920,1080}}){
      images.emplace_back();auto& im=images.back();im.role=spec.role;im.format=spec.format;im.width=spec.w;im.height=spec.h;
      im.external=query(im.format,spec.role,true,spec.w,spec.h);VkExternalMemoryImageCreateInfo ex{};auto ci=image_ci(im.format,ex,spec.w,spec.h);
      api(D(vkCreateImage)(device,&ci,nullptr,&im.image),"FG mailbox image");
      VkImageMemoryRequirementsInfo2 ri{VK_STRUCTURE_TYPE_IMAGE_MEMORY_REQUIREMENTS_INFO_2};ri.image=im.image;
      VkMemoryRequirements2 req{VK_STRUCTURE_TYPE_MEMORY_REQUIREMENTS_2};req.pNext=&im.dedicated;D(vkGetImageMemoryRequirements2)(device,&ri,&req);im.req=req.memoryRequirements;
      im.type=type(im.req.memoryTypeBits,VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
      VkMemoryDedicatedAllocateInfo dedicated{VK_STRUCTURE_TYPE_MEMORY_DEDICATED_ALLOCATE_INFO};dedicated.image=im.image;
      VkExportMemoryAllocateInfo export_info{VK_STRUCTURE_TYPE_EXPORT_MEMORY_ALLOCATE_INFO};export_info.pNext=&dedicated;export_info.handleTypes=VK_EXTERNAL_MEMORY_HANDLE_TYPE_DMA_BUF_BIT_EXT;
      VkMemoryAllocateInfo allocation{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};allocation.pNext=&export_info;allocation.allocationSize=im.req.size;allocation.memoryTypeIndex=im.type;
      api(D(vkAllocateMemory)(device,&allocation,nullptr,&im.allocation),"FG mailbox dedicated allocation");
      api(D(vkBindImageMemory)(device,im.image,im.allocation,0),"FG mailbox bind offset zero");
      VkMemoryGetFdInfoKHR fd{VK_STRUCTURE_TYPE_MEMORY_GET_FD_INFO_KHR};fd.memory=im.allocation;fd.handleType=VK_EXTERNAL_MEMORY_HANDLE_TYPE_DMA_BUF_BIT_EXT;
      api(D(vkGetMemoryFdKHR)(device,&fd,&im.fd),"FG mailbox export fd");require(im.fd>=0,"FG actual export FD");
    }
  }
  void acquire(unsigned role,VkAccessFlags access){barrier(images.at(role),VK_IMAGE_LAYOUT_GENERAL,VK_QUEUE_FAMILY_EXTERNAL,family,0,access,VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,VK_PIPELINE_STAGE_TRANSFER_BIT);}
  void release(unsigned role,VkAccessFlags access){barrier(images.at(role),VK_IMAGE_LAYOUT_GENERAL,family,VK_QUEUE_FAMILY_EXTERNAL,access,0,VK_PIPELINE_STAGE_TRANSFER_BIT,VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT);}
  void copy_to_private(FgFixture& g,FgFixture::Image& private_image,unsigned role){
    auto& shared=images.at(role);acquire(role,VK_ACCESS_TRANSFER_READ_BIT);
    g.barrier(private_image,VK_IMAGE_LAYOUT_UNDEFINED,VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);
    VkImageCopy copy{};copy.srcSubresource=copy.dstSubresource={VK_IMAGE_ASPECT_COLOR_BIT,0,0,1};copy.extent={shared.width,shared.height,1};
    D(vkCmdCopyImage)(cmd,shared.image,VK_IMAGE_LAYOUT_GENERAL,private_image.image,VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,1,&copy);
    release(role,VK_ACCESS_TRANSFER_READ_BIT);g.barrier(private_image,VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
  }
  void copy_output(FgFixture& g,FgFixture::Image& private_image){
    auto& shared=images.at(3);acquire(3,VK_ACCESS_TRANSFER_WRITE_BIT);
    g.barrier(private_image,VK_IMAGE_LAYOUT_GENERAL,VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL);
    VkImageCopy copy{};copy.srcSubresource=copy.dstSubresource={VK_IMAGE_ASPECT_COLOR_BIT,0,0,1};copy.extent={shared.width,shared.height,1};
    D(vkCmdCopyImage)(cmd,private_image.image,VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,shared.image,VK_IMAGE_LAYOUT_GENERAL,1,&copy);
    release(3,VK_ACCESS_TRANSFER_WRITE_BIT);
  }
  void destroy_released(){require(native_released&&!in_flight,"Native released imports before exporter destroy");
    for(auto& im:images){require(im.fd==-1,"export raw FD retired");D(vkDestroyImage)(device,im.image,nullptr);D(vkFreeMemory)(device,im.allocation,nullptr);}images.clear();}
};
#undef D
#undef I
