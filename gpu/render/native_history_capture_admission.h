// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "native_dev_paths.h"
#include <cerrno>
#include <cstdint>
#include <cstring>
#include <stdexcept>

namespace Vulkan::HistoryCapture {
// Only explicit CPU request validation may throw this type. Allocation, GPU
// recording, completion and writer failures must retain their ordinary handling.
class DiagnosticAdmissionRejected : public std::runtime_error {
public:
 using std::runtime_error::runtime_error;
};
struct Request {
 uint64_t sequence=0,start=0,after_candidate=1;
 unsigned limit=3;
 bool ui_request=false,capture_links=false;
 std::string session,directory;
};
inline uint64_t Number(const char* name,uint64_t fallback,uint64_t maximum){
 const char* text=std::getenv(name);if(!text)return fallback;
 char* end=nullptr;errno=0;const auto value=std::strtoull(text,&end,10);
 if(errno==ERANGE||!*text||*text=='-'||!end||*end||!value||value>maximum)
  throw DiagnosticAdmissionRejected("invalid bounded persistent diagnostic configuration");
 return value;
}
// No Vulkan objects, allocation of staging, GPU record or completion claims.
inline Request ValidateRequest(uint64_t sequence,unsigned frames,bool environment_enabled,unsigned max_frames){
 Request request;request.sequence=sequence;request.ui_request=sequence!=0;
 if(!request.ui_request&&!environment_enabled)
  throw DiagnosticAdmissionRejected("persistent cold diagnostic explicit opt-in required");
 request.limit=frames?frames:unsigned(Number("BB_NATIVE_HISTORY_DIAGNOSTIC_FRAMES",3,max_frames));
 if(!request.limit||request.limit>max_frames)
  throw DiagnosticAdmissionRejected("capture frame limit out of range");
 if(!request.ui_request){
  request.after_candidate=Number("BB_NATIVE_HISTORY_DIAGNOSTIC_AFTER_CANDIDATE",1,1000000);
  if(std::getenv("BB_NATIVE_HISTORY_DIAGNOSTIC_START_SOURCE"))
   request.start=Number("BB_NATIVE_HISTORY_DIAGNOSTIC_START_SOURCE",1,UINT64_MAX);
 }
 const char* run=std::getenv("BB_NR_NATIVE_ASYNC_RUN_DIR"),*session=std::getenv("BB_NR_NATIVE_ASYNC_SESSION");
 if(!run||!session||!*session||!BbNativeAsyncPreSr::ValidDevRun(run,session))
  throw DiagnosticAdmissionRejected("persistent diagnostic must use isolated canonical new run directory");
 request.session=session;
 request.directory=(std::filesystem::path(run)/("persistent-history-diagnostic-"+request.session+
                    (request.ui_request?"-capture-"+std::to_string(sequence):""))).string();
 std::error_code error;const auto status=std::filesystem::symlink_status(request.directory,error);
 if((error&&error!=std::errc::no_such_file_or_directory)||status.type()!=std::filesystem::file_type::not_found)
  throw DiagnosticAdmissionRejected("persistent diagnostic refuses inaccessible or existing output directory");
 const char* links=std::getenv("BB_NR_QUALITY_CAPTURE_LINKS");
 request.capture_links=links&&!std::strcmp(links,"1");
 return request;
}
}
