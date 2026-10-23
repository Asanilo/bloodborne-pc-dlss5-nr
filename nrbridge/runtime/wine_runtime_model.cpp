#include "native_runtime_model.h"
// SPDX-License-Identifier: GPL-3.0-or-later
// Root interface revision3 only. Native owns Sky; this Helper only sparse immutable-origin NR.
#include "transport.h"
#include "runtime_frame.h"
#include "runtime_config.h"
#include "pre_sr_wire.h"
#include "../forwarder/parameter_abi.h"
#include <optional>
#include <io.h>
#include <sstream>
#include <cerrno>
using Library=HMODULE;
template<class T>static T get(Library l,const char*n){auto f=reinterpret_cast<T>(GetProcAddress(l,n));require(f,n,3);return f;}
static std::wstring wide(const std::string&s){return std::wstring(s.begin(),s.end());}
static std::string winpath(const std::string&s){std::string p="Z:"+s;std::replace(p.begin(),p.end(),'/','\\');return p;}
static NrBridgeRuntime::Resources runtime_resources;
static std::string model_path=winpath(runtime_resources.model);
static std::string forwarder_path=winpath(runtime_resources.forwarder);
static std::string core_path=winpath(runtime_resources.ngx_dir+"/_nvngx.dll");
static std::string nvapi_path=winpath(runtime_resources.ge_files+"/lib/wine/nvapi/x86_64-windows/nvapi64.dll");
static std::string dxgi_path=winpath(runtime_resources.ge_files+"/lib/wine/dxvk/x86_64-windows/dxgi.dll");
static void select_runtime(const Json& config){
 runtime_resources=NrBridgeRuntime::Parse(config);
 model_path=winpath(runtime_resources.model);forwarder_path=winpath(runtime_resources.forwarder);
 core_path=winpath(runtime_resources.ngx_dir+"/_nvngx.dll");
 nvapi_path=winpath(runtime_resources.ge_files+"/lib/wine/nvapi/x86_64-windows/nvapi64.dll");
 dxgi_path=winpath(runtime_resources.ge_files+"/lib/wine/dxvk/x86_64-windows/dxgi.dll");
}


