// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "../render/native_dev_paths.h"
#include "../render/nr_runtime_control.h"
#include <atomic>
#include <cerrno>
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/syscall.h>
#include <unistd.h>

// A user scene attestation only. This marker grants no GPU or retirement proof.
namespace BbNRUiStart {
enum class Result { Published, AlreadyRequested, Rejected };
struct Scope { std::string run, session, request; };
inline bool Valid(const Scope& s) {
    return NativeRuntimeModel::Daily() && BbNativeAsyncPreSr::ValidDevRun(s.run,s.session) &&
           s.request==s.run+"/nr-ui-start-request";
}
inline Scope Environment() {
    const auto value=[](const char* name){const char* p=std::getenv(name);return std::string(p?p:"");};
    return {value("BB_NR_NATIVE_ASYNC_RUN_DIR"),value("BB_NR_NATIVE_ASYNC_SESSION"),value("BB_NR_UI_START_REQUEST")};
}
inline bool Available() { return Valid(Environment()); }
inline bool RegularPrivate(int fd) {
    struct stat st{};
    return fstat(fd,&st)==0 && S_ISREG(st.st_mode) && st.st_uid==getuid() &&
           (st.st_mode&07777)==0600 && st.st_nlink==1;
}
inline bool ExactExisting(int dir,const std::string& token) {
    const int fd=openat(dir,"nr-ui-start-request",O_RDONLY|O_NONBLOCK|O_CLOEXEC|O_NOFOLLOW);
    if(fd<0)return false;
    bool valid=RegularPrivate(fd);
    char bytes[66]{};ssize_t size=-1;
    if(valid) { do {size=read(fd,bytes,sizeof(bytes));} while(size<0&&errno==EINTR); }
    valid=valid&&size==static_cast<ssize_t>(token.size())&&std::string_view(bytes,size)==token;
    close(fd);return valid;
}
// Walk from / with directory descriptors so even a parent swapped after
// canonical validation cannot make publication traverse a symlink.
inline int OpenRun(const std::string& run) {
    int dir=open("/",O_RDONLY|O_DIRECTORY|O_CLOEXEC);
    if(dir<0)return -1;
    size_t begin=1;
    while(begin<run.size()) {
        const size_t end=run.find('/',begin);
        const auto part=run.substr(begin,end==std::string::npos?std::string::npos:end-begin);
        const int next=openat(dir,part.c_str(),O_RDONLY|O_DIRECTORY|O_CLOEXEC|O_NOFOLLOW);
        close(dir);
        if(next<0)return -1;
        dir=next;
        if(end==std::string::npos)break;
        begin=end+1;
    }
    return dir;
}
using Write = ssize_t (*)(int,const void*,size_t);
inline int RenameNoReplace(int dir,const char* temporary) {
#ifdef SYS_renameat2
    return static_cast<int>(syscall(SYS_renameat2,dir,temporary,dir,"nr-ui-start-request",1u /* RENAME_NOREPLACE */));
#else
    (void)dir;(void)temporary;errno=ENOSYS;return -1;
#endif
}
struct Operations {
    Write write=::write;
    int (*sync)(int)=::fsync;
    int (*close)(int)=::close;
    int (*rename)(int,const char*)=RenameNoReplace;
};
inline Result Publish(const Scope& s,Operations ops={}) {
    if(!Valid(s))return Result::Rejected;
    const int dir=OpenRun(s.run);
    if(dir<0)return Result::Rejected;
    const auto token=s.session+"\n";
    static std::atomic<unsigned long long> sequence{0};
    std::string temporary;
    int fd=-1;
    for(unsigned tries=0;tries<64;tries++) {
        temporary=".nr-ui-start-request-"+std::to_string(getpid())+"-"+std::to_string(sequence.fetch_add(1))+".tmp";
        fd=openat(dir,temporary.c_str(),O_WRONLY|O_CREAT|O_EXCL|O_NOFOLLOW|O_CLOEXEC,0600);
        if(fd>=0||errno!=EEXIST)break;
    }
    if(fd<0){::close(dir);return Result::Rejected;}
    // The final pathname cannot exist until every write, fsync and close succeeds.
    // Retain failed private temporaries as evidence; never remove another file.
    bool ok=RegularPrivate(fd);size_t done=0;
    while(ok&&done<token.size()) {
        const ssize_t count=ops.write(fd,token.data()+done,token.size()-done);
        if(count<0&&errno==EINTR)continue;
        if(count<=0||static_cast<size_t>(count)>token.size()-done){ok=false;break;}
        done+=static_cast<size_t>(count);
    }
    if(ok)ok=ops.sync(fd)==0;
    if(ops.close(fd)!=0)ok=false;
    Result result=Result::Rejected;
    if(ok) {
        if(ops.rename(dir,temporary.c_str())==0)result=Result::Published;
        else if(errno==EEXIST&&ExactExisting(dir,token))result=Result::AlreadyRequested;
    }
    ::close(dir);
    return result;
}
inline Result Publish() {return Publish(Environment());}
// An explicit Enable action stages display intent before the runner can observe
// the scene marker. A continued save may have persisted display bypass. This is
// only a mailbox request: no model apply, history reset or readiness acknowledgment.
inline Result PublishEnabled(const Scope& s,Operations ops={}) {
    if(!Valid(s))return Result::Rejected;
    auto config=BbNR::ReadRequest().config;
    config.display_bypass=false;
    if(!BbNR::Submit(config))return Result::Rejected;
    return Publish(s,ops);
}
inline Result PublishEnabled() {return PublishEnabled(Environment());}
}
