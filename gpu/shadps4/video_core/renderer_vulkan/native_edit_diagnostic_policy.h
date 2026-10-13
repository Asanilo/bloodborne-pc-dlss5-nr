// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include <cstdint>
namespace BbNativeAsync {
// Diagnostic admission only, never a cache/model/quality grant.
struct EditDiagnosticIdentity {
 uint64_t origin=0,current=0,origin_epoch=0,current_epoch=0;
 uint64_t origin_history=0,current_history=0,origin_params=0,current_params=0,job=0;
 bool resident=false,captured=false,actual_view_image_match=false;
 bool Eligible()const noexcept {
  return resident&&captured&&actual_view_image_match&&origin&&current>=origin&&current-origin<=4&&
   origin_epoch&&origin_epoch==current_epoch&&origin_history&&origin_history==current_history&&
   origin_params==1&&current_params==origin_params&&job<4;
 }
};
}
