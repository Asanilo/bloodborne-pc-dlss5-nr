// SPDX-License-Identifier: GPL-3.0-or-later
// CPU-only cross-Linux/Wine atomic-rename test. No Vulkan/NGX entry points.
#include "dlssg_wine_wake.h"
#include <cstdlib>

static bool publish(const std::string& root, const char* name, unsigned sequence) {
    const auto path=root+"/"+name, temp=path+".tmp";
    FILE* file=fopen(temp.c_str(),"w");if(!file)return false;
    const bool wrote=fprintf(file,"%u\n",sequence)>0;
    const bool closed=fclose(file)==0;
    return wrote&&closed&&MoveFileExA(temp.c_str(),path.c_str(),MOVEFILE_REPLACE_EXISTING|MOVEFILE_WRITE_THROUGH);
}
int main(int argc,char** argv) {
    if(argc!=2)return 2;
    setvbuf(stdout,nullptr,_IONBF,0);
    const std::string root=argv[1];DlssgWineWake wake;
    if(!wake.Open(root)){puts("CPU_NOTIFY_UNAVAILABLE");return 3;}
    if(!publish(root,"ready.txt",1))return 2;
    const auto deadline=GetTickCount64()+10000;
    for(unsigned expected=1;expected<=65;) {
        unsigned actual=0;FILE* file=fopen((root+"/request.txt").c_str(),"r");
        if(file){if(fscanf(file,"%u",&actual)!=1)actual=0;fclose(file);}
        if(actual==expected){if(!publish(root,"ack.txt",expected))return 2;++expected;}
        else {if(GetTickCount64()>deadline)return 4;wake.Pause();}
    }
    if(GetModuleHandleA("vulkan-1.dll")||GetModuleHandleA("nvngx_dlssg.dll"))return 5;
    printf("CPU_NOTIFY_PASS requests=65 fallback_count=%u Vulkan_loaded=0\n",wake.Fallbacks());
    return wake.Fallbacks()?3:0;
}
