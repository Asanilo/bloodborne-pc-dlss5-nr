#include "runtime_config.h"
#include "pre_sr_wire.h"
#include <cassert>
#include <functional>
#include <iostream>
using J=nlohmann::json;
static void rejects(std::function<void()> f){bool rejected=false;try{f();}catch(const std::exception&){rejected=true;}assert(rejected);}
int main(){
 NrBridgeRuntime::Resources empty;assert(empty.model.empty()&&empty.forwarder.empty()&&empty.ngx_dir.empty()&&empty.ge_files.empty());
 J good={{"schema","nrbridge-helper-runtime-v1"},{"model","/private/model.dll"},{"forwarder","/build/nvngx.dll_dlssnr.dll"},{"ngx_dir","/system/ngx"},{"ge_files","/runtime/GE/files"}};
 auto r=NrBridgeRuntime::Parse(good);assert(r.model==good["model"]&&r.ge_files==good["ge_files"]);
 for(const auto& key:{"schema","model","forwarder","ngx_dir","ge_files"}){auto bad=good;bad.erase(key);rejects([&]{NrBridgeRuntime::Parse(bad);});bad=good;bad[key]=1;rejects([&]{NrBridgeRuntime::Parse(bad);});}
 for(const char* path:{"relative","Z:\\model.dll","/a/../b","/a/./b","/a//b","/a/","/a\nb"}){auto bad=good;bad["model"]=path;rejects([&]{NrBridgeRuntime::Parse(bad);});}
 auto bad=good;bad["extra"]=true;rejects([&]{NrBridgeRuntime::Parse(bad);});bad=good;bad["schema"]="other-v2";rejects([&]{NrBridgeRuntime::Parse(bad);});
 auto v2=good;v2["schema"]="nrbridge-helper-runtime-v2";
 rejects([&]{NrBridgeRuntime::Parse(v2);});
 for(const auto name:{"Default","A","B"}){v2["nr_preset"]=name;auto selected=NrBridgeRuntime::Parse(v2);assert(selected.preset==(std::string(name)=="A"?1u:std::string(name)=="B"?2u:0u));}
 for(const J& invalid:{J("C"),J(1),J(nullptr),J(true)}){v2["nr_preset"]=invalid;rejects([&]{NrBridgeRuntime::Parse(v2);});}
 auto extra=good;extra["nr_preset"]="A";rejects([&]{NrBridgeRuntime::Parse(extra);});
 rejects([&]{origin_wire::parse("{\"schema\":\"nrbridge-helper-runtime-v1\",\"schema\":\"other\"}");});
 std::cout<<"RUNTIME_CONFIG_CPU_PASS exact schema/path selection; no DLL or GPU loading\n";
}
