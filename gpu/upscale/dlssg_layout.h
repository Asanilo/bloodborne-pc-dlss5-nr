// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <array>
#include <cstdint>
#include <stdexcept>
#include <nlohmann/json.hpp>
namespace DlssgGpu {
// Immutable for one Helper/mailbox generation; independent of the NR execution domain.
struct Layout {
    uint32_t render_width=1280,render_height=720,output_width=1920,output_height=1080;
    static void Check(uint64_t w,uint64_t h){
        if(w<160||h<90||w>8192||h>8192||w*h>3840ull*2160||double(w)/h<.8||double(w)/h>3.6)throw std::runtime_error("DLSSG layout extent exceeds bounded image policy");
    }
    void Validate()const{Check(render_width,render_height);Check(output_width,output_height);}
    bool operator==(const Layout& b)const{return render_width==b.render_width&&render_height==b.render_height&&output_width==b.output_width&&output_height==b.output_height;}
    bool operator!=(const Layout& b)const{return !(*this==b);}
    nlohmann::json Json()const{Validate();return {{"revision",1},{"render_width",render_width},{"render_height",render_height},{"output_width",output_width},{"output_height",output_height},{"color_format",37},{"depth_format",100},{"motion_format",83},{"source_rect_origin",{0,0}},{"color_domain","finished-scene-bytes"}};}
    static Layout From(const nlohmann::json& j){
        if(!j.contains("layout"))return {}; // Read-only compatibility with the validated M4 wire.
        const auto& d=j.at("layout");Layout value;
        const auto get=[&](const char* key){const auto& n=d.at(key);if(!n.is_number_unsigned()&&!(n.is_number_integer()&&n.get<int64_t>()>=0))throw std::runtime_error("DLSSG integer layout extent");auto v=n.get<uint64_t>();if(v>UINT32_MAX)throw std::runtime_error("DLSSG layout overflow");return uint32_t(v);};
        value.render_width=get("render_width");value.render_height=get("render_height");value.output_width=get("output_width");value.output_height=get("output_height");
        value.Validate();if(d!=value.Json())throw std::runtime_error("DLSSG exact layout format/domain contract");return value;
    }
};
}
