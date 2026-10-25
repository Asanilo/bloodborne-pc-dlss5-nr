// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include <cerrno>
#include <cstdlib>
#include <filesystem>
#include <pwd.h>
#include <string>
#include <unistd.h>
#include <vector>

namespace NativeDefaultPaths {
// Defaults belong to the actual UID, never to a caller-selected HOME. These
// paths select no capability and do not change explicit daily runtime inputs.
inline bool CanonicalDirectory(const std::string& value) {
 if(value.empty()||value.front()!='/'||value=="/"||
    value.find_first_of(":\r\n")!=std::string::npos)return false;
 std::error_code error;
 const auto path=std::filesystem::canonical(value,error);
 return !error&&path==std::filesystem::path(value)&&
        std::filesystem::is_directory(path,error)&&!error;
}
inline std::string UserHome() {
 std::vector<char> buffer(4096);
 passwd entry{};passwd* found=nullptr;
 for(;;) {
  const int status=getpwuid_r(getuid(),&entry,buffer.data(),buffer.size(),&found);
  if(status==ERANGE&&buffer.size()<1024*1024){buffer.resize(buffer.size()*2);continue;}
  if(status||!found||entry.pw_uid!=getuid()||!entry.pw_dir)return {};
  const std::string home=entry.pw_dir;
  if(!CanonicalDirectory(home))return {};
  // Python's legacy defaults use HOME. A disagreement must stop the fallback,
  // rather than letting its runner and Native select different private roots.
  const char* environment_home=std::getenv("HOME");
  if(environment_home&&home!=environment_home)return {};
  return home;
 }
}
inline std::string DirectoryBelowHome(const char* suffix) {
 const auto home=UserHome();
 if(home.empty())return {};
 const auto path=home+suffix;
 return CanonicalDirectory(path)?path:std::string{};
}
inline std::string DevRoot() {
 return DirectoryBelowHome("/Code/nr-home");
}
inline std::string LegacyPocRoot() {
 return DirectoryBelowHome("/Code/nrbridge-design/game-poc-20261011/bbport");
}
inline std::string LegacyGeFiles() {
 return DirectoryBelowHome("/.local/share/Steam/compatibilitytools.d/GE-Proton11-7-x86_64/files");
}
}
