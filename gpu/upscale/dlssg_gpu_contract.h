// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <array>
#include <cstdint>
#include <stdexcept>
#include <string>
#include <nlohmann/json.hpp>
namespace DlssgGpu {
using J=nlohmann::json;
inline constexpr const char* Schema="dlssg-gpu-mailbox-v1";
inline void Need(bool ok,const char* reason){if(!ok)throw std::runtime_error(reason);}
inline uint64_t U(const J& j,const char* key){const auto& v=j.at(key);Need(v.is_number_unsigned()||(v.is_number_integer()&&v.get<int64_t>()>=0),"GPU wire unsigned integer");return v.get<uint64_t>();}
inline bool B(const J& j,const char* key){Need(j.at(key).is_boolean(),"GPU wire boolean");return j.at(key).get<bool>();}
inline void Session(const J& j,const std::string& session){Need(j.at("schema")==Schema&&j.at("session")==session,"GPU wire schema/session");}
struct Spec{const char* role;uint32_t format,width,height;};
inline constexpr std::array<Spec,4> Specs{{{"color",37,1920,1080},{"depth",100,1280,720},{"motion",83,1280,720},{"output",37,1920,1080}}};
inline void Resource(const J& j,unsigned index){
    const auto& s=Specs.at(index);Need(j.at("role")==s.role&&U(j,"format")==s.format&&U(j,"width")==s.width&&U(j,"height")==s.height,"GPU mailbox role/format/extent");
    for(auto [k,v]:std::array<std::pair<const char*,uint64_t>,10>{{{"usage",15},{"tiling",0},{"image_type",1},{"samples",1},{"mip_levels",1},{"array_layers",1},{"flags",0},{"sharing_mode",0},{"offset",0},{"runtime_layout",1}}})Need(U(j,k)==v,"GPU mailbox exact image contract");
    Need(B(j,"dedicated")&&U(j,"allocation_size")&&U(j,"alignment")&&U(j,"memory_type_index")<32&&U(j,"req_memory_type_bits")<=UINT32_MAX&&U(j,"req_memory_type_bits"),"GPU mailbox memory contract");
    Need(U(j,"fd")>0&&U(j,"fd")<=INT32_MAX&&(U(j,"external_features")&6)==6&&(U(j,"compatible_handle_types")&512),"GPU mailbox DMA-BUF contract");
    Need(U(j,"prefers_dedicated")<=1&&U(j,"requires_dedicated")<=1,"GPU dedicated VkBool32");
}
inline J Pair(const J& j){J result;for(auto k:{"schema","session","sequence","epoch","source_game_frame","previous_source_game_frame","display"})result[k]=j.at(k);return result;}
inline void Request(const J& j,const std::string& session,uint64_t sequence,uint64_t epoch,uint64_t source,uint64_t previous_tick){
    Session(j,session);const auto next=U(j,"source_game_frame"),generation=U(j,"epoch");const bool reset=B(j,"reset");
    Need(U(j,"sequence")==sequence+1&&next>source&&U(j,"previous_source_game_frame")==source&&generation>0&&
         ((generation==epoch&&next==source+1&&!reset)||(generation>epoch&&reset)),"GPU real source pair/epoch");
    Need(B(j,"native_input_GPU_complete")&&U(j,"native_input_tick")>previous_tick,"GPU producer actual completion tick");
    Need(j.at("wire_order")=="game-scene-bytes","GPU raw scene byte contract");
}
inline void Consumed(const J& ack,const J& request,uint64_t helper_serial){
    Need(Pair(ack)==Pair(request)&&U(ack,"helper_serial_echo")==helper_serial&&B(ack,"native_GPU_complete")&&B(ack,"output_released")&&U(ack,"native_last_use_tick")>U(request,"native_input_tick"),"GPU exact consumed/fence/output release");
}
} // namespace DlssgGpu
