// SPDX-License-Identifier: GPL-2.0-or-later
#include "video_core/renderer_vulkan/native_async_pre_sr_wire.h"
#include <iostream>
namespace W=Vulkan::NativeAsyncPreSrWire;
int main(){unsigned checks=0;
 const char* roles[]{"color","depth","mv","output"};
 // Exact frozen A57b pre_sr_transport.h:51-53 metadata shape; memory/FD values
 // below are deliberately CPU synthetic fixtures, NOT observed API/GPU values.
 for(unsigned k=0;k<4;++k){auto j=W::parse(std::string("{\"role\":\"")+roles[k]+"\",\"fd\":55,\"allocation_size\":14745600,\"memory_type_index\":1,\"req_memory_type_bits\":2,\"alignment\":256,\"width\":1280,\"height\":720,\"format\":"+std::to_string(k==1?100:k==2?103:109)+",\"usage\":15,\"tiling\":0,\"image_type\":1,\"samples\":1,\"mip_levels\":1,\"array_layers\":1,\"flags\":0,\"sharing_mode\":0,\"dedicated\":true,\"offset\":0,\"runtime_layout\":1,\"external_features\":6,\"compatible_handle_types\":512,\"prefers_dedicated\":0,\"requires_dedicated\":1}");
  W::resource(j,k);++checks;
  auto rejects=[&](W::J bad){bool rejected=false;try{W::resource(bad,k);}catch(...){rejected=true;}if(!rejected)throw std::runtime_error("resource failclosed counterexample accepted");++checks;};
  auto bad=j;bad["width"]=1920u;rejects(bad);bad=j;bad["height"]=1080u;rejects(bad);
  bad=j;bad["usage"]=7u;rejects(bad);bad=j;bad["prefers_dedicated"]=true;rejects(bad);
 }
 std::cout<<"{\"status\":\"PASS_CPU_ONLY_FROZEN_A_METADATA_SHAPE\",\"checks\":"<<checks<<",\"memory_FD_numbers\":\"SYNTHETIC_NOT_API_PROOF\",\"GPU\":\"NOT_RUN\"}\n";
}
