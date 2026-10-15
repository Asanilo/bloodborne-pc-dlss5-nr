// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include <filesystem>
#include <string>
#include <string_view>
namespace BbNativeAsyncPreSr {
// One new-cycle root shared by the frontend, owner and diagnostics. Path
// confinement is independent of permission to arm, GPU proofs and wire identity.
inline bool ValidDevSession(std::string_view session) noexcept {
 if(session.empty()||session.size()>63)return false;
 for(char c:session)if(!((c>='a'&&c<='z')||(c>='A'&&c<='Z')||(c>='0'&&c<='9')||c=='-'))return false;
 return session.front()!='-';
}
inline bool ValidDevRun(const std::string& run,const std::string& session) {
 if(!ValidDevSession(session)||run!="/home/arinp/Code/nrbridge-dev/runs/"+session)return false;
 std::error_code error;
 const auto actual=std::filesystem::canonical(run,error);
 return !error&&actual==std::filesystem::path(run)&&std::filesystem::is_directory(actual,error)&&!error;
}
}
