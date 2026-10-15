// SPDX-License-Identifier: GPL-3.0-or-later
#include "native_persistent_history_diagnostic.h"
#include "video_core/buffer_cache/buffer.h"
#include "vk_instance.h"
#include "vk_scheduler.h"
#include <nlohmann/json.hpp>
#include <atomic>
#include <bit>
#include <cerrno>
#include <condition_variable>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <mutex>
#include <stdexcept>
#include <thread>
namespace Vulkan {
namespace {
constexpr unsigned MaxFrames=6;
constexpr uint64_t ManifestLimit=16384;
constexpr std::array<uint64_t,6> Sizes{uint64_t(1280)*720*8,uint64_t(1280)*720*8,uint64_t(1280)*720*8,uint64_t(1280)*720*16,32,4};
constexpr uint64_t FrameBytes=Sizes[0]+Sizes[1]+Sizes[2]+Sizes[3]+Sizes[4]+Sizes[5];
static_assert(FrameBytes*MaxFrames<256ull*1024*1024);
uint64_t Number(const char* name,uint64_t fallback,uint64_t max){
 const char* s=std::getenv(name);if(!s)return fallback;
 char* end=nullptr;errno=0;const auto v=std::strtoull(s,&end,10);
 if(errno==ERANGE||!*s||*s=='-'||!end||*end||!v||v>max)throw std::runtime_error("invalid bounded persistent diagnostic configuration");return v;
}
std::string Sha256(std::span<const uint8_t> bytes){
 constexpr std::array<uint32_t,64> k{0x428a2f98,0x71374491,0xb5c0fbcf,0xe9b5dba5,0x3956c25b,0x59f111f1,0x923f82a4,0xab1c5ed5,0xd807aa98,0x12835b01,0x243185be,0x550c7dc3,0x72be5d74,0x80deb1fe,0x9bdc06a7,0xc19bf174,0xe49b69c1,0xefbe4786,0x0fc19dc6,0x240ca1cc,0x2de92c6f,0x4a7484aa,0x5cb0a9dc,0x76f988da,0x983e5152,0xa831c66d,0xb00327c8,0xbf597fc7,0xc6e00bf3,0xd5a79147,0x06ca6351,0x14292967,0x27b70a85,0x2e1b2138,0x4d2c6dfc,0x53380d13,0x650a7354,0x766a0abb,0x81c2c92e,0x92722c85,0xa2bfe8a1,0xa81a664b,0xc24b8b70,0xc76c51a3,0xd192e819,0xd6990624,0xf40e3585,0x106aa070,0x19a4c116,0x1e376c08,0x2748774c,0x34b0bcb5,0x391c0cb3,0x4ed8aa4a,0x5b9cca4f,0x682e6ff3,0x748f82ee,0x78a5636f,0x84c87814,0x8cc70208,0x90befffa,0xa4506ceb,0xbef9a3f7,0xc67178f2};
 std::array<uint32_t,8> h{0x6a09e667,0xbb67ae85,0x3c6ef372,0xa54ff53a,0x510e527f,0x9b05688c,0x1f83d9ab,0x5be0cd19};
 const size_t padded=((bytes.size()+9+63)/64)*64;const uint64_t bits=uint64_t(bytes.size())*8;
 for(size_t offset=0;offset<padded;offset+=64){
  std::array<uint32_t,64> words{};
  for(size_t j=0;j<64;++j){size_t n=offset+j;uint8_t v=0;
   if(n<bytes.size())v=bytes[n];else if(n==bytes.size())v=0x80;
   else if(n>=padded-8)v=uint8_t(bits>>((padded-1-n)*8));
   words[j/4]|=uint32_t(v)<<((3-j%4)*8);
  }
  for(unsigned j=16;j<64;++j){const auto a=words[j-15],b=words[j-2];words[j]=words[j-16]+(std::rotr(a,7)^std::rotr(a,18)^(a>>3))+words[j-7]+(std::rotr(b,17)^std::rotr(b,19)^(b>>10));}
  auto a=h[0],b=h[1],c=h[2],d=h[3],e=h[4],f=h[5],g=h[6],z=h[7];
  for(unsigned j=0;j<64;++j){const auto t1=z+(std::rotr(e,6)^std::rotr(e,11)^std::rotr(e,25))+((e&f)^(~e&g))+k[j]+words[j];const auto t2=(std::rotr(a,2)^std::rotr(a,13)^std::rotr(a,22))+((a&b)^(a&c)^(b&c));z=g;g=f;f=e;e=d+t1;d=c;c=b;b=a;a=t1+t2;}
  h[0]+=a;h[1]+=b;h[2]+=c;h[3]+=d;h[4]+=e;h[5]+=f;h[6]+=g;h[7]+=z;
 }
 std::string out;out.reserve(64);constexpr char hex[]="0123456789abcdef";
 for(auto v:h){for(int shift=28;shift>=0;shift-=4)out.push_back(hex[(v>>shift)&15]);}
 return out;
}
struct Proof {std::atomic<uint64_t> observed{0};std::atomic<bool> written{false},failed{false};uint64_t target=0;};
}
struct NativePersistentHistoryDiagnostic::Impl {
 struct Frame {
  std::array<std::unique_ptr<VideoCore::Buffer>,6> staging;
  Snapshot snapshot;std::shared_ptr<Proof> proof=std::make_shared<Proof>();
 };
 std::array<Frame,MaxFrames> frames;
 std::atomic<unsigned> recorded{0};
 unsigned limit=6;uint64_t start=0,first=0,after_candidate=1;
 bool directory_created=false;
 const Scheduler* scheduler=nullptr;
 std::atomic<bool> unknown{false};
 std::string directory,session;
 std::mutex mutex;std::condition_variable wake;std::jthread writer;
 explicit Impl(const Instance&);
 void Write(unsigned);
 void Run(std::stop_token);
 bool Ready()const noexcept;
};
NativePersistentHistoryDiagnostic::Impl::Impl(const Instance& instance){
 if(!NativePersistentHistoryDiagnostic::Enabled())throw std::runtime_error("persistent cold diagnostic explicit opt-in required");
 limit=unsigned(Number("BB_NATIVE_HISTORY_DIAGNOSTIC_FRAMES",6,MaxFrames));
 after_candidate=Number("BB_NATIVE_HISTORY_DIAGNOSTIC_AFTER_CANDIDATE",1,1000000);
 if(std::getenv("BB_NATIVE_HISTORY_DIAGNOSTIC_START_SOURCE"))start=Number("BB_NATIVE_HISTORY_DIAGNOSTIC_START_SOURCE",1,UINT64_MAX);
 const char* run=std::getenv("BB_NR_NATIVE_ASYNC_RUN_DIR"),*id=std::getenv("BB_NR_NATIVE_ASYNC_SESSION");
 if(!run||!id||!*id)throw std::runtime_error("persistent diagnostic current run/session required");
 session=id;
 for(char c:session)if(!((c>='a'&&c<='z')||(c>='A'&&c<='Z')||(c>='0'&&c<='9')||c=='-'||c=='_'))throw std::runtime_error("persistent diagnostic unsafe session identifier");
 const auto root=std::filesystem::canonical("/home/arinp/Code/nrbridge-dev/runs");
 const auto actual=std::filesystem::canonical(run);const auto relative=actual.lexically_relative(root);
 if(relative.empty()||*relative.begin()==".."||relative=="."||actual!=std::filesystem::path(run).lexically_normal())throw std::runtime_error("persistent diagnostic must use isolated canonical new run directory");
 directory=(actual/("persistent-history-diagnostic-"+session)).string();
 // All staging is allocated before recording any readback. No render-thread
 // filesystem writes, hashing, mapping invalidation or per-frame allocation.
 if(std::filesystem::exists(directory))throw std::runtime_error("persistent diagnostic refuses existing output directory");
 for(unsigned i=0;i<limit;i++)for(unsigned j=0;j<6;j++)frames[i].staging[j]=std::make_unique<VideoCore::Buffer>(instance,0,Sizes[j],VideoCore::MemoryType::HostCached,"Bounded persistent cold readback");
 writer=std::jthread([this](std::stop_token token){Run(token);});
 std::printf("PERSISTENT_HISTORY_DIAGNOSTIC_INIT frame_limit=%u frame_bytes=%llu staging_bytes=%llu disk_limit_bytes=%llu performance_excluded=1 no_GPU_wait=1\n",limit,(unsigned long long)FrameBytes,(unsigned long long)(FrameBytes*limit),(unsigned long long)((FrameBytes+ManifestLimit)*limit));
}
bool NativePersistentHistoryDiagnostic::Enabled()noexcept{const char* e=std::getenv("BB_NATIVE_HISTORY_DIAGNOSTIC");return e&&std::strcmp(e,"1")==0;}
NativePersistentHistoryDiagnostic::NativePersistentHistoryDiagnostic(const Instance& i):impl(std::make_unique<Impl>(i)){}
NativePersistentHistoryDiagnostic::~NativePersistentHistoryDiagnostic(){
 if(!impl)return;ReportRetirement("destructor");
 if(!Ready()){std::fprintf(stderr,"PERSISTENT_HISTORY_DIAGNOSTIC_HOLD pending_or_unknown=1 staging_retained=1\n");(void)impl.release();return;}
 std::printf("PERSISTENT_HISTORY_DIAGNOSTIC_SUMMARY captured=%u frame_limit=%u first_source=%llu source_window=%u total_binary_bytes=%llu completed_writer=1\n",impl->recorded.load(),impl->limit,(unsigned long long)impl->first,impl->limit,(unsigned long long)(FrameBytes*impl->recorded.load()));
 impl->writer.request_stop();impl->wake.notify_all();if(impl->writer.joinable())impl->writer.join();impl.reset();
 std::printf("PERSISTENT_HISTORY_DIAGNOSTIC_DESTROYED staging=0 writer_joined=1\n");
}
bool NativePersistentHistoryDiagnostic::Record(Scheduler& scheduler,const Snapshot& snapshot){
 auto& p=*impl;if(p.unknown.load(std::memory_order_acquire))return false;
 if(p.scheduler&&p.scheduler!=&scheduler){HoldUnknown();return false;}
 const auto index=p.recorded.load(std::memory_order_acquire);if(index>=p.limit)return false;
 if(!snapshot.current.source||!snapshot.state||!snapshot.guard)return false;for(auto image:snapshot.images)if(!image)return false;
 if(!p.first){if(snapshot.candidate_count<p.after_candidate)return false;if((p.start&&snapshot.current.source<p.start)||(!p.start&&!snapshot.host_history))return false;p.first=snapshot.current.source;}
 // Capture only the bounded contiguous attempted window. A missed current is
 // reported, never silently relabeled as a continuous image sequence.
 if(snapshot.current.source<p.first||snapshot.current.source-p.first>=p.limit)return false;
 const auto expected=index?p.frames[index-1].snapshot.current.source+1:p.first;
 if(snapshot.current.source<expected)return false;
 if(snapshot.current.source!=expected)std::printf("PERSISTENT_HISTORY_DIAGNOSTIC_GAP expected=%llu actual=%llu no_relabel=1\n",(unsigned long long)expected,(unsigned long long)snapshot.current.source);
 p.scheduler=&scheduler;auto& frame=p.frames[index];frame.snapshot=snapshot;frame.proof->target=scheduler.CurrentTick();
 // Publish immutable slot ownership before the first command. On any partial
 // recording exception the owning history quarantine preserves these resources.
 p.recorded.store(index+1,std::memory_order_release);
 auto cmd=scheduler.CommandBuffer();
 for(unsigned j=0;j<4;j++){
  const vk::ImageMemoryBarrier2 before{.srcStageMask=vk::PipelineStageFlagBits2::eAllCommands,.srcAccessMask=vk::AccessFlagBits2::eMemoryRead|vk::AccessFlagBits2::eMemoryWrite,
   .dstStageMask=vk::PipelineStageFlagBits2::eTransfer,.dstAccessMask=vk::AccessFlagBits2::eTransferRead,
   .oldLayout=vk::ImageLayout::eGeneral,.newLayout=vk::ImageLayout::eGeneral,.srcQueueFamilyIndex=VK_QUEUE_FAMILY_IGNORED,.dstQueueFamilyIndex=VK_QUEUE_FAMILY_IGNORED,
   .image=snapshot.images[j],.subresourceRange={vk::ImageAspectFlagBits::eColor,0,1,0,1}};
  cmd.pipelineBarrier2({.imageMemoryBarrierCount=1,.pImageMemoryBarriers=&before});
  cmd.copyImageToBuffer(snapshot.images[j],vk::ImageLayout::eGeneral,frame.staging[j]->Handle(),vk::BufferImageCopy{.imageSubresource={vk::ImageAspectFlagBits::eColor,0,0,1},.imageExtent={1280,720,1}});
  auto after=before;after.srcStageMask=vk::PipelineStageFlagBits2::eTransfer;after.srcAccessMask=vk::AccessFlagBits2::eTransferRead;after.dstStageMask=vk::PipelineStageFlagBits2::eAllCommands;after.dstAccessMask=vk::AccessFlagBits2::eMemoryRead|vk::AccessFlagBits2::eMemoryWrite;
  cmd.pipelineBarrier2({.imageMemoryBarrierCount=1,.pImageMemoryBarriers=&after});
 }
 for(unsigned j=4;j<6;j++){
  const auto source=j==4?snapshot.state:snapshot.guard;
  const vk::BufferMemoryBarrier2 before{.srcStageMask=vk::PipelineStageFlagBits2::eAllCommands,.srcAccessMask=vk::AccessFlagBits2::eMemoryRead|vk::AccessFlagBits2::eMemoryWrite,
   .dstStageMask=vk::PipelineStageFlagBits2::eTransfer,.dstAccessMask=vk::AccessFlagBits2::eTransferRead,.srcQueueFamilyIndex=VK_QUEUE_FAMILY_IGNORED,.dstQueueFamilyIndex=VK_QUEUE_FAMILY_IGNORED,.buffer=source,.offset=0,.size=Sizes[j]};
  cmd.pipelineBarrier2({.bufferMemoryBarrierCount=1,.pBufferMemoryBarriers=&before});cmd.copyBuffer(source,frame.staging[j]->Handle(),vk::BufferCopy{0,0,Sizes[j]});
  auto after=before;after.srcStageMask=vk::PipelineStageFlagBits2::eTransfer;after.srcAccessMask=vk::AccessFlagBits2::eTransferRead;after.dstStageMask=vk::PipelineStageFlagBits2::eAllCommands;after.dstAccessMask=vk::AccessFlagBits2::eMemoryRead|vk::AccessFlagBits2::eMemoryWrite;
  cmd.pipelineBarrier2({.bufferMemoryBarrierCount=1,.pBufferMemoryBarriers=&after});
 }
 std::array<vk::BufferMemoryBarrier2,6> host{};for(unsigned j=0;j<6;j++)host[j]={.srcStageMask=vk::PipelineStageFlagBits2::eTransfer,.srcAccessMask=vk::AccessFlagBits2::eTransferWrite,.dstStageMask=vk::PipelineStageFlagBits2::eHost,.dstAccessMask=vk::AccessFlagBits2::eHostRead,.srcQueueFamilyIndex=VK_QUEUE_FAMILY_IGNORED,.dstQueueFamilyIndex=VK_QUEUE_FAMILY_IGNORED,.buffer=frame.staging[j]->Handle(),.offset=0,.size=Sizes[j]};
 cmd.pipelineBarrier2({.bufferMemoryBarrierCount=6,.pBufferMemoryBarriers=host.data()});
 auto proof=frame.proof;
 // Do not capture Impl: a callback may be delayed beyond a safely retired writer.
 scheduler.DeferPriorityOperation([proof]{proof->observed.store(proof->target,std::memory_order_release);});
 std::printf("PERSISTENT_HISTORY_DIAGNOSTIC_RECORDED source=%llu index=%u target_tick=%llu image_bytes=%llu state_guard_bytes=36 performance_excluded=1\n",(unsigned long long)snapshot.current.source,index,(unsigned long long)proof->target,(unsigned long long)(FrameBytes-36));
 // Writer uses a timed condition wake, so no lifetime-sensitive callback owns it.
 p.wake.notify_all();return true;
}
bool NativePersistentHistoryDiagnostic::Impl::Ready()const noexcept{
 if(unknown.load(std::memory_order_acquire))return false;
 for(unsigned i=0;i<recorded.load(std::memory_order_acquire);i++){const auto& proof=*frames[i].proof;if(proof.observed.load(std::memory_order_acquire)<proof.target||!proof.written.load(std::memory_order_acquire))return false;}return true;
}
bool NativePersistentHistoryDiagnostic::Ready()const noexcept{return impl->Ready();}
void NativePersistentHistoryDiagnostic::HoldUnknown()noexcept{impl->unknown.store(true,std::memory_order_release);impl->wake.notify_all();}
void NativePersistentHistoryDiagnostic::ObserveCompletion(Scheduler& scheduler){
 auto& p=*impl;if(p.unknown.load(std::memory_order_acquire)||!p.scheduler)return;if(p.scheduler!=&scheduler){HoldUnknown();return;}
 for(unsigned i=0;i<p.recorded.load(std::memory_order_acquire);i++){auto proof=p.frames[i].proof;if(proof->target&&proof->target<scheduler.CurrentTick()&&proof->observed.load(std::memory_order_acquire)<proof->target&&scheduler.IsFree(proof->target))proof->observed.store(proof->target,std::memory_order_release);}
 p.wake.notify_all();
}
uint64_t NativePersistentHistoryDiagnostic::StagingBytes()const noexcept{return FrameBytes*impl->limit;}
void NativePersistentHistoryDiagnostic::ReportRetirement(const char* stage)const noexcept{
 const auto& p=*impl;for(unsigned i=0;i<p.recorded.load(std::memory_order_acquire);i++){const auto& f=p.frames[i];std::printf("PERSISTENT_HISTORY_DIAGNOSTIC_RETIRE stage=%s source=%llu target=%llu observed=%llu writer_finished=%u write_failed=%u unknown=%u all_ready=%u\n",stage?stage:"unspecified",(unsigned long long)f.snapshot.current.source,(unsigned long long)f.proof->target,(unsigned long long)f.proof->observed.load(std::memory_order_acquire),f.proof->written.load(std::memory_order_acquire),f.proof->failed.load(std::memory_order_acquire),p.unknown.load(std::memory_order_acquire),Ready());}
}
void NativePersistentHistoryDiagnostic::Impl::Write(unsigned index){
 using J=nlohmann::json;const auto& frame=frames[index];const auto& s=frame.snapshot;
 if(!directory_created){if(!std::filesystem::create_directory(directory))throw std::runtime_error("diagnostic output collision; no overwrite");std::filesystem::permissions(directory,std::filesystem::perms::owner_all,std::filesystem::perm_options::replace);directory_created=true;}
 const auto dir=std::filesystem::path(directory)/("frame-"+std::to_string(index)+"-source-"+std::to_string(s.current.source));
 if(!std::filesystem::create_directory(dir))throw std::runtime_error("diagnostic frame collision; no overwrite");
 const std::array<const char*,6> names{"original-current720.rgba16f","final-guarded720.rgba16f","active-edit-confidence720.rgba16f","active-depth-guide-age720.rgba32f","bank-state.u32","return-guard.u32"};
 J assets=J::array();
 for(unsigned j=0;j<6;j++){
  frame.staging[j]->Invalidate(0,Sizes[j]);const auto bytes=std::span<const uint8_t>(frame.staging[j]->mapped_data.data(),Sizes[j]);
  std::ofstream out(dir/names[j],std::ios::binary);out.write(reinterpret_cast<const char*>(bytes.data()),bytes.size());out.close();if(!out)throw std::runtime_error("persistent diagnostic asset write failed");
  assets.push_back({{"file",names[j]},{"bytes",Sizes[j]},{"sha256",Sha256(bytes)}});
 }
 std::array<uint32_t,8> state{};uint32_t guard=0;std::memcpy(state.data(),frame.staging[4]->mapped_data.data(),32);std::memcpy(&guard,frame.staging[5]->mapped_data.data(),4);
 J handles=J::array();for(auto image:s.images)handles.push_back(reinterpret_cast<uintptr_t>(VkImage(image)));
 J manifest{{"schema","persistent-history-cold-v1"},{"session",session},{"sample_index",index},{"expected_previous_captured_source",index?frames[index-1].snapshot.current.source:0},{"capture_source_gap",index?s.current.source-frames[index-1].snapshot.current.source-1:0},{"current_source",s.current.source},{"previous_source",s.current.previous},
 {"visual_generation",s.current.history},{"params_version",s.current.params},{"real_reset",s.current.sr_reset},{"current_jitter",s.current.viewport},{"previous_jitter",s.current.previous_viewport},
 {"current_projection",s.current.current_projection},{"previous_projection",s.current.previous_projection},{"previous_view_z",s.current.previous_view_z},
 {"source_image_handles",handles},{"image_extent",{1280,720}},{"image_formats_vk",{97,97,97,109}},
 {"active_bank",s.active_bank},{"candidate_bank",s.candidate_bank},{"candidate_origin",s.candidate_origin},{"candidate_current",s.candidate_current},{"candidate_count",s.candidate_count},{"candidate_recorded_this_source",s.candidate_recorded},{"host_history_potential_only",s.host_history},
 {"bank_state_actual",state},{"return_guard_actual",guard},
 {"edit_encoding","RGB=model log2 residual at full strength; A=per-pixel confidence"},
 {"meta_encoding","R=R32 depth; G=opaque packed guide; B=true model contribution age in real source frames; A=true model contribution age milliseconds"},
 {"candidate_state_scope","invalid[0..3] plus optional valid/rejected/changed/oldest counters; counters are not evidence unless separately enabled"},
 {"copy_target_tick",frame.proof->target},{"copy_observed_tick",frame.proof->observed.load(std::memory_order_acquire)},
 {"native_scheduler_identity",reinterpret_cast<uintptr_t>(scheduler)},
 {"GPU_proof","existing same Native Scheduler target completion; no Helper serial comparison or CPU GPU wait"},
 {"capture_scope","Apply-complete currentraw/finalhalf and activebank snapshots; actual SR binding/merge/Present require independent source join"},
 {"performance_excluded",true},{"frame_limit",limit},{"frame_byte_limit",FrameBytes},{"total_byte_limit",FrameBytes*limit},{"assets",assets}};
 const auto json=manifest.dump(2)+"\n";if(json.size()>ManifestLimit)throw std::runtime_error("persistent diagnostic manifest bound exceeded");
 std::ofstream meta(dir/"readback.json.tmp");meta<<json;meta.close();if(!meta)throw std::runtime_error("persistent diagnostic manifest write failed");std::filesystem::rename(dir/"readback.json.tmp",dir/"readback.json");
 std::printf("PERSISTENT_HISTORY_DIAGNOSTIC_COMPLETE source=%llu index=%u actual_GPU_complete=1 bytes=%llu guard_actual=%u active_bank=%u candidate_bank=%u present=REQUIRES_SOURCE_JOIN performance_excluded=1\n",(unsigned long long)s.current.source,index,(unsigned long long)FrameBytes,guard,s.active_bank,s.candidate_bank);
}
void NativePersistentHistoryDiagnostic::Impl::Run(std::stop_token stop){
 while(!stop.stop_requested()){
  if(unknown.load(std::memory_order_acquire))return;
  bool worked=false;const auto count=recorded.load(std::memory_order_acquire);
  for(unsigned i=0;i<count;i++){auto proof=frames[i].proof;
   if(proof->written.load(std::memory_order_acquire)||!proof->target||proof->observed.load(std::memory_order_acquire)<proof->target)continue;
   worked=true;
   try{Write(i);}catch(const std::exception& e){proof->failed.store(true,std::memory_order_release);std::fprintf(stderr,"PERSISTENT_HISTORY_DIAGNOSTIC_FAILED source=%llu reason=%s retry=0 GPU_completed=1\n",(unsigned long long)frames[i].snapshot.current.source,e.what());}
   // Writer completion is a physical retirement fact; a failed write remains
   // explicitly failed evidence, never promoted to successful validation.
   proof->written.store(true,std::memory_order_release);
  }
  if(!worked){std::unique_lock lock(mutex);wake.wait_for(lock,std::chrono::milliseconds(10));}
 }
}
}
