// SPDX-License-Identifier: GPL-2.0-or-later
#include "gpu/shim/nr_ui_start_request.h"
#include <cassert>
#include <fstream>
#include <iostream>
#include <sstream>
namespace fs=std::filesystem;
using BbNRUiStart::Result;
static int calls=0;
static std::string final_path;
static bool check_enabled_intent=false;
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
    if(check_enabled_intent) {
        assert(!BbNR::ReadRequest().config.display_bypass);
        assert(BbNR::ReadSnapshot().status==BbNR::Status::Unavailable);
    }
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
int main(int argc,char** argv) {
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
    // Continue a saved bypassed display, then exercise the actual Enable publisher.
    // Observe the mailbox before atomic publication, rather than inventing a GPU ACK.
    BbNR::Config saved;saved.display_bypass=true;saved.display_strength=.375f;
    saved.ttl_frames=6;saved.model_style=2;saved.model_intensity=.75f;
    assert(BbNR::Submit(saved));const auto before_start=BbNR::ReadRequest();
    check_enabled_intent=true;
    assert(BbNRUiStart::PublishEnabled(s,{.write=ObservedWrite,.sync=ObservedSync,
        .close=ObservedClose,.rename=ObservedRename})==Result::Published);
    check_enabled_intent=false;
    const auto started=BbNR::ReadRequest();auto expected=saved;expected.display_bypass=false;
    assert(started.config==expected&&started.revision==before_start.revision+1);
    assert(started.reset_seq==before_start.reset_seq&&started.capture_seq==before_start.capture_seq);
    assert(started.model_apply_seq==before_start.model_apply_seq&&started.model_config==before_start.model_config);
    assert(BbNRUiStart::PublishEnabled(s)==Result::AlreadyRequested);
    assert(BbNR::ReadRequest().revision==started.revision);
    assert(BbNR::ReadSnapshot().status==BbNR::Status::Unavailable);
    fs::remove(s.request);
    assert(BbNR::Submit(saved));const auto rejected=BbNR::ReadRequest();
    bad=s;bad.session="other";
    assert(BbNRUiStart::PublishEnabled(bad)==Result::Rejected);
    assert(BbNR::ReadRequest().revision==rejected.revision&&BbNR::ReadRequest().config==saved);
    NoFinal();
    std::cout<<"NR UI intent scope, permissions, duplicate, malformed, symlink, atomic publication, faults, partial and EINTR checks passed\n";
}
