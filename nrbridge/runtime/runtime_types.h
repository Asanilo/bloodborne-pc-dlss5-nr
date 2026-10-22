#pragma once
// SPDX-License-Identifier: GPL-3.0-or-later
// Local adapter types only. No protocol6 schema is defined here.
#include "fixture_util.h"
#include "nvsdk_ngx_vk.h"
#include <json.hpp>
#include <cmath>
#include <memory>
using Json=nlohmann::json;
static void need(bool b,const char*m){require(b,m);}
static void check(VkResult r,const char*m){api(r,m);}
template<class T>static T proc(PFN_vkGetInstanceProcAddr g,VkInstance i,const char*n){auto p=reinterpret_cast<T>(g(i,n));require(p,n,3);return p;}
static double c4_cpu_now_ms(){static const double frequency=[](){LARGE_INTEGER f{};require(QueryPerformanceFrequency(&f)&&f.QuadPart>0,"QPC frequency");return double(f.QuadPart);}();LARGE_INTEGER q{};require(QueryPerformanceCounter(&q),"QPC counter");return double(q.QuadPart)*1000.0/frequency;}
// These are host requests, not parameters in the fixed NR Evaluate ABI.
static Json playable_parameter_application(){return Json{{"frame_ms","HOST_FIXTURE_DECLARATION_NOT_SENT_TO_NR_API"},{"exposure","HOST_FIXTURE_DECLARATION_NOT_SENT_TO_NR_API"},{"pre_exposure","HOST_FIXTURE_DECLARATION_NOT_SENT_TO_NR_API"},{"runtime_default_values","UNVERIFIED"}};}
