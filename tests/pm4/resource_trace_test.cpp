// SPDX-License-Identifier: GPL-2.0-or-later
#include "bbport_resource_trace.h"
#include <algorithm>
#include <cassert>
#include <map>
#include <set>
#include <sstream>
#include <string>
#include <thread>
#include <vector>
#include <sys/wait.h>

using namespace BbResourceTrace;
constexpr uint64_t Address=0xfe1fefe000ULL, Handle=0xfedcba9876543210ULL;
static int child(const std::string& mode) {
    if (mode == "off" || mode == "other") {
        assert(!Enabled() && NewId()==0);
        assert(!Emit({"buffer", "create", 1, Handle, Address, UINT64_MAX}));
        return 0;
    }
    assert(Enabled());
    if (mode == "parallel") {
        std::vector<std::thread> threads;
        for (unsigned t=0;t<8;++t) threads.emplace_back([] {
            for (unsigned i=0;i<32;++i) {
                const auto id=NewId();errno=EDOM;
                assert(Emit({"buffer\nINJECT", "create with spaces", id, Handle, Address,
                             UINT64_MAX, Handle-1, UINT64_MAX-1, 0}));
                assert(errno==EDOM);
            }
        });
        for(auto& thread:threads)thread.join();
    } else {
        assert(mode=="cap");
        std::atomic<uint64_t> accepted{0};std::vector<std::thread> threads;
        for(unsigned t=0;t<8;++t)threads.emplace_back([&] {
            for(uint64_t i=0;i<EventCap/8+2;++i)
                if(Emit({"sampler","retire",NewId(),Handle,0,0}))++accepted;
        });
        for(auto& thread:threads)thread.join();
        assert(accepted==EventCap);
    }
    return 0;
}
static std::string run(const char* self,const char* mode,const char* env) {
    int pipefd[2];assert(pipe(pipefd)==0);pid_t pid=fork();assert(pid>=0);
    if(!pid) {
        close(pipefd[0]);assert(dup2(pipefd[1],STDERR_FILENO)>=0);close(pipefd[1]);
        if(env)setenv("BB_NR_RESOURCE_TRACE",env,1);else unsetenv("BB_NR_RESOURCE_TRACE");
        execl(self,self,"--child",mode,nullptr);_exit(127);
    }
    close(pipefd[1]);std::string output;char bytes[4096];ssize_t n;
    while((n=read(pipefd[0],bytes,sizeof bytes))>0)output.append(bytes,size_t(n));
    close(pipefd[0]);int status;assert(waitpid(pid,&status,0)==pid);
    assert(WIFEXITED(status)&&WEXITSTATUS(status)==0);return output;
}
static std::vector<std::map<std::string,std::string>> records(const std::string& text) {
    std::vector<std::map<std::string,std::string>> out;
    std::istringstream stream(text);std::string line;
    while(std::getline(stream,line)) {
        assert(line.size()+1<=LineBytes);
        std::istringstream fields(line);std::string token;fields>>token;assert(token=="NR_RESOURCE_TRACE");
        std::map<std::string,std::string> record;
        while(fields>>token) {auto i=token.find('=');assert(i!=std::string::npos);assert(record.emplace(token.substr(0,i),token.substr(i+1)).second);}
        assert(record.size()==17);out.push_back(std::move(record));
    }
    assert(text.empty()||text.back()=='\n');return out;
}
int main(int argc,char** argv) {
    if(argc==3&&std::string(argv[1])=="--child")return child(argv[2]);
    assert(run(argv[0],"off",nullptr).empty());assert(run(argv[0],"other","true").empty());
    auto concurrent=records(run(argv[0],"parallel","1"));assert(concurrent.size()==256);
    std::set<std::string> ids,tids;uint64_t seq=0,last_ns=0;
    for(auto& r:concurrent) {
        assert(std::stoull(r["seq"])==++seq);
        auto ns=std::stoull(r["monotonic_ns"]);assert(ns>=last_ns&&ns>0);last_ns=ns;
        assert(r["type"]=="buffer_INJECT"&&r["event"]=="create_with_spaces");
        assert(std::stoull(r["handle"],nullptr,16)==Handle&&std::stoull(r["bda"],nullptr,16)==Address);
        assert(std::stoull(r["bytes"])==UINT64_MAX&&std::stoull(r["retire_tick"])==UINT64_MAX-1);
        assert(std::stoull(r["scheduler"],nullptr,16)==Handle-1);
        assert(r["scope"]=="linked_image_cpu_hooks");
        assert(r["observed_tick"]=="0"&&r["completion"]=="NOT_INFERRED"&&r["unknown_tick"]=="0");
        assert(r["truncated"]=="0");assert(ids.insert(r["id"]).second);tids.insert(r["tid"]);
    }
    assert(tids.size()==8);
    const auto capped_text=run(argv[0],"cap","1");assert(capped_text.size()<=(EventCap+1)*LineBytes);
    auto capped=records(capped_text);assert(capped.size()==EventCap+1);
    assert(capped.back().at("event")=="truncated"&&capped.back().at("truncated")=="1");
    assert(std::stoull(capped.back().at("seq"))==EventCap+1);
    for(size_t i=0;i<EventCap;++i)assert(capped[i].at("truncated")=="0");
}
