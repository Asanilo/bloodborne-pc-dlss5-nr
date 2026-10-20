// CPU-only tests of the actual pre-write PM4 register validator; no Vulkan/GPU.
#include "gcn/pm4_register_guard.h"
#include <array>
#include <cassert>
#include <iostream>
#include <vector>
using namespace AmdGpu::Pm4RegisterGuard;
static std::uint32_t header(unsigned op,unsigned values){return 0xc0000000u|(values<<16)|(op<<8);}
int main(){
 // Exact captured leading malformed packet: the subsequent context header must
 // never be interpreted as a legal SH offset (nor its offset as shader value).
 const std::array<std::uint32_t,6> bad{0xc0017602,0xc0016900,0x000001e2,0,0xc0016900,0x1e3};
 auto checked=Validate(bad);assert(!checked&&checked.error==Error::ReservedOffset&&checked.offset==0x6900);
 unsigned register_writes=0,checksum_writes=0,dirty_writes=0;
 if(Validate(bad)){++register_writes;++checksum_writes;++dirty_writes;}
 assert(register_writes==0&&checksum_writes==0&&dirty_writes==0);
 // Legal submitted compute-static-thread-management packet from the same dump.
 const std::array<std::uint32_t,3> good{0xc0017602,0x216,0x1ff};assert(Validate(good));
 for(unsigned op:{0x76u,0x69u,0x79u}){
  std::array<std::uint32_t,4> words{header(op,1),0x3ff,0,0};assert(Validate(words));
  words[0]=header(op,2);assert(Validate(words).error==Error::OutsideBank);
  words[0]=header(op,1);words[1]=0x400;assert(Validate(words).error==Error::OutsideBank);
  words[1]=0x10000;assert(Validate(words).error==Error::ReservedOffset);
  words[1]=0x10000000;assert(bool(Validate(words))==(op!=0x76)); // CI index allowed only context/uconfig
  words[1]=0;words[0]=header(op,2);assert(Validate(std::span(words).first(3)).error==Error::Truncated);
  words[0]=header(op,0);assert(Validate(words).error==Error::MissingValue);
  words[0]=header(op,1);assert(Validate(std::span(words).first(1)).error==Error::Truncated);
  words[0]=0xffff0000u|(op<<8);assert(Validate(words).error==Error::Truncated); // count3fff cannot wrap to one
  std::vector<std::uint32_t> full(0x4001);full[0]=words[0];full[1]=0;assert(Validate(full).error==Error::OutsideBank);
 }
 // Payload values may themselves resemble headers; only the offset is restricted.
 const std::array<std::uint32_t,3> header_payload{header(0x76,1),0x216,0xc0016900};assert(Validate(header_payload));
 const std::array<std::uint32_t,1> other{0xc0001000};assert(Validate(other));
 std::cout<<"PM4_REGISTER_GUARD_CPU_PASS captured packet rejected before write; valid boundaries retained\n";
}
