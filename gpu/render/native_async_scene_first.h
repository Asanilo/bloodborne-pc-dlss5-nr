// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <string_view>
namespace BbNativeAsync::SceneFirst {
inline bool ValidConfiguration(std::string_view flag) noexcept {return flag=="0"||flag=="1";}
inline bool Active() noexcept {
 const char*flag=std::getenv("BB_NR_NATIVE_ASYNC_SCENE_FIRST");
 const char*mode=std::getenv("BB_NR_NATIVE_ASYNC_MODE");
 return flag&&std::strcmp(flag,"1")==0&&mode&&
  (std::strcmp(mode,"resident")==0||std::strcmp(mode,"skipEvaluate")==0);
}
inline bool ExactToken(std::string_view bytes,std::string_view session) noexcept {
 return !session.empty()&&session.size()<=63&&bytes.size()==session.size()+1&&
  bytes.substr(0,session.size())==session&&bytes.back()=='\n';
}
inline bool CanStartPeer(bool supported,bool requested,bool exiting) noexcept {
 return supported&&requested&&!exiting;
}
struct NoPeerExitProof {
 bool no_peer=false,no_retained=false,no_snapshot_owner=false,no_pending=false;
 bool frontend_joined=false,unknown=false,peer_creation_attempted=false,snapshot_creation_attempted=false;
 uint64_t copies_ever=0,maximum_native_use_tick=0;unsigned chain_size=0;
};
inline bool CanExitWithoutPeer(const NoPeerExitProof&p) noexcept {
 return p.no_peer&&p.no_retained&&p.no_snapshot_owner&&p.no_pending&&p.frontend_joined&&
  !p.unknown&&!p.peer_creation_attempted&&!p.snapshot_creation_attempted&&
  !p.copies_ever&&!p.maximum_native_use_tick&&!p.chain_size;
}
} // namespace BbNativeAsync::SceneFirst
