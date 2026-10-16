// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "bbport_gnm_hooks.h"
#include <algorithm>
#include <array>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cstdint>
#include <mutex>
#include <optional>
#include <span>
#include <utility>
#include <sys/syscall.h>
#include <unistd.h>
namespace BbPm4Cold {
inline bool Enabled(){static const bool on=[] {const char* v=std::getenv("BB_NR_PM4_COLD_TRACE");return v&&std::strcmp(v,"1")==0;}();return on;}
struct Framing {bool valid=true;size_t offset=0;uint32_t word=0;const char* reason="complete";};
inline Framing Validate(std::span<const uint32_t> words){
 for(size_t at=0;at<words.size();){const auto h=words[at];const unsigned type=h>>30;size_t n=1;
  if(type==3)n=((((h>>16)&0x3fffu)+1)&0x3fffu)+1; // exact local NumWords()+header, including single-word NOP padding
  else if(type!=2)return {false,at,h,"non_type2_type3"};
  if(n>words.size()-at)return {false,at,h,"packet_exceeds_submission"};
  at+=n;
 }return {};
}
inline uint64_t Digest(std::span<const uint32_t> words){uint64_t h=14695981039346656037ull;for(auto w:words)for(unsigned n=0;n<4;n++){h^=(w>>(n*8))&255;h*=1099511628211ull;}return h;}
struct Producer {uint64_t sequence=0,time_ns=0,end_ns=0;uintptr_t address=0,callsite=0;uint32_t requested=0,footprint=0,snapshot_dw=0;long tid=0;const char* name="";std::array<uint32_t,40> head{};BbGnmHooks::ProducerContext context{};};
inline void PrintProducer(const Producer& r,const char* phase){
  std::fprintf(stderr,"PM4_COLD_PRODUCER seq=%llu name=%s cmd=%#llx requested_dw=%u logical_footprint_dw=%u logical_end=%#llx physical_store_count_unknown=1 callsite=%#llx tid=%ld begin_monotonic_ns=%llu end_monotonic_ns=%llu postwrite_snapshot_atomic=0 snapshot_dw=%u snapshot_truncated=%u NOP_payload_may_be_residual=1 postwrite_words=",(unsigned long long)r.sequence,r.name,(unsigned long long)r.address,r.requested,r.footprint,(unsigned long long)(r.address+uint64_t(r.footprint)*4),(unsigned long long)r.callsite,r.tid,(unsigned long long)r.time_ns,(unsigned long long)r.end_ns,r.snapshot_dw,r.snapshot_dw<r.footprint);
  for(unsigned i=0;i<r.snapshot_dw;i++)std::fprintf(stderr,"%08x,",r.head[i]);
  std::fprintf(stderr," phase=%s\n",phase);
  const auto& c=r.context;
  std::fprintf(stderr,"PM4_COLD_CONTEXT seq=%llu known=%u read_failed=%u exact_record=%u record_type_matches=%u record_target_matches=%u wrapper_offset=%#llx caller=%#llx cb=%#llx base=%#llx end=%#llx cursor=%#llx patcher_caller=%#llx table=%#llx records=%#llx payload=%#llx record_count=%llu record_index=%llu record_target=%#llx record_payload=%#llx snapshot_atomic=0 no_repair=1 phase=%s\n",
   (unsigned long long)r.sequence,c.known,c.read_failed,c.exact_record,c.record_type_matches,c.record_target_matches,
   (unsigned long long)c.wrapper,(unsigned long long)c.caller,(unsigned long long)c.cb,
   (unsigned long long)c.base,(unsigned long long)c.end,(unsigned long long)c.cursor,
   (unsigned long long)c.patcher_caller,(unsigned long long)c.table,
   (unsigned long long)c.records,(unsigned long long)c.payload,(unsigned long long)c.record_count,
   (unsigned long long)c.exact_record_index,(unsigned long long)c.record_target,(unsigned long long)c.record_payload,phase);
  if(c.exact_record){
   std::fprintf(stderr,"PM4_COLD_RECORD seq=%llu words=%08x,%08x,%08x, payload_words=",(unsigned long long)r.sequence,c.record[0],c.record[1],c.record[2]);
   for(unsigned i=0;i<std::min<unsigned>(c.payload_word_count,10);++i)std::fprintf(stderr,"%08x,",c.payload_words[i]);
   std::fprintf(stderr," phase=%s\n",phase);
  }
  if(c.known){
   std::fprintf(stderr,"PM4_COLD_BEFORE seq=%llu words=",(unsigned long long)r.sequence);
   for(unsigned i=0;i<std::min<unsigned>(c.target_word_count,40);++i)std::fprintf(stderr,"%08x,",c.target_before[i]);
   std::fprintf(stderr," phase=%s\n",phase);
  }
}
struct Store {std::mutex mutex;std::array<Producer,256> ring{};uint64_t sequence=0;unsigned dumps=0;
 // Reserve eight sites for CS so early PS/VS callers cannot consume its loading controls.
 std::array<std::pair<uint64_t,uint64_t>,16> control_sites{};unsigned control_count=0,control_cs_count=0,control_other_count=0;
};
inline Store& Records(){static Store s;return s;}
class ShaderWrite {
 std::optional<Producer> record;uint32_t* pointer=nullptr;
public:
 ShaderWrite(const char* name,uint32_t* p,uint32_t requested,uintptr_t caller,
             BbGnmHooks::ProducerContext (*capture)(const uint32_t*)=nullptr){
  if(!Enabled())return;
  auto& r=record.emplace();
  pointer=p;r.name=name;r.address=reinterpret_cast<uintptr_t>(p);r.callsite=caller;r.requested=requested;r.tid=syscall(SYS_gettid);
  r.time_ns=std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now().time_since_epoch()).count();
  if(capture)r.context=capture(p);
 }
 // Logical command extent, not a count of physical stores. Trailing NOP payload can be stale.
 void Footprint(uint32_t count){if(record)record->footprint=count;}
 ~ShaderWrite(){
  if(!record)return;
  auto& r=*record;
  r.end_ns=std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now().time_since_epoch()).count();
  if(pointer&&r.footprint){
   r.snapshot_dw=std::min<size_t>({r.footprint,r.requested,r.head.size()});
   std::memcpy(r.head.data(),pointer,r.snapshot_dw*4);
  }
  auto& s=Records();std::lock_guard lock(s.mutex);r.sequence=++s.sequence;s.ring[(r.sequence-1)%s.ring.size()]=r;
  if(r.footprint&&r.snapshot_dw&&r.context.known&&s.control_count<s.control_sites.size()){
   const auto site=std::pair{r.context.wrapper,r.context.caller};
   const bool cs=r.context.wrapper==0x1075840;
   auto& count=cs?s.control_cs_count:s.control_other_count;
   const auto begin=s.control_sites.begin()+(cs?0:8),end=begin+count;
   if(count<8&&std::find(begin,end,site)==end){
    begin[count++]=site;++s.control_count;
    PrintProducer(r,"control_sample");std::fflush(stderr);
   }
  }
 }
};
using CsWrite = ShaderWrite;
// At most four failure dumps/process; full DCB only <=64KiB. Never dereference guest pointers.
inline void Dump(const char* phase,uintptr_t guest,std::span<const uint32_t> copied,Framing at_submit,uint64_t submit_digest,uint64_t copy_begin_ns=0,uint64_t copy_end_ns=0,long copy_tid=0){
 if(!Enabled())return;
 auto& s=Records();std::lock_guard lock(s.mutex);if(s.dumps>=4)return;++s.dumps;
 const auto now=Validate(copied);const auto digest=Digest(copied);
 std::fprintf(stderr,"PM4_COLD_TRACE phase=%s dump=%u guest=%#llx copied=%#llx bytes=%zu submit_framing_ok=%u submit_bad_offset=%zu submit_bad_word=%08x current_framing_ok=%u current_bad_offset=%zu snapshot_digest_same=%u digest_scope=FNV64_diagnostic_not_SHA framing_scope=packet_lengths_only_not_opcode_validity no_repair=1\n",phase,s.dumps,(unsigned long long)guest,(unsigned long long)reinterpret_cast<uintptr_t>(copied.data()),copied.size_bytes(),at_submit.valid,at_submit.offset,at_submit.word,now.valid,now.offset,digest==submit_digest);
 std::fprintf(stderr,"PM4_COLD_COPY_WINDOW begin_monotonic_ns=%llu end_monotonic_ns=%llu tid=%ld scope=DCB_memcpy_not_atomic\n",(unsigned long long)copy_begin_ns,(unsigned long long)copy_end_ns,copy_tid);
 const uint64_t first=s.sequence>s.ring.size()?s.sequence-s.ring.size()+1:1;
 std::fprintf(stderr,"PM4_COLD_PRODUCER_COVERAGE ring_capacity=%zu retained_first_seq=%llu retained_last_seq=%llu retained_count=%llu evicted_count=%llu coverage_complete=0 coverage_scope=tracked_shader_calls_only snapshots_atomic=0 NOP_payload_may_be_residual=1\n",s.ring.size(),(unsigned long long)(s.sequence?first:0),(unsigned long long)s.sequence,(unsigned long long)std::min<uint64_t>(s.sequence,s.ring.size()),(unsigned long long)(s.sequence> s.ring.size()?s.sequence-s.ring.size():0));
 for(uint64_t seq=first;seq<=s.sequence;seq++){
  const auto& r=s.ring[(seq-1)%s.ring.size()];const uintptr_t end=guest+copied.size_bytes();
  if(r.address>=end||r.address+uint64_t(r.requested)*4<=guest)continue;
  PrintProducer(r,phase);
 }
 if(copied.size_bytes()<=65536){for(size_t i=0;i<copied.size();i+=8){std::fprintf(stderr,"PM4_COLD_DCB guest_offset=%#zx:",i*4);for(size_t j=i;j<std::min(i+8,copied.size());j++)std::fprintf(stderr," %08x",copied[j]);std::fputc('\n',stderr);}}
 else std::fprintf(stderr,"PM4_COLD_DCB full_dump_omitted=1 reason=above_64KiB\n");
 std::fflush(stderr);
}
}
