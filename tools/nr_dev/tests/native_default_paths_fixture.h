// CPU tests only: compile with -Wl,--wrap=getpwuid_r. Production has no override.
#pragma once
#include <cerrno>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <pwd.h>
#include <string>
#include <unistd.h>
namespace NativeDefaultPathsFixture {
inline std::string home;
inline int error=0;
inline bool missing=false,wrong_uid=false,retry=false;
inline unsigned calls=0;
inline void UseHome(const std::filesystem::path& path) {
 home=path.string();error=0;missing=false;wrong_uid=false;retry=false;calls=0;
 if(setenv("HOME",home.c_str(),1))std::abort();
}
}
extern "C" int __wrap_getpwuid_r(uid_t uid,passwd* entry,char* buffer,
                                size_t size,passwd** result) {
 namespace F=NativeDefaultPathsFixture;
 ++F::calls;*result=nullptr;
 if(uid!=getuid())return EINVAL;
 if(F::error)return F::error;
 if(F::missing)return 0;
 if(F::retry&&F::calls==1)return ERANGE;
 if(size<F::home.size()+1)return ERANGE;
 std::memcpy(buffer,F::home.c_str(),F::home.size()+1);
 *entry={};entry->pw_uid=F::wrong_uid?uid+1:uid;entry->pw_dir=buffer;
 *result=entry;
 return 0;
}
