// SPDX-License-Identifier: GPL-3.0-or-later
// Experimental synchronous CPU transport. No cross-process GPU resource ownership.
#pragma once
#include <chrono>
#include <fstream>
#include <map>
#include <sys/stat.h>
#include <sys/wait.h>
#include <thread>
#include <unistd.h>
#include "render/native_pre_sr_spawn_log.h"
#include "upscale/dlssg_camera.h"
#include "upscale/frame_generation_lifecycle.h"

namespace Vulkan {
class DlssgCpuBridge {
    using J=nlohmann::json;
    using Clock=std::chrono::steady_clock;
    std::string root,run,session,log;
    J config;
    pid_t child=-1;
    int wait_status=0;
    bool reaped=false,retired=false,failed=false;
    u64 sequence=0,epoch=0,last_source=0;
    Clock::time_point last_completed{};
    DlssgCamera::Camera previous_camera{};

    static void Need(bool value,const char* reason){if(!value)throw std::runtime_error(reason);}
    static J Read(const std::string& path){Need(std::filesystem::is_regular_file(path)&&!std::filesystem::is_symlink(path)&&std::filesystem::file_size(path)<131072,"bounded DLSSG metadata");std::ifstream f(path);J j;f>>j;return j;}
    static void Atomic(const std::string& path,const J& value){std::ofstream f(path+".tmp",std::ios::binary|std::ios::trunc);const auto text=value.dump()+"\n";f.write(text.data(),text.size());f.close();Need(bool(f)&&std::rename((path+".tmp").c_str(),path.c_str())==0,"DLSSG atomic metadata publish");}
    void CheckChild(){if(reaped)throw std::runtime_error("DLSSG child already exited");int result=waitpid(child,&wait_status,WNOHANG);if(result==child){reaped=true;throw std::runtime_error("DLSSG child exited before consumed terminal");}Need(result==0||errno==EINTR,"DLSSG waitpid poll");}
    template<class Predicate> J Wait(const char* name,Predicate accept){const auto end=Clock::now()+std::chrono::seconds(sequence?5:60);while(Clock::now()<end){const auto path=root+"/"+name;if(std::filesystem::exists(path)){auto j=Read(path);if(accept(j))return j;}CheckChild();std::this_thread::sleep_for(std::chrono::milliseconds(1));}throw std::runtime_error("DLSSG CPU wait timeout; no forced retirement");}
    void Start(){
        Need(child<0,"one DLSSG helper instance");
        struct stat st{};Need(lstat(root.c_str(),&st)==0&&S_ISDIR(st.st_mode)&&st.st_uid==getuid()&&(st.st_mode&0777)==0700,"private DLSSG CPU slot");
        Need(!std::filesystem::exists(root+"/request.json")&&!std::filesystem::exists(root+"/response.json"),"fresh DLSSG slot");
        Atomic(root+"/stream-config.json",J{{"schema","dlssg-cpu-stream-v1"},{"session",session}});
        const std::string ge=config.at("ge_files"),prefix=config.at("prefix"),preloader=ge+"/lib/wine/x86_64-unix/wine-preloader",wine=ge+"/lib/wine/x86_64-unix/wine";
        auto windows=[](const std::string& path){return "Z:"+path;};
        std::vector<std::string> argv{preloader,wine,config.at("executable").get<std::string>(),windows(root),ge,config.at("adapter").get<std::string>()};
        std::vector<char*> av;for(auto& x:argv)av.push_back(x.data());av.push_back(nullptr);
        const std::map<std::string,std::string> overrides{{"WINEPREFIX",prefix},{"WINELOADERNOEXEC","1"},{"WINEDLLOVERRIDES","mscoree,mshtml=;nvapi64=n;dxgi=n;nvcuda=n,b"},{"WINEDLLPATH",ge+"/lib/vkd3d:"+ge+"/lib/wine"},{"DXVK_ENABLE_NVAPI","1"},{"NVIDIA_WINE_DLL_DIR",config.at("ngx_dir")},{"WINEDEBUG","-all,warn+vulkan,err+vulkan"},{"NVPRESENT_ENABLE_SMOOTH_MOTION","0"},{"VK_LOADER_LAYERS_ALLOW",""},{"VK_LOADER_LAYERS_DISABLE","~implicit~"},{"FG_POC_STREAM","1"},{"FG_POC_MODEL",config.at("model")},{"FG_POC_NVCUDA",config.at("cuda")},{"DLSSNR_DISABLE","1"},{"DLSSNR_AUTO_DOWNLOAD","0"}};
        std::vector<std::string> environment;for(char** p=environ;p&&*p;++p){std::string x=*p;const auto key=x.substr(0,x.find('='));if(!overrides.contains(key)&&key!="FG_POC_INPUT_MANIFEST"&&key!="FG_POC_EVALUATE")environment.push_back(x);}for(auto& [k,v]:overrides)environment.push_back(k+"="+v);
        std::vector<char*> ep;for(auto& x:environment)ep.push_back(x.data());ep.push_back(nullptr);
        Need(NativePreSrSpawnLog::Spawn(&child,preloader.c_str(),av.data(),ep.data(),log)==0,"DLSSG helper spawn");
        Atomic(run+"/dlssg-launch.json",J{{"schema","dlssg-cpu-stream-v1"},{"session",session},{"game_pid",getpid()},{"launcher_pid",child},{"argv",argv}});
        auto ready=Wait("stream-ready.json",[&](const J& j){return j.at("session")==session;});
        Need(ready.at("schema")=="dlssg-cpu-stream-v1"&&ready.at("actual_GPU_complete")==true&&ready.at("private_image_sets")==2&&ready.at("native_gpu_readers")==0,"DLSSG real initialized private images");
        std::printf("DLSSG_CPU_BRIDGE_READY session=%s launcher_pid=%d private_image_sets=2 shared_GPU_images=0\n",session.c_str(),child);
    }
    static void WriteBytes(const std::string& path,std::span<const u8> bytes){FILE* f=std::fopen(path.c_str(),"wb");Need(f,"DLSSG CPU input open");bool ok=std::fwrite(bytes.data(),1,bytes.size(),f)==bytes.size();ok=std::fclose(f)==0&&ok;Need(ok,"DLSSG CPU input complete write");}
public:
    DlssgCpuBridge(){const char* path=std::getenv("BB_DLSSG_STREAM_CONFIG");Need(path&&*path,"explicit DLSSG config");config=Read(path);Need(config.at("schema")=="dlssg-native-cpu-v1","DLSSG Native config schema");root=config.at("ipc");run=config.at("run");session=config.at("session");log=run+"/dlssg-helper.log";const char* expected=std::getenv("BB_DLSSG_STREAM_DIR");Need(expected&&root==expected,"exact DLSSG private slot");}
    ~DlssgCpuBridge(){if(child>0&&!retired)std::fprintf(stderr,"DLSSG_NATIVE_HOLD functional=FAIL helper_retirement_unknown=1 launcher_pid=%d\n",child);}
    u64 LastSource()const{return last_source;}
    bool Exchange(const FrameGeneration::Scene& scene,VAddr display,bool reset,DlssgInputCapture& input,VideoCore::Buffer& upload){
        if(failed)return false;
        try{
            if(child<0)Start();
            const auto started=Clock::now();reset=reset||!last_source||scene.source_game_frame!=last_source+1||(last_completed!=Clock::time_point{}&&started-last_completed>std::chrono::milliseconds(250));
            if(reset)++epoch;
            auto camera=DlssgCamera::Prepare(scene,last_source?&previous_camera:nullptr,reset);
            WriteBytes(root+"/input-color.raw",input.ColorBytes());WriteBytes(root+"/input-depth.raw",input.DepthBytes());WriteBytes(root+"/input-motion.raw",input.MotionBytes());
            const u64 next=sequence+1;
            J request{{"schema","dlssg-cpu-stream-v1"},{"session",session},{"sequence",next},{"epoch",epoch},{"source_game_frame",scene.source_game_frame},{"previous_source_game_frame",last_source},{"reset",reset},{"display",display},{"wire_order","game-scene-bytes"},{"camera",camera.constants}};
            Atomic(root+"/request.json",request);
            const auto response=Wait("response.json",[&](const J& j){return j.at("sequence")==next;});
            for(auto& [key,value]:request.items())if(key!="camera")Need(response.at(key)==value,"exact DLSSG response session/epoch/source-pair/display/sequence");
            Need(response.at("actual_GPU_complete")==true&&response.at("native_gpu_readers")==0,"DLSSG output actual private GPU completion");
            const bool eligible=response.at("eligible");
            if(eligible){Need(response.at("result")==1&&(response.at("disable_word").get<unsigned>()&255)==0&&response.at("output_bytes")==upload.SizeBytes(),"DLSSG GPU eligibility");
                const auto path=root+"/output-color.raw";Need(!std::filesystem::is_symlink(path)&&std::filesystem::file_size(path)==upload.SizeBytes(),"DLSSG exact CPU output length");FILE* f=std::fopen(path.c_str(),"rb");Need(f,"DLSSG CPU output open");bool ok=std::fread(upload.mapped_data.data(),1,upload.SizeBytes(),f)==upload.SizeBytes();ok=std::fclose(f)==0&&ok;Need(ok,"DLSSG CPU output complete read");upload.Flush(0,upload.SizeBytes());}
            Atomic(root+"/consumed.json",J{{"session",session},{"sequence",next},{"epoch",epoch},{"source_game_frame",scene.source_game_frame},{"disposition",eligible?"copied_to_Native_owned_staging":"disabled_output_discarded"}});
            sequence=next;last_source=scene.source_game_frame;previous_camera=std::move(camera);last_completed=Clock::now();
            if(sequence<=3||sequence%60==0)std::printf("DLSSG_NATIVE_OUTPUT sequence=%llu source=%llu previous=%llu epoch=%llu eligible=%u helper_ms=%.3f CPU_roundtrip_ms=%.3f display=0x%llx\n",(unsigned long long)sequence,(unsigned long long)last_source,(unsigned long long)request.at("previous_source_game_frame").get<u64>(),(unsigned long long)epoch,eligible,response.at("helper_service_ms").get<double>(),std::chrono::duration<double,std::milli>(last_completed-started).count(),(unsigned long long)display);
            return eligible;
        }catch(const std::exception& error){failed=true;FgLifecycle::held.store(true);std::fprintf(stderr,"DLSSG_NATIVE_HOLD functional=FAIL reason=%s forced_retirement=0\n",error.what());std::fflush(stderr);std::abort();}
    }
    bool Retire(){
        if(retired)return true;
        if(child<0){retired=true;std::printf("DLSSG_CPU_BRIDGE_NO_PEER no_helper_created=1\n");return true;}
        if(failed)return false;
        try{
            Atomic(root+"/stream-stop.json",J{{"session",session},{"sequence",sequence}});
            const auto end=Clock::now()+std::chrono::seconds(30);
            while(!reaped&&Clock::now()<end){int result=waitpid(child,&wait_status,WNOHANG);if(result==child)reaped=true;else{Need(result==0||errno==EINTR,"DLSSG normal waitpid");std::this_thread::sleep_for(std::chrono::milliseconds(1));}}
            Need(reaped&&WIFEXITED(wait_status)&&WEXITSTATUS(wait_status)==0,"DLSSG normal child reap");
            const auto terminal=Read(root+"/stream-retired.json");
            const J expected{{"schema","dlssg-cpu-stream-v1"},{"session",session},{"sequence",sequence},{"epoch",epoch},{"source_game_frame",last_source},{"cpu_slot_consumed",true},{"actual_GPU_complete",true},{"feature_released",true},{"device_destroyed",true},{"native_gpu_readers",0}};
            Need(terminal==expected,"DLSSG exact consumed/private GPU terminal");
            std::ifstream file(log);const std::string text{std::istreambuf_iterator<char>(file),{}};
            const std::string proof="FG_POC_RETIRED actual_GPU_complete=1 feature_released=1 core_shutdown=1 device_destroyed=1 native_readers=0";
            const auto pos=text.find(proof);Need(pos!=std::string::npos&&text.find(proof,pos+proof.size())==std::string::npos&&text.find("FG_POC_HOLD")==std::string::npos,"DLSSG actual unique Helper retirement proof");
            Atomic(run+"/dlssg-retired.json",J{{"terminal",terminal},{"launcher_pid",child},{"wait_status",wait_status},{"Native_staging_GPU_complete",true},{"shared_GPU_resources",0}});
            retired=true;std::printf("DLSSG_CPU_BRIDGE_RETIRED helper_GPU_complete=1 feature_released=1 child_reaped=1 Native_staging_GPU_complete=1 sequence=%llu\n",(unsigned long long)sequence);
            return true;
        }catch(const std::exception& error){failed=true;FgLifecycle::held.store(true);std::fprintf(stderr,"DLSSG_NATIVE_HOLD functional=FAIL retirement=%s\n",error.what());return false;}
    }
};
} // namespace Vulkan
