// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include <filesystem>
#include <cstdlib>
#include "native_runtime_model.h"
#include <string>
#include <string_view>
#include <vector>
namespace BbNativeAsyncPreSr {
// One new-cycle root shared by the frontend, owner and diagnostics. Path
// confinement is independent of permission to arm, GPU proofs and wire identity.
inline bool ValidDevSession(std::string_view session) noexcept {
 if(session.empty()||session.size()>63)return false;
 for(char c:session)if(!((c>='a'&&c<='z')||(c>='A'&&c<='Z')||(c>='0'&&c<='9')||c=='-'))return false;
 return session.front()!='-';
}
inline bool ValidDevRun(const std::string& run,const std::string& session) {
 const char* configured=std::getenv("NRBRIDGE_HOME");
 const std::string root=NativeRuntimeModel::Daily()&&configured?configured:"/home/arinp/Code/nrbridge-dev";
 std::error_code root_error;
 if(root.empty()||root.front()!='/'||std::filesystem::canonical(root,root_error)!=std::filesystem::path(root)||root_error)return false;
 if(!ValidDevSession(session)||run!=root+"/runs/"+session)return false;
 std::error_code error;
 const auto actual=std::filesystem::canonical(run,error);
 return !error&&actual==std::filesystem::path(run)&&std::filesystem::is_directory(actual,error)&&!error;
}
// Capability marker is consumed only after the runner verifies the Native SHA.
// It prevents passing a new config to an old binary; it is not a GPU proof.
inline constexpr const char* HelperRuntimeConfigVersion = "NRBRIDGE_NATIVE_HELPER_RUNTIME_CONFIG_V1";
inline bool ValidHelperRuntimePaths(const std::string& run,const std::string& config,
                                  const std::string& ge_files,const std::string& ngx_dir) {
 if(config.empty())return ge_files.empty()&&ngx_dir.empty();
 if(config!=run+"/helper-runtime.json")return false;
 for(const auto& value:{config,ge_files,ngx_dir}) {
  if(value.empty()||value.front()!='/'||value.find_first_of(":\n\r")!=std::string::npos)return false;
  std::error_code error;
  if(std::filesystem::canonical(value,error)!=std::filesystem::path(value)||error)return false;
 }
 std::error_code error;
 return std::filesystem::is_regular_file(config,error)&&!error&&
        std::filesystem::is_directory(ge_files,error)&&!error&&
        std::filesystem::is_directory(ngx_dir,error)&&!error;
}
inline std::vector<std::string> HelperRuntimeArguments(
 const std::string& preloader,const std::string& wine,const std::string& helper,
 const std::string& mode,const std::string& run,const std::string& session,
 const std::string& config) {
 std::vector<std::string> args{preloader,wine,"Z:"+helper,mode,"Z:"+run,session};
 if(!config.empty()){args.push_back("--runtime-config");args.push_back("Z:"+config);}
 return args;
}
}
