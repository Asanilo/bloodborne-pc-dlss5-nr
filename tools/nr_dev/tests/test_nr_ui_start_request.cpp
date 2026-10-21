// SPDX-License-Identifier: GPL-2.0-or-later
#include "gpu/shim/nr_ui_start_request.h"
#include "gpu/shim/nr_ui_input_drain.h"
#include <cassert>
#include <fstream>
#include <iostream>
#include <sstream>
namespace fs=std::filesystem;
using BbNRUiStart::Result;
static int calls=0;
static std::string final_path;
static std::string Read(const std::string& path) {
    std::ifstream stream(path);std::ostringstream text;text<<stream.rdbuf();return text.str();
}
static void NoFinal(){assert(!fs::exists(final_path));}
static int observed_fd=-1;
static ssize_t ObservedWrite(int fd,const void* data,size_t size) {
    NoFinal();observed_fd=fd;return ::write(fd,data,size);
}
static int ObservedSync(int fd){NoFinal();return ::fsync(fd);}
static int ObservedClose(int fd){NoFinal();return ::close(fd);}
static int ObservedRename(int dir,const char* name) {
    NoFinal();assert(fcntl(observed_fd,F_GETFD)==-1&&errno==EBADF);
    assert(Read(fs::path(final_path).parent_path()/name)=="ui-test\n");
    return BbNRUiStart::RenameNoReplace(dir,name);
}
static int FailedSync(int){errno=EIO;return -1;}
static int FailedClose(int fd){assert(::close(fd)==0);errno=EIO;return -1;}
static int UnsupportedRename(int,const char*){errno=ENOSYS;return -1;}
static int FailedRename(int,const char*){errno=EIO;return -1;}

