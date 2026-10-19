// CPU-only resource path/argv wiring. No Vulkan/Wine dependency.
#include "gpu/shadps4/video_core/renderer_vulkan/native_dev_paths.h"
#include "gpu/shadps4/video_core/renderer_vulkan/native_origin_identity_policy.h"
#include <cassert>
#include <fstream>
#include <iostream>
int main(int argc,char**argv) {
 assert(argc==2);
 namespace P=BbNativeAsyncPreSr;
 const std::string run=argv[1],config=run+"/helper-runtime.json";
 std::filesystem::create_directories(run+"/ge");
 std::filesystem::create_directories(run+"/ngx");
 std::ofstream(config)<<"{}\n";
 assert(P::ValidHelperRuntimePaths(run,"","",""));
 assert(!P::ValidHelperRuntimePaths(run,"",run+"/ge",""));
 assert(P::ValidHelperRuntimePaths(run,config,run+"/ge",run+"/ngx"));
 assert(!P::ValidHelperRuntimePaths(run,config,run+"/missing",run+"/ngx"));
 assert(!P::ValidHelperRuntimePaths(run,run+"/other.json",run+"/ge",run+"/ngx"));
 std::filesystem::create_directory_symlink(run+"/ge",run+"/linked-ge");
 assert(!P::ValidHelperRuntimePaths(run,config,run+"/linked-ge",run+"/ngx"));
 const std::vector<std::string> legacy{"/preloader","/wine","Z:/helper.exe","resident","Z:"+run,"session"};
 assert(P::HelperRuntimeArguments("/preloader","/wine","/helper.exe","resident",run,"session","")==legacy);
 auto explicit_args=legacy;explicit_args.push_back("--runtime-config");explicit_args.push_back("Z:"+config);
 assert(P::HelperRuntimeArguments("/preloader","/wine","/helper.exe","resident",run,"session",config)==explicit_args);
 assert(!P::ValidDevRun(run,"session")); // ROOT/runs confinement remains fixed.
 assert(std::string(P::HelperRuntimeConfigVersion)=="NRBRIDGE_NATIVE_HELPER_RUNTIME_CONFIG_V1");
 namespace I=BbNativeAsync::IdentityPolicy;
 I::Expected expected{"/helper.exe",run,"resident","session","/wine","/preloader"};
 I::Candidate candidate{.pid=44,.game=20,.launcher=44,.start_before=123,.start_after=123,
  .descendant_game=true,.descendant_launcher=true,.exact_pe_maps=true,
  .elf="/preloader",.argv={expected.helper,expected.mode,expected.run,expected.session}};
 assert(I::Check(candidate,expected).Accepted());
 auto configured=expected;configured.runtime_config=config;
 assert(!I::Check(candidate,configured).Accepted()); // New config requires all six PE argv.
 candidate.argv.push_back("--runtime-config");candidate.argv.push_back(config);
 assert(I::Check(candidate,configured).Accepted());
 assert(!I::Check(candidate,expected).Accepted()); // Legacy never accepts extra argv.
 candidate.argv[5]+="-wrong";assert(!I::Check(candidate,configured).Accepted());
 candidate.argv[5]=config;candidate.argv.push_back("extra");assert(!I::Check(candidate,configured).Accepted());
 candidate.argv.pop_back();candidate.argv[4]="--other";assert(!I::Check(candidate,configured).Accepted());
 candidate.argv[4]="--runtime-config";candidate.exact_pe_maps=false;assert(!I::Check(candidate,configured).Accepted());
 candidate.exact_pe_maps=true;candidate.start_after++;assert(!I::Check(candidate,configured).Accepted());
 std::cout<<"runtime input Native path/argv CPU checks PASS\n";
}
