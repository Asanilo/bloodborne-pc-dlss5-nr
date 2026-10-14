// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include <cstdlib>
#include <cstring>
namespace BbNativeAsyncPreSrSrPolicy {
enum class History {DomainReset,GameOnly,Invalid};
enum class Input {Switching,Fixed,Invalid};
inline History ParseHistory(const char* s)noexcept{
 if(!s||!std::strcmp(s,"domain_reset"))return History::DomainReset;
 if(!std::strcmp(s,"game_only"))return History::GameOnly;
 return History::Invalid;
}
inline Input ParseInput(const char* s)noexcept{
 if(!s||!std::strcmp(s,"switching"))return Input::Switching;
 if(!std::strcmp(s,"fixed"))return Input::Fixed;
 return Input::Invalid;
}
inline History HistoryMode()noexcept{return ParseHistory(std::getenv("BB_NR_NATIVE_ASYNC_PRE_SR_SR_HISTORY"));}
inline Input InputMode()noexcept{return ParseInput(std::getenv("BB_NR_NATIVE_ASYNC_PRE_SR_SR_INPUT"));}
inline const char* HistoryName()noexcept{return HistoryMode()==History::GameOnly?"game_only":HistoryMode()==History::DomainReset?"domain_reset":"INVALID";}
inline const char* InputName()noexcept{return InputMode()==Input::Fixed?"fixed":InputMode()==Input::Switching?"switching":"INVALID";}
inline bool ExtraDomainReset(bool changed)noexcept{return changed&&HistoryMode()!=History::GameOnly;}
// Original game/resource/cut/history request and actual managed-resource entry/exit
// remain protected in BOTH experiments. Only cache-domain extra reset is optional.
inline bool AppliedReset(bool original,bool changed,bool fixed_resource_transition)noexcept{
 return original||fixed_resource_transition||ExtraDomainReset(changed);
}
inline bool ExplicitModes()noexcept{
 return std::getenv("BB_NR_NATIVE_ASYNC_PRE_SR_SR_HISTORY")&&std::getenv("BB_NR_NATIVE_ASYNC_PRE_SR_SR_INPUT")&&
 HistoryMode()!=History::Invalid&&InputMode()!=Input::Invalid;
}
}
