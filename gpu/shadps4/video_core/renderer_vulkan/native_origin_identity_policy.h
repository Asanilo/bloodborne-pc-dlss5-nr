// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include <cstdint>
#include <string>
#include <vector>
namespace BbNativeAsync::IdentityPolicy {
struct Expected {
 std::string helper,run,mode,session,wine,preloader;
};
struct Candidate {
 int64_t pid=0,game=0,launcher=0;
 uint64_t start_before=0,start_after=0;
 bool descendant_game=false,descendant_launcher=false,exact_pe_maps=false;
 std::string elf;
 std::vector<std::string> argv;
};
struct Decision {
 bool pid=false,ancestry=false,start_stable=false,argv=false,elf=false,maps=false;
 bool Accepted()const noexcept{return pid&&ancestry&&start_stable&&argv&&elf&&maps;}
};
inline Decision Check(const Candidate&c,const Expected&e) {
 const auto slash=e.helper.rfind('/');
 const auto base=slash==std::string::npos?e.helper:e.helper.substr(slash+1);
 return {
  .pid=c.pid>1&&c.game>1&&c.launcher>1&&c.pid!=c.game,
  .ancestry=c.descendant_game&&c.descendant_launcher,
  .start_stable=c.start_before>0&&c.start_before==c.start_after,
  .argv=c.argv.size()==4&&(c.argv[0]==e.helper||c.argv[0]==base)&&
    c.argv[1]==e.mode&&c.argv[2]==e.run&&c.argv[3]==e.session,
  // Only the two configured/pinned absolute ELF paths. Not a "wine" substring.
  .elf=!c.elf.empty()&&((!e.wine.empty()&&c.elf==e.wine)||
                       (!e.preloader.empty()&&c.elf==e.preloader)),
  .maps=c.exact_pe_maps
 };
}
struct EmptyBootstrapExit {
 bool failed=false,worker_finished=false,spawned=false,reaped=false;
 bool import_objects_destroyed=false,has_snapshot=false,has_pending_lease=false;
 uint64_t imports_ever_created=0,source_copies_ever=0,native_last_use=0,native_observed=0;
};
inline bool SafeAbnormalClose(const EmptyBootstrapExit&f)noexcept {
 return f.failed&&f.worker_finished&&(!f.spawned||f.reaped)&&
  f.import_objects_destroyed&&!f.has_snapshot&&!f.has_pending_lease&&
  !f.imports_ever_created&&!f.source_copies_ever&&!f.native_last_use&&!f.native_observed;
}
}
