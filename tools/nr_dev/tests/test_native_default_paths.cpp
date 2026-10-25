// Real CPU filesystem checks against the production path helpers.
#include "gpu/render/native_dev_paths.h"
#include "native_default_paths_fixture.h"
#include <cassert>
#include <iostream>
int main(int argc,char** argv) {
 assert(argc==2);
 namespace D=NativeDefaultPaths;
 namespace F=NativeDefaultPathsFixture;
 namespace P=BbNativeAsyncPreSr;
 namespace fs=std::filesystem;
 const fs::path scratch=argv[1],home=scratch/"relocated-user";
 const auto dev=home/"Code/nr-home";
 const auto legacy=home/"Code/nrbridge-design/game-poc-20261011/bbport";
 const auto ge=home/".local/share/Steam/compatibilitytools.d/GE-Proton11-7-x86_64/files";
 fs::create_directories(home);
 F::UseHome(home);
 assert(D::UserHome()==home);
 assert(D::DevRoot().empty()&&D::LegacyPocRoot().empty()&&D::LegacyGeFiles().empty());
 for(const auto& path:{dev/"runs/session",legacy/"runs",ge})fs::create_directories(path);
 assert(D::DevRoot()==dev&&D::LegacyPocRoot()==legacy&&D::LegacyGeFiles()==ge);
 setenv("BB_NR_DAILY","0",1);unsetenv("NRBRIDGE_HOME");
 assert(P::ValidDevRun((dev/"runs/session").string(),"session"));
 for(const auto* bad:{"","-session","../session","session/child","session_other"})
  assert(!P::ValidDevRun((dev/"runs/session").string(),bad));
 const auto other=scratch/"other";
 fs::create_directories(other/"runs/session");
 fs::create_directories(dev/"runs/session/child");
 fs::create_directories(dev/"runs-sibling/session");
 assert(!P::ValidDevRun((other/"runs/session").string(),"session"));
 assert(!P::ValidDevRun((dev/"runs/session/child").string(),"session"));
 assert(!P::ValidDevRun((dev/"runs-sibling/session").string(),"session"));
 assert(!P::ValidDevRun((dev/"runs/../runs/session").string(),"session"));
 fs::create_directory_symlink(dev/"runs/session",dev/"runs/linked");
 assert(!P::ValidDevRun((dev/"runs/linked").string(),"linked"));
 setenv("NRBRIDGE_HOME",other.c_str(),1);
 assert(P::ValidDevRun((dev/"runs/session").string(),"session"));
 assert(!P::ValidDevRun((other/"runs/session").string(),"session"));
 setenv("BB_NR_DAILY","1",1);
 assert(P::ValidDevRun((other/"runs/session").string(),"session"));
 assert(!P::ValidDevRun((dev/"runs/session").string(),"session"));
 assert(D::LegacyPocRoot()==legacy); // Daily never relocates archived PoCs.
 setenv("HOME",other.c_str(),1);
 assert(D::UserHome().empty()&&D::DevRoot().empty()&&D::LegacyPocRoot().empty()&&D::LegacyGeFiles().empty());
 assert(P::ValidDevRun((other/"runs/session").string(),"session")); // Explicit daily unchanged.
 unsetenv("NRBRIDGE_HOME");
 assert(!P::ValidDevRun((dev/"runs/session").string(),"session"));
 F::UseHome(home);
 assert(P::ValidDevRun((dev/"runs/session").string(),"session")); // Missing daily override retains fallback.
 setenv("NRBRIDGE_HOME","",1);
 assert(!P::ValidDevRun((dev/"runs/session").string(),"session"));
 setenv("NRBRIDGE_HOME","relative",1);
 assert(!P::ValidDevRun((dev/"runs/session").string(),"session"));
 fs::create_directory_symlink(other,scratch/"linked-root");
 setenv("NRBRIDGE_HOME",(scratch/"linked-root").c_str(),1);
 assert(!P::ValidDevRun((scratch/"linked-root/runs/session").string(),"session"));
 unsetenv("NRBRIDGE_HOME");
 for(const auto* bad:{"","relative","/"}) {
  F::UseHome(bad);assert(D::UserHome().empty()&&D::DevRoot().empty());
 }
 F::UseHome(scratch/"absent");assert(D::UserHome().empty());
 fs::create_directory_symlink(home,scratch/"linked-home");
 F::UseHome(scratch/"linked-home");assert(D::UserHome().empty());
 F::UseHome(home);
 F::error=EIO;assert(D::UserHome().empty());
 F::error=0;F::missing=true;assert(D::UserHome().empty());
 F::missing=false;F::wrong_uid=true;assert(D::UserHome().empty());
 F::wrong_uid=false;F::retry=true;F::calls=0;assert(D::UserHome()==home&&F::calls==2);
 F::retry=false;F::error=ERANGE;assert(D::UserHome().empty()); // Bounded retry, no fallback.
 F::UseHome(home);
 unsetenv("HOME");assert(D::UserHome()==home);
 setenv("HOME","",1);assert(D::UserHome().empty());
 F::UseHome(home);
 fs::rename(dev,home/"Code/real-dev");
 fs::create_directory_symlink(home/"Code/real-dev",dev);
 assert(D::DevRoot().empty());
 assert(!P::ValidDevRun((dev/"runs/session").string(),"session"));
 fs::rename(ge,ge.string()+"-real");
 fs::create_directory_symlink(ge.string()+"-real",ge);
 assert(D::LegacyGeFiles().empty());
 fs::rename(legacy,legacy.string()+"-real");
 fs::create_directory_symlink(legacy.string()+"-real",legacy);
 assert(D::LegacyPocRoot().empty());
 std::cout<<"Native default relocation/confinement CPU checks PASS; GPU NOT RUN\n";
}