namespace W=origin_wire;
static uint64_t uint_value(const Json&j,const char*k){return W::u(j,k);}
static std::string string_value(const Json&j,const char*k){return W::s(j,k);}
static bool bool_value(const Json&j,const char*k){return W::b(j,k);}
struct PreSrHelper:PreSrTransport {
 using N=LowresProduction;Library fw{},core{},nvapi{},dxgi{};NVSDK_NGX_Parameter*params{};void*feature{};
 std::unique_ptr<N>nr;std::unique_ptr<PreSrFrame>gpu;N::Image color,depth,motion,output;
 std::string mode,expected_device,expected_driver,terminal_reason;uint64_t game_pid=0,game_start_ticks=0,exporter_ticks=0,creation_serial=0,release_serial=0,control_epoch=0;
 uint32_t timestamp_bits=0,service_wall_seconds=600;uint64_t feature_create_calls=0,feature_release_calls=0,evaluates=0,resolves=0;
 bool negative=false,model_initialized=false,feature_created_ever=false,normal_shutdown=false,stopping=false,active=false;double service_start=0,cold_start=0,job_start=0,last_peer_check=-1;
 uint64_t model_generation=0,model_params=1,model_revision=0;NativeRuntimeModel::Settings model_settings;
 W::Counts counts;Json current_input,stop_request,last_release;std::string name_root(const std::string&kind)const{return W::base(kind,session);}
 Json common()const{return W::common(session,mode);}
 Json envelope(const char*op,const char*status,bool gpuProof)const{auto j=common();j.update(Json{{"op",op},{"status",status},{"completed_GPU",gpuProof},{"output_valid",false},{"model_valid",false}});return j;}
 Json terminal(const char*op,const char*status,bool gpuProof)const{auto j=envelope(op,status,gpuProof);j.update(counts.fields());j["control_epoch"]=control_epoch;j["terminal_reason"]=terminal_reason;return j;}
 static bool safe_token(const std::string&s){return !s.empty()&&s.size()<=63&&std::all_of(s.begin(),s.end(),[](char c){return std::isalnum((unsigned char)c)||c=='-'||c=='_';});}
 static std::optional<std::string>try_text(const std::string&path){const auto a=GetFileAttributesA(path.c_str());if(a==INVALID_FILE_ATTRIBUTES){auto e=GetLastError();require(e==ERROR_FILE_NOT_FOUND||e==ERROR_PATH_NOT_FOUND,"unreadable control path",3);return {};}require(!(a&(FILE_ATTRIBUTE_DIRECTORY|FILE_ATTRIBUTE_REPARSE_POINT)),"regular control",3);FILE*f=fopen(path.c_str(),"rb");if(!f){if(errno==ENOENT)return {};throw Stop(3,"control fopen");}std::string t;char b[4096];size_t n;while((n=fread(b,1,sizeof b,f))){t.append(b,n);if(t.size()>65536){fclose(f);throw Stop(2,"control64KiB");}}bool ok=!ferror(f),closed=fclose(f)==0;require(ok&&closed,"control read/close");return t;}
 static std::pair<char,uint64_t>proc_identity(uint64_t pid){auto t=try_text("Z:\\proc\\"+std::to_string(pid)+"\\stat");require(t.has_value(),"numeric real Unix PID proc",3);auto p=t->rfind(')');require(p!=std::string::npos,"proc stat comm delimiter",3);std::istringstream in(t->substr(p+2));std::string v;char state=0;uint64_t ticks=0;for(unsigned f=3;f<=22;f++){require(bool(in>>v),"proc stat22",3);if(f==3){require(v.size()==1,"proc state",3);state=v[0];}if(f==22){require(!v.empty()&&std::all_of(v.begin(),v.end(),[](char c){return c>='0'&&c<='9';}),"proc ticks digits",3);ticks=std::stoull(v);require(ticks>0,"proc ticks",3);}}return{state,ticks};}
 static bool live(char c){return c!='Z'&&c!='X'&&c!='x';}
 void check_peer(bool force=false){const double now=c4_cpu_now_ms();if(!game_pid||(!force&&now-last_peer_check<250))return;auto g=proc_identity(game_pid),e=proc_identity(linux_pid);require(g.second==game_start_ticks&&live(g.first)&&e.second==exporter_ticks&&live(e.first),"PID/start_ticks/nonZombie identity",3);last_peer_check=now;}
 void rotate_daily_logs(){
  if(!NativeRuntimeModel::Daily())return;
  for(FILE* stream:{stdout,stderr}){auto position=ftell(stream);require(position>=0,"daily log position",3);if(position>16*1024*1024){require(fflush(stream)==0&&_chsize_s(_fileno(stream),0)==0&&fseek(stream,0,SEEK_SET)==0,"daily log cap",3);}}
 }
 void service_check(){if(!NativeRuntimeModel::Daily())require(c4_cpu_now_ms()-service_start<600000,"service wall600s; HOLD",3);require(!try_text(dir+"\\native.abort"),"native quarantine; no normal retirement",3);check_peer();}
 void phase_tick(const char*stage){service_check();double t=c4_cpu_now_ms()-cold_start;printf("COLD_PHASE stage=%s elapsed_ms=%.6f budget=120000\n",stage,t);require(t>=0&&t<120000,"cold120s; HOLD",3);}
 void job_check(const char*stage){service_check();double t=c4_cpu_now_ms()-job_start;printf("ORIGIN_JOB_PHASE stage=%s elapsed_ms=%.6f budget=30000\n",stage,t);require(t>=0&&t<30000,"active origin30s; HOLD",3);}
 Json wait_root(const std::string&name){auto start=c4_cpu_now_ms();while(c4_cpu_now_ms()-start<60000){service_check();if(auto t=try_text(dir+"\\"+name))return W::parse(*t);Sleep(5);}throw Stop(3,"host control60s; HOLD "+name);}
 void write_root(const std::string&name,const Json&j){atomic_text(dir+"\\"+name,j.dump()+"\n");}
 void config(){if(BbNRDimensions::Get().enabled)std::printf("%s NRBRIDGE_HELPER_DIMENSIONS_V3 nr=%ux%u\n",BbNRDimensions::RevisionMarker,BbNRDimensions::Width(),BbNRDimensions::Height());auto t=try_text(dir+"\\async-config.json");require(t.has_value(),"async config before spawn",3);auto j=W::parse(*t);W::config(j,session,mode);expected_device=W::s(j,"device_uuid");expected_driver=W::s(j,"driver_uuid");width=BbNRDimensions::Width();height=BbNRDimensions::Height();service_start=c4_cpu_now_ms();for(uint64_t bucket=0;bucket<(NativeRuntimeModel::Daily()?1u:256u);bucket++){auto attrs=GetFileAttributesA((dir+"\\frames\\"+W::bucket_leaf(bucket*64)).c_str());require(attrs!=INVALID_FILE_ATTRIBUTES&&(attrs&FILE_ATTRIBUTE_DIRECTORY)&&!(attrs&FILE_ATTRIBUTE_REPARSE_POINT),"Native precreated all256 bounded buckets",3);}}
 void peer_identity(){auto j=wait_root("native-exporter-identity.json");W::bootstrap(j,common(),"native-exporter-identity.json");game_pid=W::u(j,"game_pid");game_start_ticks=W::u(j,"game_start_ticks");exporter_ticks=W::u(j,"exporter_start_ticks");require(game_pid>1&&game_pid<=INT32_MAX&&W::u(j,"exporter_pid")==linux_pid&&W::u(j,"launcher_pid")>1&&W::u(j,"launcher_pid")<=INT32_MAX,"actual Linux process identities",3);for(auto k:{"descendant_game","descendant_launcher","request_exe_match","unique_run_cmd","exact_pe_maps"})require(W::b(j,k),"actual verified native identity",3);check_peer(true);}
 void environment(){const std::string ge=runtime_resources.ge_files+"/lib/";require(getenv("DXVK_ENABLE_NVAPI")&&std::string(getenv("DXVK_ENABLE_NVAPI"))=="1","NVAPI enable",3);require(getenv("NVIDIA_WINE_DLL_DIR")&&std::string(getenv("NVIDIA_WINE_DLL_DIR"))==runtime_resources.ngx_dir,"host NGX dir",3);require(getenv("WINEDLLPATH")&&std::string(getenv("WINEDLLPATH"))==std::string(ge)+"vkd3d:"+ge+"wine","child exact DLL path",3);require(getenv("WINEDLLOVERRIDES")&&std::string(getenv("WINEDLLOVERRIDES"))=="mscoree,mshtml=;nvapi64=n;dxgi=n","native overrides",3);for(auto k:{"DXVK_ENABLE_NVAPI","NVIDIA_WINE_DLL_DIR","WINEDLLPATH","WINEDLLOVERRIDES","WINEDEBUG"})printf("CHILD_ENV %s=%s\n",k,getenv(k)?getenv(k):"<unset>");}
 void baseline(){require(!in_flight&&creation_serial>0&&feature&&nr->safe,"actual feature creation fence");auto j=envelope("baseline","BASELINE",true);j.update(Json{{"model_init",true},{"feature_created",true},{"feature_create_calls",feature_create_calls},{"creation_GPU_complete",true},{"creation_GPU_serial",creation_serial},{"model_width",width},{"model_height",height},{"cold_init_timeout_ms",120000u},{"control_timeout_ms",60000u},{"gpu_timeout_ms",10000u},{"job_completion_timeout_ms",30000u}});W::bootstrap(j,common(),"async-baseline-SESSION-g0.json");write_root(name_root("baseline"),j);auto a=wait_root(name_root("native-baseline"));W::bootstrap(a,common(),"async-native-baseline-SESSION-g0.json");require(W::s(a,"op")=="baseline_audit"&&W::s(a,"status")=="BASELINE_AUDITED"&&!W::b(a,"completed_GPU")&&!W::b(a,"output_valid")&&!W::b(a,"model_valid")&&W::u(a,"helper_creation_GPU_serial_echo")==creation_serial&&W::s(a,"inventory")=="async-native-fd-baseline.json"&&W::s(a,"helper_inventory")=="async-wine-fd-baseline.json","host baseline proof not GPU",3);}
 void exports(){create_images();color=PreSrFrame::wrap(images[0].image,images[0].view,images[0].format,false,width,height);depth=PreSrFrame::wrap(images[1].image,images[1].view,images[1].format,false,width,height);motion=PreSrFrame::wrap(images[2].image,images[2].view,images[2].format,false,width,height);output=PreSrFrame::wrap(images[3].image,images[3].view,images[3].format,true,width,height);initial_release();release_serial=serial;nr->serial=serial;auto j=W::parse(metadata());j.update(envelope("metadata","READY",true));j.update(Json{{"initial_release_GPU_serial",serial},{"width",width},{"height",height},{"render_width",width},{"render_height",height},{"model_width",width},{"model_height",height},{"max_jobs",NativeRuntimeModel::JobLimit()},{"initial_history_version",1u},{"manual_white_point",1.0},{"passthrough",false}});W::bootstrap(j,common(),"async-meta-g0.json");require(serial>creation_serial,"real initial release after model creation",3);write_root("async-meta-g0.json",j);
  auto a=wait_root(name_root("imported"));W::bootstrap(a,common(),"async-imported-SESSION-g0.json");Json fd=Json::array();for(auto&i:images)fd.push_back({{"role",i.role},{"fd",unsigned(i.fd)}});require(W::s(a,"op")=="imported"&&W::s(a,"status")=="IMPORTED"&&!W::b(a,"completed_GPU")&&!W::b(a,"output_valid")&&!W::b(a,"model_valid")&&W::u(a,"initial_release_GPU_serial_echo")==release_serial&&a["imported_roles"]==Json::array({"color","depth","mv","output"})&&a["raw_fd_pairs"]==fd&&W::u(a,"native_imported_count")==4&&W::u(a,"owned_duplicate_fd_count_after_import")==0,"host actual import ACK false and exactFD identities",3);retire_fds();auto f=envelope("fd_retired","FD_RETIRED",true);f.update(Json{{"initial_release_GPU_serial",release_serial},{"closed_fd",Json::parse(closed_fds)},{"export_raw_fd_owned",0u}});write_root(name_root("fd-retired"),f);
 }
 bool control(){auto t=try_text(dir+"\\"+name_root("control-request"));if(!t)return false;auto j=W::parse(*t);W::terminal(j,common(),counts,"request_stage");auto epoch=W::u(j,"control_epoch");if(epoch==control_epoch){require(j==stop_request,"conflicting replay stop",3);return true;}require(!active&&!in_flight&&nr->safe&&epoch==control_epoch+1&&!stopping,"strict quiescent next stop epoch",3);require(W::s(j,"op")=="control"&&W::s(j,"status")=="REQUESTED"&&W::s(j,"action")=="stop"&&!W::b(j,"completed_GPU")&&!W::b(j,"output_valid")&&!W::b(j,"model_valid"),"host stop request false",3);terminal_reason=W::s(j,"reason");require(std::set<std::string>{"enable_off","exit","profile_change","budget"}.count(terminal_reason),"terminal reason",3);stopping=true;control_epoch=epoch;stop_request=j;auto a=j;a["op"]="control_ack";a["status"]="STOP_READY";a["completed_GPU"]=true;a["helper_gpu_serial"]=serial;W::terminal(a,common(),counts,"ack_stage");write_root(name_root("control-ack"),a);return true;}
 void model_control(){
  if(auto text=try_text(dir+"\\"+NativeRuntimeModel::RequestName(model_generation+1))){
   auto request=W::parse(*text);auto commit=NativeRuntimeModel::Parse(request,session);
   require(!active&&!stopping&&!in_flight&&nr&&nr->safe&&feature,"runtime feature rebuild actual idle GPU fence",3);
   require(commit.previous_generation==model_generation&&commit.previous_params==model_params&&commit.revision>model_revision&&commit.finalized_jobs==counts.finalized,"runtime model exact predecessor/finalized count",3);
   rotate_daily_logs();
   require(serial<NativeRuntimeModel::CounterLimit&&feature_create_calls<NativeRuntimeModel::CounterLimit&&feature_release_calls<NativeRuntimeModel::CounterLimit,"runtime model counters exhausted before release/create",3);
   get<void(*)(void*)>(fw,"dlssnr_vk_release")(feature);feature=nullptr;++feature_release_calls;
   begin();using Create=void*(*)(void*,void*,unsigned,unsigned,int,float,int,float,float,float,int,int);
   require(feature_create_calls<NativeRuntimeModel::CounterLimit,"feature count exhausted",3);++feature_create_calls;feature=get<Create>(fw,"dlssnr_vk_create")(cmd,params,width,height,int(runtime_resources.preset),commit.settings.intensity,int(commit.settings.style),1,1,1,1,0);
   auto*last=reinterpret_cast<int*>(GetProcAddress(fw,"dlssnr_vk_last_create"));
   submit();require(feature&&last&&*last==1,"runtime feature creation real GPU completion",3);nr->serial=serial;creation_serial=serial;
   verify_preset();
   model_settings=commit.settings;model_generation=commit.generation;model_params=commit.params;model_revision=commit.revision;
   write_root(NativeRuntimeModel::AckName(model_generation),NativeRuntimeModel::Ack(request,serial,feature_create_calls,feature_release_calls));
   printf("RUNTIME_MODEL_ACK generation=%llu params=%llu revision=%llu style=%u intensity=%.9g actual_create_fence=%llu\n",(unsigned long long)model_generation,(unsigned long long)model_params,(unsigned long long)model_revision,model_settings.style,model_settings.intensity,(unsigned long long)serial);
  }
 }
 std::optional<Json>next_input(){for(;;){service_check();if(control())return {};model_control();if(counts.finalized<NativeRuntimeModel::JobLimit())if(auto t=try_text(W::job_path(dir,"input",session,counts.finalized)))return W::parse(*t);Sleep(1);}}
 void job(const Json&input){rotate_daily_logs();W::input(input,common(),counts.finalized);auto model_text=try_text(dir+"\\"+NativeRuntimeModel::JobName(counts.finalized));require(model_text.has_value(),"runtime job model sidecar before input",3);auto model_job=W::parse(*model_text);NativeRuntimeModel::CheckJob(model_job,session,W::job_fields(input),model_generation,model_params,model_settings);gpu->model_style=model_settings.style;gpu->model_intensity=model_settings.intensity;require(!active&&!stopping&&!in_flight&&nr->safe,"one origin slot; prior consumed and actual fence",3);if(counts.finalized)require(W::u(input,"origin_source")>W::u(counts.last,"origin_source"),"strict sparse origin monotonic",3);current_input=input;active=true;job_start=c4_cpu_now_ms();double record_start=job_start;begin();gpu->start();for(auto&i:images)barrier(i,VK_IMAGE_LAYOUT_GENERAL,VK_QUEUE_FAMILY_EXTERNAL,family,0,VK_ACCESS_SHADER_READ_BIT|VK_ACCESS_SHADER_WRITE_BIT|VK_ACCESS_TRANSFER_WRITE_BIT,VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT|VK_PIPELINE_STAGE_TRANSFER_BIT);
  auto eval=get<PreSrFrame::Evaluate>(fw,"dlssnr_vk_evaluate");gpu->record(color,depth,motion,output,negative,feature,params,eval);job_check("production_recorded");for(auto&i:images)barrier(i,VK_IMAGE_LAYOUT_GENERAL,family,VK_QUEUE_FAMILY_EXTERNAL,VK_ACCESS_SHADER_READ_BIT|VK_ACCESS_SHADER_WRITE_BIT|VK_ACCESS_TRANSFER_WRITE_BIT,0,VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT|VK_PIPELINE_STAGE_TRANSFER_BIT,VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT);gpu->finish();nr->submit();serial=nr->serial;gpu->command_bound=c4_cpu_now_ms()-record_start;gpu->complete();job_check("actual_current_fence");require(negative?(gpu->evaluate_calls==0&&gpu->resolve_calls==0):(gpu->evaluate_calls==1&&gpu->evaluate_result==1&&gpu->resolve_calls==1),"actual sparse Evaluate/resolve/result gate",2);evaluates+=gpu->evaluate_calls;resolves+=gpu->resolve_calls;
  auto e=common();e.update(W::job_fields(input));e.update(Json{{"op","complete"},{"status","ORIGIN_NR_COMPLETE"},{"native_input_tick_echo",input["native_input_tick"]},{"helper_gpu_serial",serial},{"helper_GPU_complete",true},{"output_valid",!negative},{"model_valid",!negative},{"evaluate_calls",gpu->evaluate_calls},{"evaluate_result",gpu->evaluate_result},{"resolve_calls",gpu->resolve_calls},{"model_reset",1u},{"evaluate_parameters",negative?Json::object():gpu->evaluate_parameters()},{"gpu_times",gpu->gpu_times()}});job_check("completion_before_atomic_publish");write_root(NativeRuntimeModel::JobName(counts.finalized,true),model_job);atomic_text(W::job_path(dir,"complete",session,counts.finalized),e.dump()+"\n");
  const double start=c4_cpu_now_ms();Json consumed;for(;;){service_check();require(c4_cpu_now_ms()-start<60000,"origin consumed60s; HOLD",3);if(auto t=try_text(W::job_path(dir,"consumed",session,counts.finalized))){consumed=W::parse(*t);break;}Sleep(1);}auto k=W::keys(common());for(auto f:{"job","origin_source","origin_epoch","origin_history","op","status","helper_gpu_serial_echo","native_shared_last_use_tick","native_shared_GPU_complete","disposition"})k.insert(f);W::exact(consumed,k);W::common_check(consumed,common());require(W::job_fields(consumed)==W::job_fields(input)&&W::s(consumed,"op")=="consumed"&&W::s(consumed,"status")=="ORIGIN_RESOURCES_RELEASED"&&W::u(consumed,"helper_gpu_serial_echo")==serial&&W::u(consumed,"native_shared_last_use_tick")>0&&W::b(consumed,"native_shared_GPU_complete"),"exact consumed actual native shared last use",3);auto disposition=W::s(consumed,"disposition");require(negative?disposition=="expected_skip":(disposition=="captured"||disposition=="obsolete_discard"),"mode-specific safe disposition",3);counts.finalize(input,disposition);
  if(NativeRuntimeModel::Daily()){
   // Only after exact CONSUMED and actual Native final shared use. At least
   // two ring slots are needed: renderer FinalizedAck is not a Helper ACK.
   W::cleanup_consumed(dir,session,W::u(input,"job"),[](const std::string& path){return DeleteFileA(path.c_str())!=0;});
  }
  active=false;printf("ORIGIN_FINALIZED job=%llu origin=%llu Eval=%u resolve=%u disposition=%s Helper_GPU_serial=%llu\n",(unsigned long long)W::u(input,"job"),(unsigned long long)W::u(input,"origin_source"),gpu->evaluate_calls,gpu->resolve_calls,disposition.c_str(),(unsigned long long)serial);
 }
 void retire_shared(){require(stopping&&!active&&!in_flight&&nr->safe,"stop actual quiescence includingN0",3);auto j=wait_root(name_root("native-released"));W::terminal(j,common(),counts,"native_release_stage");require(W::s(j,"op")=="release"&&W::s(j,"status")=="NATIVE_RELEASED"&&W::u(j,"control_epoch")==control_epoch&&W::s(j,"terminal_reason")==terminal_reason&&!W::b(j,"output_valid")&&!W::b(j,"model_valid"),"native terminal identity",3);W::native_last_use(j);for(auto k:{"native_imported_images_owned","native_raw_duplicate_fd_owned","native_origin_leases_owned","native_cache_owners_owned"})require(W::u(j,k)==0,"actual native retirement owner counts",3);last_release=j;free_shared();auto e=terminal("retired","RETIRED",true);e.update(Json{{"helper_gpu_serial",serial},{"shared_images_owned",0u},{"export_raw_fd_owned",0u},{"normal_shared_retirement",true}});W::terminal(e,common(),counts,"helper_retired_stage");write_root(name_root("retired"),e);
  auto a=wait_root(name_root("native-audited"));W::terminal(a,common(),counts,"native_audited_stage");W::native_last_use(a);require(W::s(a,"op")=="audit"&&W::s(a,"status")=="FD_AUDITED"&&W::u(a,"control_epoch")==control_epoch&&W::s(a,"terminal_reason")==terminal_reason&&!W::b(a,"output_valid")&&!W::b(a,"model_valid")&&W::u(a,"tracked_NR_FD_remaining")==0&&W::s(a,"native_inventory")=="async-native-fd-final.json"&&W::s(a,"helper_inventory")=="async-wine-fd-final.json","targeted real final FD audit",3);for(auto k:{"native_last_use_tick","native_last_use_proof","completed_GPU"})require(a.at(k)==last_release.at(k),"audit repeats actual release proof",3);
 }
 void init_nr_device(){
  loader=LoadLibraryA("vulkan-1.dll");require(loader,"vulkan-1.dll",3);gipa=reinterpret_cast<PFN_vkGetInstanceProcAddr>(GetProcAddress(loader,"vkGetInstanceProcAddr"));require(gipa,"gipa",3);
  // Match the original successful NR fixture explicit KHR properties2 instance condition.
  uint32_t instance_count=0;api(I(vkEnumerateInstanceExtensionProperties)(nullptr,&instance_count,nullptr),"InstanceExtCount");
  std::vector<VkExtensionProperties> instance_exts(instance_count);api(I(vkEnumerateInstanceExtensionProperties)(nullptr,&instance_count,instance_exts.data()),"InstanceExts");
  const char*instance_ext=VK_KHR_GET_PHYSICAL_DEVICE_PROPERTIES_2_EXTENSION_NAME;bool properties2_advertised=false;
  for(auto&e:instance_exts)if(!strcmp(e.extensionName,instance_ext))properties2_advertised=true;
  printf("INSTANCE_EXTENSION name=%s advertised=%u enabled=%u original_success_fixture_alignment=1\n",instance_ext,properties2_advertised,properties2_advertised);
  require(properties2_advertised,"KHR properties2 instance extension not advertised",3);
  VkApplicationInfo app{VK_STRUCTURE_TYPE_APPLICATION_INFO};app.apiVersion=VK_API_VERSION_1_2;app.pApplicationName="Pre-SR original render HDR720 NR720 -> Native DLSS SR1080";VkInstanceCreateInfo ci{VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO};ci.pApplicationInfo=&app;ci.enabledExtensionCount=1;ci.ppEnabledExtensionNames=&instance_ext;api(I(vkCreateInstance)(&ci,nullptr,&instance),"CreateInstance");
  auto core_properties=gipa(instance,"vkGetPhysicalDeviceProperties2"),khr_properties=gipa(instance,"vkGetPhysicalDeviceProperties2KHR");
  printf("PROPERTIES2_ENTRYPOINT core=%p khr=%p both_present=%u\n",reinterpret_cast<void*>(core_properties),reinterpret_cast<void*>(khr_properties),core_properties&&khr_properties);
  require(core_properties&&khr_properties,"properties2 core/KHR dispatch after explicit extension",3);
  uint32_t n=0;api(I(vkEnumeratePhysicalDevices)(instance,&n,nullptr),"PhysicalCount");std::vector<VkPhysicalDevice> ps(n);api(I(vkEnumeratePhysicalDevices)(instance,&n,ps.data()),"PhysicalDevices");
  for(auto p:ps){VkPhysicalDeviceProperties2 pr{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2};pr.pNext=&ids;I(vkGetPhysicalDeviceProperties2)(p,&pr);if(pr.properties.vendorID==0x10de){physical=p;properties=pr.properties;break;}}
  require(physical,"NVIDIA absent",3);I(vkGetPhysicalDeviceMemoryProperties)(physical,&memory);printf("DEVICE name=%s device_uuid=%s driver_uuid=%s driver=%u\n",properties.deviceName,hex(ids.deviceUUID).c_str(),hex(ids.driverUUID).c_str(),properties.driverVersion);
  require(hex(ids.deviceUUID)==expected_device&&hex(ids.driverUUID)==expected_driver,"C4 actual game/helper UUID mismatch",3);
  char luid_hex[17]{};for(unsigned i=0;i<8;i++)snprintf(luid_hex+2*i,3,"%02x",ids.deviceLUID[i]);
  printf("DEVICE_ID_LUID valid=%u luid=%s node_mask=%u host_NGX_requires_validity_unverified=1\n",ids.deviceLUIDValid,luid_hex,ids.deviceNodeMask);
  api(I(vkEnumerateDeviceExtensionProperties)(physical,nullptr,&n,nullptr),"ExtCount");std::vector<VkExtensionProperties> es(n);api(I(vkEnumerateDeviceExtensionProperties)(physical,nullptr,&n,es.data()),"Exts");std::vector<const char*> enable;
  for(const char*s:{VK_KHR_EXTERNAL_MEMORY_FD_EXTENSION_NAME,VK_EXT_EXTERNAL_MEMORY_DMA_BUF_EXTENSION_NAME,"VK_NVX_binary_import","VK_NVX_image_view_handle",VK_KHR_PUSH_DESCRIPTOR_EXTENSION_NAME,VK_KHR_BUFFER_DEVICE_ADDRESS_EXTENSION_NAME}){bool found=false;for(auto&e:es)if(!strcmp(e.extensionName,s))found=true;printf("EXTENSION name=%s advertised=%d enabled=%d\n",s,found,found);require(found,s,3);enable.push_back(s);}
  I(vkGetPhysicalDeviceQueueFamilyProperties)(physical,&n,nullptr);std::vector<VkQueueFamilyProperties> qs(n);I(vkGetPhysicalDeviceQueueFamilyProperties)(physical,&n,qs.data());bool found=false;for(uint32_t i=0;i<n;i++)if(qs[i].queueCount&&(qs[i].queueFlags&VK_QUEUE_COMPUTE_BIT)){family=i;timestamp_bits=qs[i].timestampValidBits;found=true;break;}require(found,"compute queue",3);
  VkPhysicalDeviceBufferDeviceAddressFeatures bda{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_BUFFER_DEVICE_ADDRESS_FEATURES};VkPhysicalDeviceFeatures2 ft{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2};ft.pNext=&bda;I(vkGetPhysicalDeviceFeatures2)(physical,&ft);printf("BDA_FEATURE available=%u\n",bda.bufferDeviceAddress);require(bda.bufferDeviceAddress,"NR bufferDeviceAddress",3);bda.bufferDeviceAddressCaptureReplay=bda.bufferDeviceAddressMultiDevice=VK_FALSE;
  float priority=.5f;VkDeviceQueueCreateInfo qi{VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO};qi.queueFamilyIndex=family;qi.queueCount=1;qi.pQueuePriorities=&priority;VkDeviceCreateInfo dc{VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO};dc.pNext=&bda;dc.queueCreateInfoCount=1;dc.pQueueCreateInfos=&qi;dc.enabledExtensionCount=uint32_t(enable.size());dc.ppEnabledExtensionNames=enable.data();api(I(vkCreateDevice)(physical,&dc,nullptr,&device),"CreateDevice");gdpa=I(vkGetDeviceProcAddr);D(vkGetDeviceQueue)(device,family,0,&queue);
  VkCommandPoolCreateInfo cp{VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO};cp.flags=VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;cp.queueFamilyIndex=family;api(D(vkCreateCommandPool)(device,&cp,nullptr,&pool),"CreatePool");VkCommandBufferAllocateInfo ca{VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO};ca.commandPool=pool;ca.level=VK_COMMAND_BUFFER_LEVEL_PRIMARY;ca.commandBufferCount=1;api(D(vkAllocateCommandBuffers)(device,&ca,&cmd),"AllocateCommand");VkFenceCreateInfo fc{VK_STRUCTURE_TYPE_FENCE_CREATE_INFO};api(D(vkCreateFence)(device,&fc,nullptr,&fence),"CreateFence");
 }

