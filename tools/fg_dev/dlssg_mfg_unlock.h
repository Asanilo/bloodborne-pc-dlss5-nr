// SPDX-License-Identifier: GPL-3.0-or-later
// Adapted from Asanilo/OptiScaler-Linux-NR MfgUnlock.cpp (ca1c0287).
// Only the runner-pinned 310.9.1.0 model is admitted. Validate the entire patch
// plan before modifying this process's mapping; never write the DLL on disk.
#pragma once
#include <vector>
#include <algorithm>
#include <cstring>
#include <winver.h>
namespace MfgUnlock {
struct Patch { unsigned char* at; std::vector<unsigned char> bytes; };
template<class T> T Read(const unsigned char* p){T value;std::memcpy(&value,p,sizeof value);return value;}
inline void Apply(HMODULE module) {
 require(module,"MFG model module");
 wchar_t path[32768]{};require(GetModuleFileNameW(module,path,32768)>0,"MFG model path");
 DWORD ignored=0;auto size=GetFileVersionInfoSizeW(path,&ignored);require(size>0,"MFG model version size");
 std::vector<unsigned char> version(size);require(GetFileVersionInfoW(path,0,size,version.data()),"MFG model version");
 VS_FIXEDFILEINFO* info=nullptr;UINT bytes=0;require(VerQueryValueW(version.data(),L"\\",reinterpret_cast<void**>(&info),&bytes)&&bytes>=sizeof(*info),"MFG version record");
 require(info->dwFileVersionMS==((310u<<16)|9u)&&info->dwFileVersionLS==(1u<<16),"MFG requires exact 310.9.1.0 runtime");
 auto* base=reinterpret_cast<unsigned char*>(module);auto* dos=reinterpret_cast<IMAGE_DOS_HEADER*>(base);
 require(dos->e_magic==IMAGE_DOS_SIGNATURE,"MFG PE DOS");auto* nt=reinterpret_cast<IMAGE_NT_HEADERS64*>(base+dos->e_lfanew);
 require(nt->Signature==IMAGE_NT_SIGNATURE&&nt->OptionalHeader.Magic==IMAGE_NT_OPTIONAL_HDR64_MAGIC,"MFG PE64");
 auto* sections=IMAGE_FIRST_SECTION(nt);std::vector<Patch> plan;unsigned advertise=0,validate=0,kernels=0;
 const unsigned char adv[]={0x81,0xfd,0xb0,1,0,0,0x0f,0x8c,0,0,0,0,0xbf,5,0,0,0};
 const unsigned char val[]={0x3d,0xb0,1,0,0,0x0f,0x93,0xc0};
 for(unsigned n=0;n<nt->FileHeader.NumberOfSections;++n){
  const auto& sec=sections[n];require(uint64_t(sec.VirtualAddress)+sec.Misc.VirtualSize<=nt->OptionalHeader.SizeOfImage,"MFG bounded PE section");
  auto* start=base+sec.VirtualAddress;size_t length=sec.Misc.VirtualSize;
  if(sec.Characteristics&IMAGE_SCN_MEM_EXECUTE){
   for(size_t p=0;p+sizeof(adv)<=length;++p)if(!memcmp(start+p,adv,8)&&!memcmp(start+p+12,adv+12,5)){++advertise;plan.push_back({start+p+6,{0x0f,0x1f,0x44,0,0,0x90}});}
   for(size_t p=0;p+sizeof(val)<=length;++p)if(!memcmp(start+p,val,sizeof val)){++validate;plan.push_back({start+p+5,{0xb0,1,0x90}});}
   continue;
  }
  for(size_t p=0;p+16<=length;++p){
   auto* c=start+p;if(Read<uint32_t>(c)!=0xba55ed50||Read<uint16_t>(c+6)!=16)continue;
   auto fat=Read<uint64_t>(c+8);if(!fat||fat>length-p-16)continue;
   unsigned char* blackwell=nullptr;size_t bh=0,bp=0;std::vector<unsigned char*> ada;bool valid=true;
   for(size_t k=16;k<16+fat;){
    if(16+fat-k<32){valid=false;break;}auto* im=c+k;
    auto h=Read<uint32_t>(im+4);auto payload=Read<uint64_t>(im+8);auto arch=Read<uint32_t>(im+28);
    if(h<32||h>16+fat-k||!payload||payload>16+fat-k-h){valid=false;break;}
    if(Read<uint16_t>(im)==1&&arch==120){require(!blackwell,"MFG unique Blackwell PTX");blackwell=im;bh=h;bp=payload;}
    else if(arch==89)ada.push_back(im);
    k+=h+payload;
   }
   if(!valid||!blackwell||ada.empty())continue;
   const char from[]=".target sm_120",to[]=".target sm_89 ";
   auto* at=std::search(blackwell+bh,blackwell+bh+bp,from,from+sizeof(from)-1);
   require(at!=blackwell+bh+bp,"MFG Blackwell target literal");
   require(std::search(at+1,blackwell+bh+bp,from,from+sizeof(from)-1)==blackwell+bh+bp,"MFG unique target literal");
   plan.push_back({at,std::vector<unsigned char>(to,to+sizeof(to)-1)});
   plan.push_back({blackwell+28,{89,0,0,0}});
   for(auto* im:ada)plan.push_back({im+28,{122,0,0,0}});
   ++kernels;p+=15+fat;
  }
 }
 require(advertise==1&&validate==1&&kernels==31,"MFG exact 310.9.1 signatures and 31 kernel containers");
 for(const auto& patch:plan){DWORD old=0;require(VirtualProtect(patch.at,patch.bytes.size(),PAGE_EXECUTE_READWRITE,&old),"MFG memory protection");std::memcpy(patch.at,patch.bytes.data(),patch.bytes.size());DWORD unused=0;require(VirtualProtect(patch.at,patch.bytes.size(),old,&unused),"MFG restore protection");require(FlushInstructionCache(GetCurrentProcess(),patch.at,patch.bytes.size()),"MFG flush instructions");}
 printf("DLSSG_MFG_UNLOCK version=310.9.1.0 advertise=1 validate=1 kernel_containers=%u max_generated=5 disk_modified=0\n",kernels);
}
}
