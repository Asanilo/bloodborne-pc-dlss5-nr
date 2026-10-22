// SPDX-License-Identifier: GPL-3.0-or-later
// CPU-only shared payload. Sequence/epoch ownership is carried by atomic JSON
// request, response and consumption ACK; this file contains no GPU resources.
#pragma once
#include <atomic>
#include <cstddef>
#include <stdexcept>
#include <string>
#ifdef _WIN32
#include <windows.h>
#else
#include <fcntl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>
#endif

namespace DlssgCpu {
inline constexpr size_t ColorBytes=1920u*1080*4;
inline constexpr size_t GuideBytes=1280u*720*4;
inline constexpr size_t ColorOffset=0;
inline constexpr size_t DepthOffset=ColorBytes;
inline constexpr size_t MotionOffset=DepthOffset+GuideBytes;
inline constexpr size_t OutputOffset=MotionOffset+GuideBytes;
inline constexpr size_t SlotBytes=OutputOffset+ColorBytes;

class MappedSlot {
    void* view=nullptr;
    static void Need(bool ok,const char* text){if(!ok)throw std::runtime_error(text);}
public:
    MappedSlot()=default;
    MappedSlot(const MappedSlot&)=delete;
    MappedSlot& operator=(const MappedSlot&)=delete;
    ~MappedSlot(){Close();}
    void Open(const std::string& path,bool create=false){
        Need(!view,"one CPU payload mapping");
#ifdef _WIN32
        Need(!create,"Native producer creates CPU payload before Wine launch");
        HANDLE file=CreateFileA(path.c_str(),GENERIC_READ|GENERIC_WRITE,
            FILE_SHARE_READ|FILE_SHARE_WRITE,nullptr,OPEN_EXISTING,FILE_ATTRIBUTE_NORMAL,nullptr);
        Need(file!=INVALID_HANDLE_VALUE,"CPU payload open");
        LARGE_INTEGER size{};
        bool exact=GetFileSizeEx(file,&size)&&size.QuadPart==static_cast<LONGLONG>(SlotBytes);
        if(!exact){CloseHandle(file);throw std::runtime_error("CPU payload exact length");}
        HANDLE mapping=CreateFileMappingA(file,nullptr,PAGE_READWRITE,0,0,nullptr);
        CloseHandle(file);Need(mapping!=nullptr,"CPU payload mapping object");
        view=MapViewOfFile(mapping,FILE_MAP_READ|FILE_MAP_WRITE,0,0,SlotBytes);
        CloseHandle(mapping);Need(view!=nullptr,"CPU payload map view");
#else
        int fd=open(path.c_str(),O_RDWR|O_CLOEXEC|O_NOFOLLOW|(create?(O_CREAT|O_EXCL):0),0600);
        Need(fd>=0,"CPU payload exclusive/no-follow open");
        if(create&&ftruncate(fd,SlotBytes)!=0){close(fd);throw std::runtime_error("CPU payload allocate");}
        struct stat st{};
        bool exact=fstat(fd,&st)==0&&S_ISREG(st.st_mode)&&st.st_uid==getuid()&&
            (st.st_mode&0777)==0600&&st.st_nlink==1&&st.st_size==static_cast<off_t>(SlotBytes);
        if(!exact){close(fd);throw std::runtime_error("CPU payload exact private length/mode");}
        void* mapped=mmap(nullptr,SlotBytes,PROT_READ|PROT_WRITE,MAP_SHARED,fd,0);
        close(fd);Need(mapped!=MAP_FAILED,"CPU payload shared map");view=mapped;
#endif
    }
    void Close(){
        if(!view)return;
#ifdef _WIN32
        UnmapViewOfFile(view);
#else
        munmap(view,SlotBytes);
#endif
        view=nullptr;
    }
    unsigned char* At(size_t offset,size_t bytes){
        Need(view&&offset<=SlotBytes&&bytes<=SlotBytes-offset,"CPU payload bounded view");
        return static_cast<unsigned char*>(view)+offset;
    }
    // Publish payload before metadata rename; acquire after reading the exact
    // peer metadata. Neither side may reuse a payload before its consumption ACK.
    static void Publish(){std::atomic_thread_fence(std::memory_order_release);}
    static void Acquire(){std::atomic_thread_fence(std::memory_order_acquire);}
};
} // namespace DlssgCpu
