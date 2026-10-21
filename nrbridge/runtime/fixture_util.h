#pragma once
// SPDX-License-Identifier: MIT
// Joint fixture utility; own MIT code reused without touching previous evidence.
#define NOMINMAX
#define VK_NO_PROTOTYPES
#define VK_USE_PLATFORM_WIN32_KHR
#include <windows.h>
#include <winternl.h>
#include <vulkan/vulkan.h>
#include <cstdio>
#include <cstdlib>
#include <cstdint>
#include <cstring>
#include <string>
#include <vector>
#include <stdexcept>
#include <array>
#include <cstddef>

struct Stop: std::runtime_error { int code; Stop(int c,const std::string& s):std::runtime_error(s),code(c){} };
static void require(bool ok,const char* s,int code=2){if(!ok)throw Stop(code,s);}
static void api(VkResult r,const char* s){std::printf("API %s result=%d\n",s,int(r));if(r!=VK_SUCCESS)throw Stop(r==VK_ERROR_EXTENSION_NOT_PRESENT?3:2,s);}
static std::string quote(const std::string& s){std::string t="\"";for(char c:s){if(c=='\\'||c=='\"')t+='\\';t+=c;}return t+'\"';}
static std::string module(HMODULE m){char b[32768]{};GetModuleFileNameA(m,b,sizeof(b));return b;}
static std::string hex(const uint8_t* b){char s[VK_UUID_SIZE*2+1]{};for(unsigned i=0;i<VK_UUID_SIZE;i++)std::sprintf(s+2*i,"%02x",b[i]);return s;}
static void atomic_text(const std::string& path,const std::string& text){
 const std::string tmp=path+".tmp";FILE* f=std::fopen(tmp.c_str(),"wb");require(f,"metadata fopen");
 bool ok=std::fwrite(text.data(),1,text.size(),f)==text.size();ok=(std::fclose(f)==0)&&ok;require(ok,"metadata fwrite");
 require(MoveFileExA(tmp.c_str(),path.c_str(),MOVEFILE_REPLACE_EXISTING|MOVEFILE_WRITE_THROUGH),"metadata rename");
}

#include <regex>
#include <cmath>
#include <algorithm>
