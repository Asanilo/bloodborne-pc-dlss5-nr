// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "native_persistent_history.h"
#include "vulkan/vk_common.h"
namespace Vulkan {
class Instance;class Scheduler;
struct RuntimeOutputProgress{unsigned sr_complete=0,present_complete=0,presented=0;bool failed=false;};
RuntimeOutputProgress ReadRuntimeOutputProgress(uint64_t seq);
bool RuntimeOutputAllReady();
void RuntimeCapturePresented(const NativePersistentHistory::CaptureFrame&,bool success);
// Cold-only exact-source output readback. No second SR history is implied.
void RecordRuntimeOutput(const Instance&,Scheduler&,vk::Image,vk::Format,uint32_t,uint32_t,
                         const NativePersistentHistory::CaptureFrame&,const char* stage);
}
