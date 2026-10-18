#!/usr/bin/env python3
"""Execute the private 1.09 GX dispatch with CPU event substitutes, never a GPU.

The original guest bytes stay outside the repository. This exercises the actual
async/sync branches and the production patch, not a replacement dispatch model.
Event substitutes expose the ownership window; this is not a real game PASS.
"""
from pathlib import Path
import subprocess
import tempfile
import unittest
ROOT = Path(__file__).resolve().parents[2]
ELF = ROOT.parent / 'runs/dev-restart-sr-01/profile/out/eboot.elf'

class DispatchOwnership(unittest.TestCase):
    @unittest.skipUnless(ELF.exists(), 'requires private local 1.09 executable')
    def test_actual_dispatch_completion_boundary(self):
        with ELF.open('rb') as f:
            f.seek(0x4000 + 0x21b6aa0)
            code = f.read(0x131)
            f.seek(0x4000 + 0x1d96b09)
            factory = f.read(32)
        source = r'''
#include "bbport_gx_render_owner.h"
#include <array>
#include <atomic>
#include <cassert>
#include <chrono>
#include <condition_variable>
#include <cstdio>
#include <cstring>
#include <mutex>
#include <thread>
#include <sys/mman.h>
using namespace std::chrono_literals;
struct Event { std::mutex mutex;std::condition_variable cv;bool set=false; };
std::array<Event,3> events;
alignas(16) unsigned char object[0xa0]{};
unsigned index(void* p) { auto d=(unsigned char*)p-object; assert(d==0x28||d==0x48||d==0x68);return (d-0x28)/0x20; }
extern "C" void Set(void* p,bool value) {
 auto& e=events[index(p)];{std::lock_guard lock(e.mutex);e.set=value;}e.cv.notify_all();
}
extern "C" int Wait(void* p,int timeout) {
 assert(timeout==-1);auto& e=events[index(p)];std::unique_lock lock(e.mutex);
 assert(e.cv.wait_for(lock,3s,[&]{return e.set;}));return 0;
}
using Dispatch=void(*)(void*,bool,bool,void*);
void absolute_jump(unsigned char* p,void* target) {
 p[0]=0x48;p[1]=0xb8;std::memcpy(p+2,&target,8);p[10]=0xff;p[11]=0xe0;
}
void initialize(unsigned char* image, unsigned char* config) {
 // Execute the actual seven-byte factory store with its original rbp displacement.
 auto* stub=image+0x1000;assert(mprotect(stub,0x1000,PROT_READ|PROT_WRITE)==0);
 const unsigned char head[]={0x55,0x48,0x89,0xfd};std::memcpy(stub,head,sizeof(head));
 std::memcpy(stub+4,image+BbGxRenderOwner::Factory+16,7);stub[11]=0x5d;stub[12]=0xc3;
 assert(mprotect(stub,0x1000,PROT_READ|PROT_EXEC)==0);
 ((void(*)(void*))(stub))(config+0x1e0);
}
void exercise(unsigned char* image,bool patched,bool config_async,bool update) {
 for(auto& e:events)e.set=false;
 events[2].set=true;
 alignas(16) unsigned char config[0x200]{};initialize(image,config);assert(config[0x72]==unsigned(!patched));
 if(!patched)config[0x72]=config_async; // Original synchronous control or original async default.
 config[0x44]=1;
 auto* config_ptr=config;std::memcpy(object+0x88,&config_ptr,8);
 // Real vtable wait call at +0x18; event setters are the only external direct calls.
 std::array<void*,4> vtable{};vtable[3]=(void*)&Wait;auto* vt=vtable.data();
 for(unsigned off:{0x28u,0x48u,0x68u})std::memcpy(object+off,&vt,8);
 std::mutex gate;std::condition_variable cv;bool entered=false,finish=false,returned=false,completed=false;
 int job=7;
 auto dispatch=(Dispatch)(image+BbGxRenderOwner::Dispatch);
 std::thread worker([&]{
   Wait(object+0x28,-1);
   void* observed=nullptr;std::memcpy(&observed,object+0x20,8);assert(observed==&job);
   {std::unique_lock lock(gate);entered=true;cv.notify_all();assert(cv.wait_for(lock,3s,[&]{return finish;}));completed=true;}
   Set(object+0x68,true);
 });
 std::thread caller([&]{dispatch(object,false,update,&job);std::lock_guard lock(gate);returned=true;cv.notify_all();});
 {
   std::unique_lock lock(gate);assert(cv.wait_for(lock,3s,[&]{return entered;}));
   bool before=cv.wait_for(lock,80ms,[&]{return returned;});
   assert(before==(!patched&&config_async));assert(!completed);
   // main may touch its shared context only after dispatch returns. This is the
   // old permitted overlap; the wrapper test independently demonstrates corruption.
   std::printf("patched=%u config_async=%u update=%u caller_before_job_complete=%u\n",patched,config_async,update,before);
   finish=true;cv.notify_all();
 }
 caller.join();worker.join();assert(completed&&returned);
 // A wait-only call must not enqueue another job. Completion is already signaled.
 Set(object+0x28,false);dispatch(object,true,false,nullptr);assert(!events[0].set);
}
int main(int argc,char** argv) {
 assert(argc==3);constexpr size_t bytes=0x5541000;
 auto* image=(unsigned char*)mmap(nullptr,bytes,PROT_READ|PROT_WRITE,MAP_PRIVATE|MAP_ANONYMOUS,-1,0);assert(image!=MAP_FAILED);
 auto* code=image+BbGxRenderOwner::Dispatch;FILE* f=fopen(argv[1],"rb");assert(f);
 assert(fread(code,1,BbGxRenderOwner::DispatchSize,f)==BbGxRenderOwner::DispatchSize);fclose(f);
 f=fopen(argv[2],"rb");assert(f);assert(fread(image+BbGxRenderOwner::Factory,1,BbGxRenderOwner::FactorySize,f)==BbGxRenderOwner::FactorySize);fclose(f);
 auto factory_original=std::array<unsigned char,BbGxRenderOwner::FactorySize>{};std::memcpy(factory_original.data(),image+BbGxRenderOwner::Factory,factory_original.size());
 auto original=std::array<unsigned char,BbGxRenderOwner::DispatchSize>{};std::memcpy(original.data(),code,original.size());
 assert(BbGxRenderOwner::Patch(nullptr,bytes)==BbGxRenderOwner::Result::ImageMismatch);
 assert(BbGxRenderOwner::Patch(image,BbGxRenderOwner::Dispatch+1)==BbGxRenderOwner::Result::ImageMismatch);
 code[0x40]^=1;auto bad=std::array<unsigned char,BbGxRenderOwner::DispatchSize>{};std::memcpy(bad.data(),code,bad.size());
 assert(BbGxRenderOwner::Patch(image,bytes)==BbGxRenderOwner::Result::ImageMismatch);
 assert(!std::memcmp(code,bad.data(),bad.size()));std::memcpy(code,original.data(),original.size());
 image[BbGxRenderOwner::Factory]^=1;
 assert(BbGxRenderOwner::Patch(image,bytes)==BbGxRenderOwner::Result::ImageMismatch);
 assert(image[BbGxRenderOwner::Factory+BbGxRenderOwner::ModeImmediate]==1);
 std::memcpy(image+BbGxRenderOwner::Factory,factory_original.data(),factory_original.size());
 // Keep original relative calls/jumps intact: the setter lives at its actual image offset.
 absolute_jump(image+0x207e780,(void*)&Set);
 alignas(16) unsigned char renderer[0x110]{};auto* renderer_ptr=renderer;
 std::memcpy(image+0x55406c8,&renderer_ptr,8);
 assert(mprotect(image+0x207e000,0x1000,PROT_READ|PROT_EXEC)==0);
 assert(mprotect(image+0x21b6000,0x1000,PROT_READ|PROT_EXEC)==0);
 for(bool mode:{false,true})for(bool update:{false,true})exercise(image,false,mode,update);
 assert(BbGxRenderOwner::Patch(image,bytes)==BbGxRenderOwner::Result::Applied);
 assert(BbGxRenderOwner::Patch(image,bytes)==BbGxRenderOwner::Result::AlreadyApplied);
 assert(!std::memcmp(code,original.data(),original.size()));
 for(size_t i=0;i<factory_original.size();++i)assert(image[BbGxRenderOwner::Factory+i]==(i==BbGxRenderOwner::ModeImmediate?0:factory_original[i]));
 for(bool mode:{false,true})for(bool update:{false,true})exercise(image,true,mode,update);
 assert(munmap(image,bytes)==0);
 puts("actual GX dispatch CPU completion boundary passed; game/GPU validation pending");
}
'''
        # Avoid placing any guest code bytes in tracked sources.
        with tempfile.TemporaryDirectory(prefix='nr-gx-dispatch-') as tmp:
            tmp=Path(tmp);(tmp/'dispatch.bin').write_bytes(code);(tmp/'factory.bin').write_bytes(factory);(tmp/'test.cpp').write_text(source)
            subprocess.run(['c++','-std=c++20','-O2','-pthread','-Wall','-Wextra','-I',str(ROOT/'gpu/shim'),str(tmp/'test.cpp'),'-o',str(tmp/'test')],check=True)
            subprocess.run([str(tmp/'test'),str(tmp/'dispatch.bin'),str(tmp/'factory.bin')],check=True,timeout=15)
if __name__=='__main__':unittest.main()
