#include "gpu/render/native_origin_identity_policy.h"
#include "gpu/render/native_async_frontend.h"
#include "tools/nr_dev/tests/native_default_paths_fixture.h"
#include <cstdlib>
#include <iostream>
using namespace BbNativeAsync::IdentityPolicy;
unsigned checks=0;
void need(bool b){++checks;if(!b)std::abort();}
int main(int argc,char**argv){
 need(argc==2);
 Expected e{"/private/origin.exe","/private/run","skipEvaluate","session",
   "/pinned/wine","/pinned/wine-preloader"};
 Candidate c{.pid=44,.game=20,.launcher=44,.start_before=123,.start_after=123,
 .descendant_game=true,.descendant_launcher=true,.exact_pe_maps=true,
 .elf=e.preloader,.argv={e.helper,e.mode,e.run,e.session}};
 need(Check(c,e).Accepted()); // Preloader is configured; all PE evidence remains mandatory.
 c.elf=e.wine;need(Check(c,e).Accepted());
 for(const auto&path:{"/other/wine-preloader","/pinned/wineserver","/pinned/wine64-preloader",
                      "/pinned/wine-preloader (deleted)","/pinned/wine-malicious"}){
  auto x=c;x.elf=path;need(!Check(x,e).Accepted());
 }
 {auto x=c;x.exact_pe_maps=false;need(!Check(x,e).Accepted());}
 {auto x=c;x.descendant_game=false;need(!Check(x,e).Accepted());}
 {auto x=c;x.descendant_launcher=false;need(!Check(x,e).Accepted());}
 {auto x=c;x.start_after++;need(!Check(x,e).Accepted());}
 {auto x=c;x.start_before=0;x.start_after=0;need(!Check(x,e).Accepted());}
 {auto x=c;x.argv.push_back("extra");need(!Check(x,e).Accepted());}
 {auto x=c;x.argv[1]="resident";need(!Check(x,e).Accepted());}
 {auto x=c;x.argv[2]+="-suffix";need(!Check(x,e).Accepted());}
 {auto x=c;x.argv[3]+="-suffix";need(!Check(x,e).Accepted());}
 {auto x=c;x.argv[0]="other.exe";need(!Check(x,e).Accepted());}
 {auto x=c;x.pid=x.game;need(!Check(x,e).Accepted());}
 EmptyBootstrapExit f{.failed=true,.worker_finished=true,.spawned=true,.reaped=true,
  .import_objects_destroyed=true};
 need(SafeAbnormalClose(f)); // Actual abnormal wait status is kept elsewhere, never normal0.
 {auto x=f;x.reaped=false;need(!SafeAbnormalClose(x));}
 {auto x=f;x.worker_finished=false;need(!SafeAbnormalClose(x));}
 {auto x=f;x.import_objects_destroyed=false;need(!SafeAbnormalClose(x));}
 {auto x=f;x.imports_ever_created=1;need(!SafeAbnormalClose(x));}
 {auto x=f;x.has_snapshot=true;need(!SafeAbnormalClose(x));}
 {auto x=f;x.has_pending_lease=true;need(!SafeAbnormalClose(x));}
 {auto x=f;x.source_copies_ever=1;need(!SafeAbnormalClose(x));}
 {auto x=f;x.native_last_use=7;x.native_observed=7;need(!SafeAbnormalClose(x));} // N0 is insufficient.
 {auto x=f;x.native_observed=7;need(!SafeAbnormalClose(x));}
 {auto x=f;x.failed=false;need(!SafeAbnormalClose(x));}
 {auto x=f;x.spawned=false;x.reaped=false;need(SafeAbnormalClose(x));}
 // Real controlled managed CPU thread; only a temporary root, no game/Wine/GPU.
 const std::filesystem::path home=std::filesystem::path(argv[1])/"identity-home";
 const auto legacy=home/"Code/nrbridge-design/game-poc-20261011/bbport";
 std::filesystem::create_directories(legacy);
 NativeDefaultPathsFixture::UseHome(home);
 const auto run=legacy/"runs/native-async-game-cpu-identity-unused-path";
 setenv("BB_NR_NATIVE_ASYNC_SESSION","cpu-identity-unused-path",1);
 setenv("BB_NR_NATIVE_ASYNC_RUN_DIR",run.c_str(),1);
 setenv("BB_NR_NATIVE_ASYNC_SCENE_ARM",(run/"native-scene-arm").c_str(),1);
 {
  BbNativeAsync::Frontend frontend;frontend.RequestStop();
  const auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(2);
  while(!frontend.Finished()&&std::chrono::steady_clock::now()<deadline)std::this_thread::sleep_for(std::chrono::milliseconds(1));
  need(frontend.Finished());frontend.JoinFinished();need(!frontend.Armed());
 }
 std::cout<<"{\"status\":\"PASS_CPU_PRODUCTION_IDENTITY_EXIT_POLICY_ONLY\",\"checks\":"<<checks
          <<",\"Wine_GPU\":\"NOT_RUN\",\"failed_case_actual_elf\":\"NOT_ARCHIVED_UNKNOWN\"}\n";
}
