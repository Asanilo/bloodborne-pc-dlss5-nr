// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "native_async_pre_sr_policy.h"
#include "native_async_pre_sr_sr_policy.h"
#include "native_pre_sr_sky_policy.h"
#include <bit>
#include <span>
#include <string>
namespace BbNativeAsyncPreSr {
inline constexpr uint64_t DiagnosticImageBytes=uint64_t(1280)*720*8;
inline bool DiagnosticEnabled()noexcept{
 const char* d=std::getenv("BB_NR_NATIVE_ASYNC_PRE_SR_DIAGNOSTIC_ONCE");
 const char* m=std::getenv("BB_NR_NATIVE_ASYNC_MODE");
 return Enabled()&&(!BbNativeAsyncPreSrPlay::Enabled()||BbNativeAsyncPreSrSrPolicy::ExplicitModes())&&d&&!std::strcmp(d,"1")&&m&&!std::strcmp(m,"resident");
}
inline bool MaskDiagnosticEnabled()noexcept{
 const char* d=std::getenv("BB_NR_NATIVE_ASYNC_PRE_SR_MASK_DIAGNOSTIC_ONCE");
 return DiagnosticEnabled()&&BbNativePreSrSkyPolicy::Enabled()&&d&&!std::strcmp(d,"1");
}
struct DiagnosticAdmission {
 CacheIdentity cache;BbNativeAsync::Frame current,origin;
 uint64_t sr_read_tick=0,input_tick=0,helper_serial=0;
 bool actual_selected_image_match=false,SR_success=false,cache_selected=false;
 bool Eligible(const BbNativeAsync::Chain& chain)const noexcept{
  return cache_selected&&SR_success&&actual_selected_image_match&&input_tick&&helper_serial&&sr_read_tick&&
   cache.current==current.source&&cache.origin==origin.source&&cache.epoch==current.epoch&&
   cache.epoch==origin.epoch&&cache.history==current.history&&cache.history==origin.history&&
   cache.params==current.params&&cache.params==origin.params&&BbNativeAsync::Finite(current)&&
   BbNativeAsync::Finite(origin)&&CacheLineageMatches(chain,cache);
 }
};
// Cold writer only: SHA-256 of the exact mapped bytes saved to disk. No render hashing.
inline std::string DiagnosticSha256(std::span<const uint8_t> bytes){
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
} // namespace BbNativeAsyncPreSr
