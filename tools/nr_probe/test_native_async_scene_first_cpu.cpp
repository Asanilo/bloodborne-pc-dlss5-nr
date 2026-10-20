// SPDX-License-Identifier: GPL-2.0-or-later
#include "gpu/render/native_async_scene_first.h"
#include "gpu/render/native_async_frontend.h"
#include "tools/nr_dev/tests/native_default_paths_fixture.h"
#include <filesystem>
#include <fstream>
#include <unistd.h>
namespace S=BbNativeAsync::SceneFirst;
int main(int argc,char**argv){
 unsigned n=0;const auto check=[&](bool b){++n;if(!b)std::abort();};
 check(argc==2);
 const std::filesystem::path home=std::filesystem::path(argv[1])/"fixture-home";
 const auto legacy=home/"Code/nrbridge-design/game-poc-20261011/bbport";
 std::filesystem::create_directories(legacy/"runs");
 NativeDefaultPathsFixture::UseHome(home);
 check(S::ValidConfiguration("0"));check(S::ValidConfiguration("1"));
 for(const auto*bad:{"","2","true","01","1\n"})check(!S::ValidConfiguration(bad));
 check(S::ExactToken("cpu\n","cpu"));
 for(const auto*bad:{"cpu","cpu\r\n","cpu\nextra","other\n"," cpu\n"})check(!S::ExactToken(bad,"cpu"));
 check(!S::ExactToken("\n",""));
 check(S::CanStartPeer(true,true,false));
 for(unsigned k=0;k<3;++k)check(!S::CanStartPeer(k!=0,k!=1,k==2));
 S::NoPeerExitProof p{.no_peer=true,.no_retained=true,.no_snapshot_owner=true,.no_pending=true,.frontend_joined=true};
 check(S::CanExitWithoutPeer(p));
 for(unsigned k=0;k<11;++k){auto bad=p;
  switch(k){case 0:bad.no_peer=false;break;case 1:bad.no_retained=false;break;
   case 2:bad.no_snapshot_owner=false;break;case 3:bad.no_pending=false;break;
   case 4:bad.frontend_joined=false;break;case 5:bad.unknown=true;break;
   case 6:bad.peer_creation_attempted=true;break;case 7:bad.snapshot_creation_attempted=true;break;
   case 8:bad.copies_ever=1;break;case 9:bad.maximum_native_use_tick=1;break;case 10:bad.chain_size=1;break;}
  check(!S::CanExitWithoutPeer(bad));
 }
 // These are real managed CPU threads/files, not a Vulkan/Helper readiness proof.
 const std::string session="cpu-scene-first-"+std::to_string(getpid());
 const std::filesystem::path run=legacy/("runs/native-async-game-"+session);
 check(std::filesystem::create_directory(run));
 struct Cleanup{std::filesystem::path p;~Cleanup(){std::filesystem::remove_all(p);}}cleanup{run};
 const auto arm=run/"native-scene-arm";
 setenv("BB_NR_NATIVE_ASYNC_RUN_DIR",run.c_str(),1);setenv("BB_NR_NATIVE_ASYNC_SCENE_ARM",arm.c_str(),1);
 setenv("BB_NR_NATIVE_ASYNC_SESSION",session.c_str(),1);setenv("BB_NR_NATIVE_ASYNC_MODE","resident",1);
 setenv("BB_NR_NATIVE_ASYNC_SCENE_FIRST","1",1);check(S::Active());
 const auto until=[](auto predicate){const auto limit=std::chrono::steady_clock::now()+std::chrono::seconds(2);
  while(!predicate()&&std::chrono::steady_clock::now()<limit){std::this_thread::sleep_for(std::chrono::milliseconds(1));}
  return predicate();};
 {BbNativeAsync::Frontend f;check(!f.HelperStartRequested()&&!f.Armed());
  std::ofstream(run/"native-helper-start")<<session;std::this_thread::sleep_for(std::chrono::milliseconds(130));check(!f.HelperStartRequested());
  std::ofstream(run/"native-helper-start")<<session+"\n";check(until([&]{return f.HelperStartRequested();}));check(!f.Armed());
  std::ofstream(arm)<<session+"\n";check(until([&]{return f.Armed()&&f.Finished();}));
  f.JoinFinished();check(f.Joined());
 }
 std::filesystem::remove(arm);std::filesystem::remove(run/"native-helper-start");
 {BbNativeAsync::Frontend f;check(!f.HelperStartRequested());f.RequestStop();check(until([&]{return f.Finished();}));f.JoinFinished();check(f.Joined());}
 setenv("BB_NR_NATIVE_ASYNC_SCENE_FIRST","0",1);check(!S::Active());
 {BbNativeAsync::Frontend f;check(f.HelperStartRequested());std::ofstream(arm)<<session+"\n";check(until([&]{return f.Armed()&&f.Finished();}));f.JoinFinished();check(f.Joined());}
 std::filesystem::remove(arm);setenv("BB_NR_NATIVE_ASYNC_SCENE_FIRST","1",1);setenv("BB_NR_NATIVE_ASYNC_MODE","localnoop",1);check(!S::Active());
 {BbNativeAsync::Frontend f;check(f.HelperStartRequested());std::ofstream(arm)<<session+"\n";check(until([&]{return f.Armed()&&f.Finished();}));f.JoinFinished();check(f.Joined());}
 // A daily override cannot make the old PoC frontend follow a new root.
 const auto daily=std::filesystem::path(argv[1])/"daily";
 const auto daily_run=daily/("runs/native-async-game-"+session);
 std::filesystem::create_directories(daily_run);
 std::ofstream(daily_run/"native-scene-arm")<<session+"\n";
 setenv("BB_NR_DAILY","1",1);setenv("NRBRIDGE_HOME",daily.c_str(),1);
 setenv("BB_NR_NATIVE_ASYNC_RUN_DIR",daily_run.c_str(),1);
 setenv("BB_NR_NATIVE_ASYNC_SCENE_ARM",(daily_run/"native-scene-arm").c_str(),1);
 {BbNativeAsync::Frontend f;check(f.Finished()&&f.Joined()&&!f.Armed());}
 setenv("BB_NR_NATIVE_ASYNC_RUN_DIR",run.c_str(),1);setenv("BB_NR_NATIVE_ASYNC_SCENE_ARM",arm.c_str(),1);
 setenv("HOME",daily.c_str(),1);
 {BbNativeAsync::Frontend f;check(f.Finished()&&f.Joined()&&!f.Armed());}
 std::printf("{\"status\":\"PASS\",\"checks\":%u,\"scope\":\"actual_managed_frontend_CPU_tokens_join_absent_owner_negative_proofs_not_GPU\",\"GPU\":\"NOT_RUN\"}\n",n);
}
