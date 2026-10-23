// SPDX-License-Identifier: GPL-3.0-or-later
// CPU control/identity for the GPU mailbox. Pixel data never enters this class.
#pragma once
#include <algorithm>
#include <chrono>
#include <map>
#include <thread>
#include <sys/wait.h>
#include "dlssg_mailbox_import.h"
#include "dlssg_directory_wake.h"
#include "../render/native_pre_sr_spawn_log.h"
extern char** environ;
namespace DlssgGpu {
struct Peer {
    using Clock=std::chrono::steady_clock;
    DirectoryWake wake;
    J config;std::string root,run,session;pid_t child=-1,exporter=-1;uint64_t child_ticks=0,exporter_ticks=0;int status=0;bool reaped=false,retired=false;
    explicit Peer(J c):config(std::move(c)),root(config.at("ipc")),run(config.at("run")),session(config.at("session")){}
    ~Peer(){if(child>0&&!retired)std::fprintf(stderr,"DLSSG_GPU_PEER_HOLD normal_retirement_unproved=1\n");}
    static J Read(const std::string& path){Need(std::filesystem::is_regular_file(path)&&!std::filesystem::is_symlink(path)&&std::filesystem::file_size(path)<131072,"GPU bounded regular metadata");std::ifstream f(path);J j;f>>j;return j;}
    static void Atomic(const std::string& path,const J& j){const auto temp=path+".tmp";const auto text=j.dump()+"\n";std::ofstream f(temp,std::ios::binary|std::ios::trunc);f.write(text.data(),text.size());f.close();Need(bool(f)&&std::rename(temp.c_str(),path.c_str())==0,"GPU metadata atomic publish");}
    void Check(){Need(!reaped,"GPU helper already exited");int result=waitpid(child,&status,WNOHANG);if(result==child){reaped=true;throw std::runtime_error("GPU helper premature exit");}Need(result==0||errno==EINTR,"GPU helper waitpid");Need(Ticks(child)==child_ticks,"GPU launcher stable identity");if(exporter>0)Need(Ticks(exporter)==exporter_ticks,"GPU exporter stable identity");}
    template<class Accept>J Wait(const char* name,Accept accept,unsigned seconds=30){auto deadline=Clock::now()+std::chrono::seconds(seconds);while(Clock::now()<deadline){Check();auto path=root+"/"+name;if(std::filesystem::exists(path)){auto j=Read(path);Session(j,session);if(accept(j))return j;}wake.Pause();}throw std::runtime_error(std::string("GPU peer wait timeout: ")+name);}
    J Wait(const char* name,unsigned seconds=30){return Wait(name,[](const J&){return true;},seconds);}
    static std::string Normalize(std::string value){std::replace(value.begin(),value.end(),'\\','/');if(value.rfind("Z:",0)==0)value.erase(0,2);return value;}
    static pid_t Parent(pid_t pid){std::istringstream in(Proc("/proc/"+std::to_string(pid)+"/status"));std::string l;while(std::getline(in,l))if(l.rfind("PPid:",0)==0)return pid_t(std::stol(l.substr(5)));return -1;}
    static bool Descendant(pid_t pid,pid_t ancestor){for(unsigned n=0;n<128&&pid>1;++n){if(pid==ancestor)return true;pid=Parent(pid);}return false;}
    void FindExporter(){
        const std::string exe=config.at("executable"),ge=config.at("ge_files"),adapter=config.at("adapter");
        const auto deadline=Clock::now()+std::chrono::seconds(30);
        while(Clock::now()<deadline){Check();std::vector<std::pair<pid_t,uint64_t>> found;
            for(const auto& entry:std::filesystem::directory_iterator("/proc")){
                const auto name=entry.path().filename().string();if(name.empty()||name.find_first_not_of("0123456789")!=std::string::npos)continue;pid_t pid=pid_t(std::stol(name));
                if(!Descendant(pid,child)||!Descendant(pid,getpid()))continue;uint64_t ticks=Ticks(pid);if(!ticks)continue;
                auto raw=Proc(entry.path().string()+"/cmdline");std::vector<std::string> args;size_t begin=0;for(size_t n=0;n<=raw.size();++n)if(n==raw.size()||raw[n]==0){if(n>begin)args.push_back(Normalize(raw.substr(begin,n-begin)));begin=n+1;}
                if(args.size()<4||args.size()>5)continue;const auto offset=args.size()-4;
                if((args[offset]!=exe&&args[offset]!=std::filesystem::path(exe).filename().string())||args[offset+1]!=root||args[offset+2]!=ge||args[offset+3]!=adapter)continue;
                std::array<char,4096> link{};auto count=readlink((entry.path().string()+"/exe").c_str(),link.data(),link.size()-1);if(count<1)continue;const auto elf=std::string(link.data(),count);
                if(elf!=std::filesystem::canonical(ge+"/lib/wine/x86_64-unix/wine").string()&&elf!=std::filesystem::canonical(ge+"/lib/wine/x86_64-unix/wine-preloader").string())continue;
                std::istringstream maps(Proc(entry.path().string()+"/maps"));std::string line;bool mapped=false;while(std::getline(maps,line))if(line.size()>exe.size()&&line.substr(line.size()-exe.size()-1)==" "+exe)mapped=true;
                if(mapped&&Ticks(pid)==ticks)found.emplace_back(pid,ticks);
            }
            if(found.size()==1){exporter=found[0].first;exporter_ticks=found[0].second;return;}
            Need(found.size()<2,"GPU unique exporter process");std::this_thread::sleep_for(std::chrono::milliseconds(2));
        }
        throw std::runtime_error("GPU exact exporter identity timeout");
    }
    J Start(MailboxImport& imported){
        Need(child<0,"one GPU peer");struct stat st{};Need(lstat(root.c_str(),&st)==0&&S_ISDIR(st.st_mode)&&st.st_uid==getuid()&&(st.st_mode&0777)==0700,"GPU private IPC root");
        Need(std::filesystem::is_empty(root),"fresh GPU IPC root");
        const bool notified=wake.Open(root);std::printf("DLSSG_GPU_CONTROL_WAKE mode=%s GPU_completion_proof=unchanged\n",notified?"inotify":"poll_1ms");
        const std::string helper_wake=config.value("helper_wake",std::string("poll-v1"));Need(helper_wake=="poll-v1"||helper_wake=="notify-v1","GPU exact Helper wake mode");
        const unsigned count=config.value("multiplier",2u)-1;Need(count>=1&&count<=5,"MFG configured multiplier");
        J settings{{"generated_count",count},{"helper_wake",helper_wake},{"schema",Schema},{"session",session},{"transport","gpu-dmabuf-v1"},{"native_pid",getpid()},{"native_start_ticks",Ticks(getpid())}};if((config.value("pipeline",std::string("sync-v1"))=="consumer-ready-v1"||config.value("pipeline",std::string("sync-v1"))=="consumer-queue-v1")) settings["native_completion_mode"]="split-v1";settings.update(imported.Identity());Atomic(root+"/stream-config.json",settings);
        const std::string ge=config.at("ge_files"),prefix=config.at("prefix"),preloader=ge+"/lib/wine/x86_64-unix/wine-preloader",wine=ge+"/lib/wine/x86_64-unix/wine";
        std::vector<std::string> argv{preloader,wine,config.at("executable"),"Z:"+root,ge,config.at("adapter")};std::vector<char*> av;for(auto& a:argv)av.push_back(a.data());av.push_back(nullptr);
        std::map<std::string,std::string> overrides{{"WINEPREFIX",prefix},{"WINELOADERNOEXEC","1"},{"WINEDLLOVERRIDES","mscoree,mshtml=;nvapi64=n;dxgi=n;nvcuda=n,b"},{"WINEDLLPATH",ge+"/lib/vkd3d:"+ge+"/lib/wine"},{"DXVK_ENABLE_NVAPI","1"},{"NVIDIA_WINE_DLL_DIR",config.at("ngx_dir")},{"WINEDEBUG","-all,warn+vulkan,err+vulkan"},{"NVPRESENT_ENABLE_SMOOTH_MOTION","0"},{"VK_LOADER_LAYERS_ALLOW",""},{"VK_LOADER_LAYERS_DISABLE","~implicit~"},{"FG_POC_STREAM","1"},{"FG_POC_MODEL",config.at("model")},{"FG_POC_NVCUDA",config.at("cuda")},{"DLSSNR_DISABLE","1"},{"DLSSNR_AUTO_DOWNLOAD","0"}};
        overrides["FG_POC_GENERATED_COUNT"]=std::to_string(count);
        std::vector<std::string> env;for(char** p=environ;p&&*p;++p){std::string v=*p;auto key=v.substr(0,v.find('='));if(!overrides.count(key)&&key!="FG_POC_INPUT_MANIFEST"&&key!="FG_POC_EVALUATE")env.push_back(v);}for(auto& [k,v]:overrides)env.push_back(k+"="+v);std::vector<char*> ep;for(auto& v:env)ep.push_back(v.data());ep.push_back(nullptr);
        Need(Vulkan::NativePreSrSpawnLog::Spawn(&child,preloader.c_str(),av.data(),ep.data(),run+"/dlssg-helper.log")==0,"GPU helper spawn");child_ticks=Ticks(child);Need(child_ticks>0,"GPU launcher starttime");Atomic(run+"/dlssg-launch.json",J{{"schema",Schema},{"session",session},{"game_pid",getpid()},{"launcher_pid",child},{"argv",argv}});
        auto pid_request=Wait("gpu-pid-request.json",60u);Need(Normalize(pid_request.at("exe").get<std::string>())==config.at("executable").get<std::string>()&&U(pid_request,"win_pid")>0,"GPU helper exact PE pid request");FindExporter();
        Atomic(root+"/gpu-identity.json",J{{"schema",Schema},{"session",session},{"exporter_pid",exporter},{"exporter_start_ticks",exporter_ticks},{"native_pid",getpid()},{"native_start_ticks",Ticks(getpid())}});
        auto meta=Wait("gpu-meta.json");Need(U(meta,"win_pid")==U(pid_request,"win_pid"),"GPU exact Windows process");Need(Count(meta)==count,"MFG negotiated metadata count");imported.Import(meta,session,exporter,exporter_ticks);
        J pairs=J::array();for(const auto& r:meta.at("resources"))pairs.push_back({{"role",r.at("role")},{"fd",r.at("fd")}});
        Atomic(root+"/gpu-imported.json",J{{"schema",Schema},{"session",session},{"initial_release_serial_echo",meta.at("initial_release_serial")},{"raw_fd_pairs",pairs},{"imported_images",3+count},{"owned_duplicate_fd",0}});
        auto closed=Wait("gpu-fd-retired.json");Need(closed.at("closed_fd")==pairs&&U(closed,"export_raw_fd_owned")==0,"GPU exact raw FD retirement");imported.AuditRawRetired();
        auto ready=Wait("stream-ready.json");Need(Count(ready)==count,"MFG ready count");Need(Split(ready)==((config.value("pipeline",std::string("sync-v1"))=="consumer-ready-v1"||config.value("pipeline",std::string("sync-v1"))=="consumer-queue-v1")),"GPU ready negotiated completion mode");Need(B(ready,"actual_GPU_complete")&&U(ready,"private_image_sets")==2&&ready.at("transport")=="gpu-dmabuf-v1","GPU ready");
        Atomic(run+"/dlssg-gpu-import.json",J{{"schema",Schema},{"session",session},{"exporter_pid",exporter},{"exporter_start_ticks",exporter_ticks},{"metadata",meta},{"FD_retired",closed},{"raw_payloads_absent",true}});
        return ready;
    }
    J Exchange(const J& request,uint64_t prior_serial){Session(request,session);Atomic(root+"/request.json",request);auto r=Wait("response.json",[&](const J& j){return U(j,"sequence")==U(request,"sequence");});for(auto& [k,v]:request.items())if(k!="camera")Need(r.at(k)==v,"GPU exact response request");if(Count(request)>1){Need(r.at("outputs").is_array()&&r.at("outputs").size()==Count(request),"MFG response output count");bool all=true;for(unsigned i=0;i<Count(request);++i){const auto& o=r.at("outputs").at(i);Need(U(o,"index")==i+1&&U(o,"count")==Count(request)&&U(o,"phase_denominator")==Count(request)+1&&B(o,"actual_GPU_complete")&&B(o,"output_released"),"MFG output completion/phase");if(B(o,"eligible"))Need(U(o,"result")==1&&(U(o,"disable_word")&255)==0,"MFG eligible result");all=all&&B(o,"eligible");}Need(B(r,"eligible")==all,"MFG aggregate eligibility");}Need(B(r,"helper_GPU_complete")&&B(r,"output_released")&&U(r,"helper_serial")>prior_serial,"GPU helper actual fence/release");return r;}
    static void PublishConsumed(const std::string& directory,const J& request,uint64_t helper_serial,uint64_t tick){auto ack=Pair(request);ack.update(J{{"helper_serial_echo",helper_serial},{"native_last_use_tick",tick},{"native_GPU_complete",true},{"output_released",true}});if(Count(request)>1){ack["completed_outputs"]=J::array();for(unsigned i=1;i<=Count(request);++i)ack["completed_outputs"].push_back(i);}if(Split(request))ack["native_last_use_domain"]="consumer";Consumed(ack,request,helper_serial);Atomic(directory+"/consumed.json",ack);}
    void Consume(const J& request,uint64_t helper_serial,uint64_t tick){PublishConsumed(root,request,helper_serial,tick);}
    J Retire(MailboxImport& imported,const J& request,uint64_t helper_serial,uint64_t consumer_tick=0){
        const bool split=Split(request);Need(!split||U(request,"sequence")||consumer_tick==0,"GPU zero requests have no consumer completion");const J pair=Pair(request);auto stop=pair;stop["helper_serial_echo"]=helper_serial;Atomic(root+"/stream-stop.json",stop);
        auto ready=Wait("gpu-stop-ready.json");Need(Pair(ready)==pair&&B(ready,"helper_GPU_complete")&&U(ready,"helper_serial")==helper_serial,"GPU exact STOP_READY");
        Need(imported.complete&&(U(request,"sequence")==0||(split?(imported.last_use>=U(request,"native_input_tick")&&consumer_tick>0):imported.last_use>U(request,"native_input_tick"))),"GPU Native final use observed before release");const auto draw_tick=imported.last_use;const auto tick=split?consumer_tick:draw_tick;imported.Destroy();
        auto released=pair;released.update(J{{"native_GPU_complete",true},{"native_last_use_tick",tick},{"native_imported_images_owned",0},{"native_duplicate_fd_owned",0},{"native_shared_used",imported.used}});if(split)released.update(J{{"native_last_use_domain","consumer"},{"native_draw_last_use_tick",draw_tick},{"native_draw_GPU_complete",true},{"native_consumer_GPU_complete",true}});Atomic(root+"/gpu-native-released.json",released);
        const auto deadline=Clock::now()+std::chrono::seconds(30);while(!reaped&&Clock::now()<deadline){int value=waitpid(child,&status,WNOHANG);if(value==child)reaped=true;else{Need(value==0||errno==EINTR,"GPU normal reap");std::this_thread::sleep_for(std::chrono::milliseconds(1));}}
        Need(reaped&&WIFEXITED(status)&&WEXITSTATUS(status)==0,"GPU helper normal exit required");auto terminal=Read(root+"/stream-retired.json");Need(Pair(terminal)==pair&&U(terminal,"helper_serial")==helper_serial&&U(terminal,"native_last_use_tick")==tick&&U(terminal,"shared_images_owned")==0&&U(terminal,"export_raw_fd_owned")==0&&B(terminal,"native_imports_released")&&B(terminal,"actual_GPU_complete")&&B(terminal,"feature_released")&&B(terminal,"device_destroyed"),"GPU complete two-phase terminal");
        if(split)Need(terminal.at("native_last_use_domain")=="consumer"&&U(terminal,"native_draw_last_use_tick")==draw_tick&&B(terminal,"native_draw_GPU_complete")&&B(terminal,"native_consumer_GPU_complete"),"GPU split terminal actual domains");
        const auto text=Proc(run+"/dlssg-helper.log");const std::string proof="FG_POC_RETIRED actual_GPU_complete=1 feature_released=1 core_shutdown=1 device_destroyed=1 native_readers=0";auto pos=text.find(proof);Need(pos!=std::string::npos&&text.find(proof,pos+proof.size())==std::string::npos&&text.find("HOLD")==std::string::npos,"GPU unique helper teardown proof");
        J report{{"terminal",terminal},{"launcher_pid",child},{"wait_status",status},{"Native_shared_GPU_complete",true},{"Native_imports_destroyed",true},{"native_last_use_tick",tick},{"native_shared_used",imported.used},{"last_native_input_tick",U(request,"native_input_tick")},{"last_request_pair",pair}};if(split)report.update(J{{"native_completion_mode","split-v1"},{"native_last_use_domain","consumer"},{"native_draw_last_use_tick",draw_tick},{"native_draw_GPU_complete",true},{"native_consumer_GPU_complete",true}});Atomic(run+"/dlssg-retired.json",report);retired=true;return report;
    }
};
} // namespace DlssgGpu
