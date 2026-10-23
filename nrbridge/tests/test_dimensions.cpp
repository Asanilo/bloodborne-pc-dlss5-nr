#include "pre_sr_wire.h"
#include <cassert>
#include <cstdlib>
int main(){
 setenv("BB_NR_DAILY","1",1);setenv("BB_NR_DIMENSIONS_V3","1",1);
 setenv("BB_NR_MODEL_WIDTH","1130",1);setenv("BB_NR_MODEL_HEIGHT","636",1);
 setenv("BB_NR_SR_OUTPUT_WIDTH","1920",1);setenv("BB_NR_SR_OUTPUT_HEIGHT","1080",1);
 using namespace BbNRDimensions;
 const auto& d=Get();assert(d.Valid()&&d.enabled&&d.Pixels()==718680);
 assert(GroupsX()==142&&GroupsY()==80); // partial right/bottom workgroups are not dropped
 auto e=origin_wire::config_expected("cpu-m5","resident",std::string(32,'a'),std::string(32,'b'));
 assert(e["protocol_version"]==3&&e["width"]==1130&&e["quality_profile"]["guide_height"]==636);
 origin_wire::config(e,"cpu-m5","resident");
 e["width"]=1280u;bool rejected=false;try{origin_wire::config(e,"cpu-m5","resident");}catch(...){rejected=true;}assert(rejected);
 auto common=origin_wire::common("cpu-m5","resident");assert(common["protocol_version"]==3);
 auto wrong=common;wrong["quality_profile"]["sr_output_width"]=2560u;
 rejected=false;try{origin_wire::common_check(wrong,common);}catch(...){rejected=true;}assert(rejected);
 assert(common["quality_profile"]["nr_mvec_scale_x"]==1.0); // stored MV remains render pixels
 auto bad=d;bad.width=4096;bad.height=2160;assert(!bad.Valid());bad=d;bad.width=1131;assert(!bad.Valid());
 rejected=false;try{Positive("1130junk");}catch(...){rejected=true;}assert(rejected);
}
