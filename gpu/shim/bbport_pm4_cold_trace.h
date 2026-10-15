// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include <algorithm>
#include <array>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cstdint>
#include <mutex>
#include <span>
#include <sys/syscall.h>
#include <unistd.h>
namespace BbPm4Cold {
inline bool Enabled(){static const bool on=[] {const char* v=std::getenv("BB_NR_PM4_COLD_TRACE");return v&&std::strcmp(v,"1")==0;}();return on;}
struct Framing {bool valid=true;size_t offset=0;uint32_t word=0;const char* reason="complete";};
inline Framing Validate(std::span<const uint32_t> words){
 for(size_t at=0;at<words.size();){const auto h=words[at];const unsigned type=h>>30;size_t n=1;
  if(type==3)n=((((h>>16)&0x3fffu)+1)&0x3fffu)+1; // exact local NumWords()+header, including single-word NOP padding
  else if(type!=2)return {false,at,h,"non_type2_type3"};
  if(n>words.size()-at)return {false,at,h,"packet_exceeds_submission"};at+=n;
 }return {};
}
inline uint64_t Digest(std::span<const uint32_t> words){uint64_t h=14695981039346656037ull;for(auto w:words)for(unsigned n=0;n<4;n++){h^=(w>>(n*8))&255;h*=1099511628211ull;}return h;}
struct Producer {uint64_t sequence=0,time_ns=0,end_ns=0;uintptr_t address=0,callsite=0;uint32_t requested=0,written=0;long tid=0;const char* name="";std::array<uint32_t,25> head{};};
struct Store {std::mutex mutex;std::array<Producer,64> ring{};uint64_t sequence=0;unsigned dumps=0;};
inline Store& Records(){static Store s;return s;}
class CsWrite {
 Producer record;uint32_t* pointer=nullptr;bool enabled=false;
public:
 CsWrite(const char* name,uint32_t* p,uint32_t requested,uintptr_t caller):enabled(Enabled()){
  if(!enabled)return;pointer=p;record.name=name;record.address=reinterpret_cast<uintptr_t>(p);record.callsite=caller;record.requested=requested;record.tid=syscall(SYS_gettid);
  record.time_ns=std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now().time_since_epoch()).count();
 }
 void Written(uint32_t count){if(enabled)record.written=count;}
 ~CsWrite(){if(!enabled)return;record.end_ns=std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now().time_since_epoch()).count();if(pointer&&record.written)std::memcpy(record.head.data(),pointer,std::min<size_t>(record.written,record.head.size())*4);auto& s=Records();std::lock_guard lock(s.mutex);record.sequence=++s.sequence;s.ring[(record.sequence-1)%s.ring.size()]=record;}
};
// At most four failure dumps/process; full DCB only <=64KiB. Never dereference guest pointers.
inline void Dump(const char* phase,uintptr_t guest,std::span<const uint32_t> copied,Framing at_submit,uint64_t submit_digest){
 if(!Enabled())return;auto& s=Records();std::lock_guard lock(s.mutex);if(s.dumps>=4)return;++s.dumps;
 const auto now=Validate(copied);const auto digest=Digest(copied);
 std::fprintf(stderr,"PM4_COLD_TRACE phase=%s dump=%u guest=%#llx copied=%#llx bytes=%zu submit_framing_ok=%u submit_bad_offset=%zu submit_bad_word=%08x current_framing_ok=%u current_bad_offset=%zu snapshot_digest_same=%u digest_scope=FNV64_diagnostic_not_SHA framing_scope=packet_lengths_only_not_opcode_validity no_repair=1\n",phase,s.dumps,(unsigned long long)guest,(unsigned long long)reinterpret_cast<uintptr_t>(copied.data()),copied.size_bytes(),at_submit.valid,at_submit.offset,at_submit.word,now.valid,now.offset,digest==submit_digest);
 for(uint64_t seq=s.sequence>64?s.sequence-63:1;seq<=s.sequence;seq++){
  const auto& r=s.ring[(seq-1)%64];const uintptr_t end=guest+copied.size_bytes();
  if(r.address>=end||r.address+uint64_t(r.requested)*4<=guest)continue;
  std::fprintf(stderr,"PM4_COLD_PRODUCER seq=%llu name=%s cmd=%#llx requested_dw=%u actual_written_dw=%u actual_end=%#llx callsite=%#llx tid=%ld begin_monotonic_ns=%llu end_monotonic_ns=%llu postwrite_snapshot_atomic=0 postwrite_words=",(unsigned long long)r.sequence,r.name,(unsigned long long)r.address,r.requested,r.written,(unsigned long long)(r.address+uint64_t(r.written)*4),(unsigned long long)r.callsite,r.tid,(unsigned long long)r.time_ns,(unsigned long long)r.end_ns);
  for(unsigned i=0;i<std::min<unsigned>(r.written,r.head.size());i++)std::fprintf(stderr,"%08x,",r.head[i]);std::fputc('\n',stderr);
 }
 if(copied.size_bytes()<=65536){for(size_t i=0;i<copied.size();i+=8){std::fprintf(stderr,"PM4_COLD_DCB guest_offset=%#zx:",i*4);for(size_t j=i;j<std::min(i+8,copied.size());j++)std::fprintf(stderr," %08x",copied[j]);std::fputc('\n',stderr);}}
 else std::fprintf(stderr,"PM4_COLD_DCB full_dump_omitted=1 reason=above_64KiB\n");
 std::fflush(stderr);
}
}
