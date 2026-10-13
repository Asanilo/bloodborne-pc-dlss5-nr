// SPDX-License-Identifier: GPL-2.0-or-later
#include "video_core/renderer_vulkan/native_edit_diagnostic_policy.h"
#include <cstdio>
#include <cstdlib>
using BbNativeAsync::EditDiagnosticIdentity;
int main(){
 unsigned cases=0;auto check=[&](bool ok){++cases;if(!ok)std::abort();};
 const EditDiagnosticIdentity good{100,100,7,7,3,3,1,1,0,true,true,true};
 check(good.Eligible());auto late=good;late.current=104;late.job=3;check(late.Eligible());
 auto bad=[&](auto mutate){auto c=good;mutate(c);check(!c.Eligible());};
 bad([](auto&c){c.current=105;});bad([](auto&c){c.current=99;});
 bad([](auto&c){c.origin=0;});bad([](auto&c){c.current_epoch=8;});
 bad([](auto&c){c.origin_epoch=c.current_epoch=0;});
 bad([](auto&c){c.current_history=4;});bad([](auto&c){c.origin_history=c.current_history=0;});
 bad([](auto&c){c.origin_params=c.current_params=2;});bad([](auto&c){c.current_params=2;});
 bad([](auto&c){c.job=4;});bad([](auto&c){c.resident=false;});
 bad([](auto&c){c.captured=false;});bad([](auto&c){c.actual_view_image_match=false;});
 auto huge=good;huge.origin=UINT64_MAX-4;huge.current=UINT64_MAX;check(huge.Eligible());
 huge.current=0;check(!huge.Eligible());
 std::printf("{\"status\":\"PASS\",\"cases\":%u,\"scope\":\"production_diagnostic_admission_only_no_GPU_or_pixel_claim\"}\n",cases);
}