 // The forwarder uses the NVIDIA/MSVC unsigned setter slot 3; its getter is 11.
 // Check parameter readback only: NGX does not expose which internal weights it chose.
 void verify_preset(){
  alignas(16) unsigned char storage[128];std::memset(storage,0xA5,sizeof(storage));
  using GetUInt=int(__thiscall*)(void*,const char*,void*);
  void**vt=*reinterpret_cast<void***>(params);
  const int result=reinterpret_cast<GetUInt>(vt[11])(params,"DLSSNR.Hint.Render.Preset",storage+32);
  unsigned value=0;std::memcpy(&value,storage+32,sizeof(value));bool safe=true;
  for(unsigned i=0;i<128;++i)if(i<32||i>=36)safe&=storage[i]==0xA5;
  printf("MODEL_PRESET requested=%u readback=%u result=0x%08x canary=%d feature_created=1 GPU_complete=1 internal_weights=NOT_EXPOSED\n",runtime_resources.preset,value,result,int(safe));
  require(safe&&result==1&&value==runtime_resources.preset,"NR preset parameter readback after creation fence",3);
 }
 void init_model(){
  // Actual NVAPI trace requires DXVK's Vulkan-aware DXGI factory; no D3D DLL replacement.
  SetLastError(ERROR_SUCCESS);
  dxgi=LoadLibraryExW(wide(dxgi_path).c_str(),nullptr,LOAD_WITH_ALTERED_SEARCH_PATH);
  const DWORD dxgi_load_error=GetLastError();
  printf("DXGI_LOAD requested=%s flags=LOAD_WITH_ALTERED_SEARCH_PATH handle=%p winerror=%lu before_NVAPI_core=1 DLL_copy=0\n",dxgi_path.c_str(),dxgi,(unsigned long)dxgi_load_error);
  require(dxgi,"pinned installed DXVK DXGI PE load",3);
  wchar_t dxgi_actual_wide[32768]{};SetLastError(ERROR_SUCCESS);
  const DWORD dxgi_actual_length=GetModuleFileNameW(dxgi,dxgi_actual_wide,32768),dxgi_path_error=GetLastError();
  require(dxgi_actual_length>0&&dxgi_actual_length<32768,"DXGI actual module path",3);
  const std::string dxgi_actual(dxgi_actual_wide,dxgi_actual_wide+dxgi_actual_length);
  const bool dxgi_path_matches=_stricmp(dxgi_actual.c_str(),dxgi_path.c_str())==0;
  printf("DXGI_MODULE actual=%s requested_path_match=%u winerror=%lu held_until_after_core_and_NVAPI_release=1\n",dxgi_actual.c_str(),dxgi_path_matches,(unsigned long)dxgi_path_error);
  require(dxgi_path_matches,"DXGI module differs from pinned absolute path",3);
  // Single availability adaptation: installed pinned PE only; no NVAPI function calls.
  SetLastError(ERROR_SUCCESS);
  nvapi=LoadLibraryExW(wide(nvapi_path).c_str(),nullptr,LOAD_WITH_ALTERED_SEARCH_PATH);
  const DWORD load_error=GetLastError();
  printf("NVAPI_LOAD requested=%s flags=LOAD_WITH_ALTERED_SEARCH_PATH handle=%p winerror=%lu private_NVAPI_calls=0 DLL_copy=0\n",nvapi_path.c_str(),nvapi,(unsigned long)load_error);
  require(nvapi,"pinned installed NVAPI PE load",3);
  wchar_t actual_wide[32768]{};SetLastError(ERROR_SUCCESS);
  const DWORD actual_length=GetModuleFileNameW(nvapi,actual_wide,32768),path_error=GetLastError();
  require(actual_length>0&&actual_length<32768,"NVAPI actual module path",3);
  const std::string actual(actual_wide,actual_wide+actual_length);
  const bool path_matches=_stricmp(actual.c_str(),nvapi_path.c_str())==0;
  printf("NVAPI_MODULE actual=%s requested_path_match=%u winerror=%lu\n",actual.c_str(),path_matches,(unsigned long)path_error);
  require(path_matches,"NVAPI module differs from pinned absolute path",3);
  SetLastError(ERROR_SUCCESS);const auto query_interface=GetProcAddress(nvapi,"nvapi_QueryInterface");const DWORD symbol_error=GetLastError();
  printf("NVAPI_EXPORT nvapi_QueryInterface=%p winerror=%lu called=0 held_until_after_core_shutdown=1\n",reinterpret_cast<void*>(query_interface),(unsigned long)symbol_error);
  require(query_interface,"installed NVAPI public export unavailable",3);
  fw=LoadLibraryW(wide(forwarder_path).c_str());core=LoadLibraryW(wide(core_path).c_str());require(fw&&core,"pinned PE core/forwarder load",3);
  printf("RUNTIME model=%s core=%s forwarder=%s\n",model_path.c_str(),module(core).c_str(),module(fw).c_str());
  int mask=get<int(*)(const wchar_t*)>(fw,"dlssnr_vk_probe")(wide(model_path).c_str());printf("MODEL_VK_EXPORT_MASK %d expected=15\n",mask);require(mask==15,"NR model real exports",3);
  phase_tick("PE_runtime_loaded");
  auto ir=get<decltype(&NVSDK_NGX_VULKAN_Init_Ext)>(core,"NVSDK_NGX_VULKAN_Init_Ext")(0x24480451ull,wide(dir).c_str(),instance,physical,device,NVSDK_NGX_Version_API,nullptr);
  phase_tick("core_init_returned");
  printf("CORE_VK_INIT result=0x%08x\n",(unsigned)ir);require(ir==NVSDK_NGX_Result_Success,"core init");
  auto caps=get<decltype(&NVSDK_NGX_VULKAN_GetCapabilityParameters)>(core,"NVSDK_NGX_VULKAN_GetCapabilityParameters")(&params);
  printf("VK_CAPABILITY result=0x%08x ptr=%p\n",(unsigned)caps,params);require(caps==NVSDK_NGX_Result_Success&&params,"core caps");
  require(nr_discover_float(params,get<void(*)(void*,const char*,float,int)>(fw,"dlssnr_call_probe_float"),get<void(*)(int)>(fw,"dlssnr_call_set_float_slot")),"safe parameter float ABI");
  phase_tick("caps_safe_parameter_abi");
  int mr=get<int(*)(const wchar_t*,const wchar_t*,void*,void*,void*,int)>(fw,"dlssnr_vk_init")(wide(model_path).c_str(),wide(dir).c_str(),instance,physical,device,0x15);
  printf("MODEL_VK_INIT result=0x%08x\n",mr);require(mr==1,"model init");
  phase_tick("model_init");
  begin();using Create=void*(*)(void*,void*,unsigned,unsigned,int,float,int,float,float,float,int,int);
  require(feature_create_calls<NativeRuntimeModel::CounterLimit,"feature count exhausted",3);++feature_create_calls;feature=get<Create>(fw,"dlssnr_vk_create")(cmd,params,width,height,int(runtime_resources.preset),model_settings.intensity,int(model_settings.style),1,1,1,1,0);
  auto*last=reinterpret_cast<int*>(GetProcAddress(fw,"dlssnr_vk_last_create"));printf("MODEL_VK_CREATE handle=%p result=0x%08x\n",feature,last?*last:0);
  submit();require(feature&&last&&*last==1,"model create result after real GPU completion");
  verify_preset();
  model_initialized=true;feature_created_ever=true;creation_serial=serial;
  phase_tick("feature_create_gpu");
  nr=std::make_unique<N>(instance,physical,device,cmd,queue,gipa);nr->gpu_timeout_ms=gpu_timeout_ms;nr->shader();
  query(VK_FORMAT_R16G16B16A16_SFLOAT,"private_raw97_ACTUAL_MODEL_TARGET",false,width,height);
  nr->serial=serial;gpu=std::make_unique<PreSrFrame>(*nr,timestamp_bits,properties.limits.timestampPeriod,width,height);
  phase_tick("persistent_production_and_optional_diagnostic_ready");
 }
 void shutdown(){
  require(!in_flight&&images.empty()&&nr&&nr->safe,"normal model shutdown requires GPU quiescence and release/audit");
  get<void(*)(void*)>(fw,"dlssnr_vk_release")(feature);feature=nullptr;require(feature_release_calls<NativeRuntimeModel::CounterLimit,"feature count exhausted",3);++feature_release_calls;puts("TEARDOWN feature_release completed");
  gpu.reset();
  nr.reset();puts("TEARDOWN private_production_fixture released");
  int sr=get<int(*)(const wchar_t*,void*)>(fw,"nr_poc_vk_shutdown")(wide(model_path).c_str(),device);printf("TEARDOWN model_Shutdown1 result=0x%08x\n",sr);require(sr==1,"model Shutdown1");
  auto dr=get<decltype(&NVSDK_NGX_VULKAN_DestroyParameters)>(core,"NVSDK_NGX_VULKAN_DestroyParameters")(params);printf("TEARDOWN core_DestroyParameters result=0x%08x\n",(unsigned)dr);require(dr==NVSDK_NGX_Result_Success,"DestroyParameters");params=nullptr;
  using CoreShutdown=NVSDK_NGX_Result(NVSDK_CONV*)(void);auto cs=get<CoreShutdown>(core,"NVSDK_NGX_VULKAN_Shutdown")();printf("TEARDOWN core_Shutdown result=0x%08x\n",(unsigned)cs);require(cs==NVSDK_NGX_Result_Success,"public core Shutdown");
  require(FreeLibrary(fw)&&FreeLibrary(core),"PE FreeLibrary");fw=core=nullptr;
  SetLastError(ERROR_SUCCESS);const BOOL nvapi_freed=FreeLibrary(nvapi);const DWORD free_error=GetLastError();
  printf("TEARDOWN NVAPI_FreeLibrary result=%u winerror=%lu after_core_shutdown=1\n",unsigned(nvapi_freed),(unsigned long)free_error);
  require(nvapi_freed,"NVAPI FreeLibrary after core shutdown");nvapi=nullptr;
  SetLastError(ERROR_SUCCESS);const BOOL dxgi_freed=FreeLibrary(dxgi);const DWORD dxgi_free_error=GetLastError();
  printf("TEARDOWN DXGI_FreeLibrary result=%u winerror=%lu after_core_shutdown_and_NVAPI_FreeLibrary=1\n",unsigned(dxgi_freed),(unsigned long)dxgi_free_error);
  require(dxgi_freed,"DXGI FreeLibrary after core shutdown and NVAPI release");dxgi=nullptr;
  D(vkDestroyFence)(device,fence,nullptr);D(vkDestroyCommandPool)(device,pool,nullptr);D(vkDestroyDevice)(device,nullptr);device=nullptr;
  I(vkDestroyInstance)(instance,nullptr);instance=nullptr;require(FreeLibrary(loader),"loader FreeLibrary");loader=nullptr;
  normal_shutdown=true;puts("NORMAL_RELEASE NR=1 shared=1 private=1 core=1 device=1 origin_jobs_actual=1 process_exit_not_raw_fd_strategy=1");
 }
};
int main(int argc,char**argv){if(argc!=6)return 2;if(argc==6&&std::string(argv[4])!="--runtime-config")return 2;PreSrHelper c;c.mode=argv[1];c.dir=argv[2];c.session=argv[3];c.negative=c.mode=="skipEvaluate";if(!freopen((c.dir+"\\async-helper.log").c_str(),"wb",stdout)||!freopen((c.dir+"\\async-helper-stderr.log").c_str(),"wb",stderr))return 4;setvbuf(stdout,nullptr,_IONBF,0);
 try{if(argc==6){auto config_text=PreSrHelper::try_text(argv[5]);require(config_text.has_value(),"runtime config before device or DLL loading",3);select_runtime(W::parse(*config_text));printf("RUNTIME_INPUTS schema=%s model=%s forwarder=%s ngx_dir=%s ge_files=%s\n",W::s(W::parse(*config_text),"schema").c_str(),runtime_resources.model.c_str(),runtime_resources.forwarder.c_str(),runtime_resources.ngx_dir.c_str(),runtime_resources.ge_files.c_str());}require(PreSrHelper::safe_token(c.session),"CLI safe session",3);c.config();c.environment();auto p=c.envelope("pid_request","PID_REQUEST",false);p["exe"]=module(GetModuleHandleA(nullptr));p["win_pid"]=GetCurrentProcessId();W::bootstrap(p,c.common(),"async-pid-request.json");c.write_root("async-pid-request.json",p);
  double start=c4_cpu_now_ms();std::optional<std::string>text;while(c4_cpu_now_ms()-start<60000){c.service_check();text=PreSrHelper::try_text(c.dir+"\\async-exporter.pid");if(text)break;Sleep(5);}require(text.has_value(),"verified exporter PID missing",3);require(!text->empty()&&text->back()=='\n',"actual PID newline",3);text->pop_back();require(!text->empty()&&std::all_of(text->begin(),text->end(),[](char x){return x>='0'&&x<='9';}),"actual PID numeric",3);auto pid=std::stoull(*text);require(pid>1&&pid<=INT32_MAX,"actual Linux exporter PID",3);c.linux_pid=uint32_t(pid);c.peer_identity();c.cold_start=c4_cpu_now_ms();c.init_nr_device();c.phase_tick("device_ready");c.init_model();c.baseline();c.exports();while(auto input=c.next_input())c.job(*input);c.retire_shared();c.shutdown();auto j=c.terminal("result","PASS",true);j.update(Json{{"normal_release",true},{"normal_shutdown",c.normal_shutdown},{"feature_create_calls",c.feature_create_calls},{"feature_release_calls",c.feature_release_calls},{"evaluate_calls_total",c.evaluates},{"resolve_calls_total",c.resolves},{"NR_phase","UNVERIFIED"},{"native_present_count","REQUIRES_NATIVE_LEDGER_NOT_INFERRED"}});W::terminal(j,c.common(),c.counts,"helper_result_stage");require(c.feature_create_calls==c.model_generation+1&&c.feature_release_calls==c.model_generation+1&&c.evaluates==(c.negative?0:c.counts.finalized)&&c.resolves==c.evaluates,"actual complete API counters",2);c.write_root("async-wine-result.json",j);return 0;
 }catch(const std::exception&e){auto*stop=dynamic_cast<const Stop*>(&e);int code=stop?stop->code:2;fprintf(stderr,"ASYNC_%s reason=%s active=%u GPU_unknown=%u normal_release=0 HOLD=1\n",code==3?"BLOCKED":"FAIL",e.what(),c.active,c.in_flight||(c.nr&&!c.nr->safe));try{auto j=c.envelope("result",code==3?"BLOCKED":"FAIL",false);j.update(c.counts.fields());j.update(Json{{"reason",e.what()},{"exit_code",code},{"normal_release",false},{"normal_shutdown",false},{"HOLD",true},{"active_job",c.active},{"GPU_unknown",c.in_flight||(c.nr&&!c.nr->safe)}});c.write_root("async-wine-result.json",j);}catch(...){}c.gpu.release();c.nr.release();return code;}
}