static ssize_t InterruptedShortWrite(int fd,const void* data,size_t size) {
    if(calls++==0){errno=EINTR;return -1;}
    return ::write(fd,data,std::min(size,size_t(1)));
}
static ssize_t PartialFailure(int fd,const void* data,size_t) {
    if(calls++==0)return ::write(fd,data,1);
    errno=ENOSPC;return -1;
}
static ssize_t ZeroWrite(int,const void*,size_t){return 0;}
static void ReplayInputDrain() {
    using namespace BbNRUiInput;
    const uint32_t keys[]={Enter,KeypadEnter,Space,Escape,Delete,MouseLeft,South,L3,R3};
    for(const auto key:keys) {
        Drain state;
        // SDL DOWN, then render activates/closes the prompt while physically held.
        assert(!state.Event(key,true));state.Arm();assert(state.Active());
        assert(state.Event(key,false));assert(!state.Active());
        // SDL DOWN+UP were both received before render. ImGui may still be
        // processing DOWN, but a delayed logical activation must not arm drain.
        assert(!state.Event(key,true));assert(!state.Event(key,false));
        state.Arm();assert(!state.Active());
        // Release arriving between logical activation and another render stays clear.
        state.Event(key,true);state.Arm();assert(state.Event(key,false));
        state.Arm();assert(!state.Active());
        // Lost focus clears actual held state as well as published capture state.
        state.Event(key,true);state.Arm();state.Clear();
        assert(!state.Active());state.Arm();assert(!state.Active());
    }
    Drain simultaneous;
    simultaneous.Event(Enter,true);simultaneous.Event(South,true);
    simultaneous.Arm();assert(simultaneous.Active());
    assert(simultaneous.Event(Enter,false));assert(simultaneous.Active());
    assert(simultaneous.Event(South,false));assert(!simultaneous.Active());
    // A second activation input pressed during drain cannot leak into game polling.
    simultaneous.Event(MouseLeft,true);simultaneous.Arm();
    simultaneous.Event(South,true);simultaneous.Event(MouseLeft,false);
    assert(simultaneous.Active());assert(simultaneous.Event(South,false));assert(!simultaneous.Active());
    // Disconnect clears the removed device while retaining a still-held keyboard key.
    simultaneous.Event(Enter,true);simultaneous.Event(South,true);simultaneous.Event(L3,true);
    simultaneous.Arm();simultaneous.Clear(Gamepad);assert(simultaneous.Active());
    assert(simultaneous.Event(Enter,false));simultaneous.Arm();assert(!simultaneous.Active());
}
int main(int argc,char** argv) {
    ReplayInputDrain();
    assert(argc==2);const std::string root=fs::canonical(argv[1]);
    setenv("BB_NR_DAILY","1",1);setenv("NRBRIDGE_HOME",root.c_str(),1);
    fs::create_directories(root+"/runs/ui-test");
    BbNRUiStart::Scope s{root+"/runs/ui-test","ui-test",root+"/runs/ui-test/nr-ui-start-request"};
    final_path=s.request;
    setenv("BB_NR_NATIVE_ASYNC_RUN_DIR",s.run.c_str(),1);
    setenv("BB_NR_NATIVE_ASYNC_SESSION",s.session.c_str(),1);
    setenv("BB_NR_UI_START_REQUEST",s.request.c_str(),1);
    assert(BbNRUiStart::Available());
    assert(BbNRUiStart::Publish()==Result::Published);
    assert(BbNRUiStart::Publish(s)==Result::AlreadyRequested);
    struct stat st{};assert(lstat(s.request.c_str(),&st)==0 && st.st_uid==getuid() && (st.st_mode&07777)==0600);
    auto bad=s;bad.request=s.run+"/elsewhere";assert(BbNRUiStart::Publish(bad)==Result::Rejected);
    bad=s;bad.session="other";assert(BbNRUiStart::Publish(bad)==Result::Rejected);
    unsetenv("BB_NR_DAILY");assert(BbNRUiStart::Publish(s)==Result::Rejected);setenv("BB_NR_DAILY","1",1);
    unsetenv("BB_NR_UI_START_REQUEST");assert(!BbNRUiStart::Available());
    fs::remove(s.request);
    for(const auto& payload:{"","ui-test","other\n","ui-test\nextra"}) {
        {std::ofstream out(s.request);out<<payload;}chmod(s.request.c_str(),0600);
        assert(BbNRUiStart::Publish(s)==Result::Rejected);fs::remove(s.request);
    }
    {std::ofstream out(root+"/target");out<<"ui-test\n";}chmod((root+"/target").c_str(),0600);
    fs::create_symlink(root+"/target",s.request);assert(BbNRUiStart::Publish(s)==Result::Rejected);
    assert(fs::is_symlink(s.request));fs::remove(s.request);
    fs::create_symlink(root+"/absent",s.request);assert(BbNRUiStart::Publish(s)==Result::Rejected);fs::remove(s.request);
    fs::create_hard_link(root+"/target",s.request);assert(BbNRUiStart::Publish(s)==Result::Rejected);fs::remove(s.request);
    fs::create_directory(s.request);assert(BbNRUiStart::Publish(s)==Result::Rejected);fs::remove(s.request);
    assert(mkfifo(s.request.c_str(),0600)==0);assert(BbNRUiStart::Publish(s)==Result::Rejected);fs::remove(s.request);
    {std::ofstream out(s.request);out<<"ui-test\n";}chmod(s.request.c_str(),0644);
    assert(BbNRUiStart::Publish(s)==Result::Rejected);fs::remove(s.request);
    calls=0;assert(BbNRUiStart::Publish(s,{.write=InterruptedShortWrite})==Result::Published);fs::remove(s.request);
    calls=0;assert(BbNRUiStart::Publish(s,{.write=PartialFailure})==Result::Rejected);NoFinal();
    assert(BbNRUiStart::Publish(s,{.write=ZeroWrite})==Result::Rejected);NoFinal();
    assert(BbNRUiStart::Publish(s,{.write=ObservedWrite,.sync=ObservedSync,.close=ObservedClose,.rename=ObservedRename})==Result::Published);
    assert(Read(s.request)=="ui-test\n");fs::remove(s.request);
    const BbNRUiStart::Operations faults[]={
        {.write=PartialFailure}, {.write=ZeroWrite}, {.sync=FailedSync},
        {.close=FailedClose}, {.rename=UnsupportedRename}, {.rename=FailedRename}
    };
    for(const auto& fault:faults) {
        calls=0;assert(BbNRUiStart::Publish(s,fault)==Result::Rejected);NoFinal();
        {std::ofstream out(s.request);out<<"unchanged existing file\n";}chmod(s.request.c_str(),0600);
        struct stat before{},after{};assert(lstat(s.request.c_str(),&before)==0);
        calls=0;assert(BbNRUiStart::Publish(s,fault)==Result::Rejected);
        assert(Read(s.request)=="unchanged existing file\n");assert(lstat(s.request.c_str(),&after)==0);
        assert(before.st_ino==after.st_ino);fs::remove(s.request);
    }
    bool partial_evidence=false;
    for(const auto& entry:fs::directory_iterator(s.run))
        if(entry.path().filename().string().starts_with(".nr-ui-start-request-")&&fs::file_size(entry.path())==1)partial_evidence=true;
    assert(partial_evidence);
    fs::create_directory_symlink(s.run,root+"/runs/alias");bad=s;bad.run=root+"/runs/alias";bad.session="alias";bad.request=bad.run+"/nr-ui-start-request";
    assert(BbNRUiStart::Publish(bad)==Result::Rejected);
    std::cout<<"NR UI intent scope, permissions, duplicate, malformed, symlink, atomic publication, faults, partial, EINTR and input event replay checks passed\n";
}
