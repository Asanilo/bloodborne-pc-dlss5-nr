#include "video_core/renderer_vulkan/native_origin_wire.h"
#include <iostream>
using namespace Vulkan::NativeOriginWire;
unsigned checks=0;
template<class F>void reject(F f){++checks;try{f();throw std::runtime_error("expected reject");}catch(const std::invalid_argument&){}}
int main(){
 const std::string session="cpu-scene-arm",mode="resident";
 auto c=common(session,mode);
 auto cfg=config_expected(session,mode,std::string(32,'0'),std::string(32,'1'));config(parse(cfg.dump()),session,mode);++checks;
 reject([&]{auto x=cfg;x["max_jobs"]=true;config(parse(x.dump()),session,mode);});
 reject([&]{auto x=cfg;x["max_jobs"]=5u;config(parse(x.dump()),session,mode);});
 reject([&]{parse("{\"a\":1,\"a\":2}");});
 reject([&]{parse(std::string(65537,' '));});
 Counts count;
 auto fields=count.fields();count.check(fields);++checks;
 auto use=J{{"native_last_use_tick",7u},{"native_last_use_observed_tick",7u},
 {"native_last_use_proof","ACTUAL_NATIVE_GPU_LAST_USE"},{"completed_GPU",true}};
 native_last_use(use);++checks; // N0 is not NO_NATIVE_USE.
 reject([&]{auto x=use;x["native_last_use_observed_tick"]=6u;native_last_use(x);});
 reject([&]{auto x=use;x["completed_GPU"]=false;native_last_use(x);});
 reject([&]{auto x=use;x["native_last_use_proof"]="NOT_REQUIRED_NO_NATIVE_USE";native_last_use(x);});
 auto zero=J{{"native_last_use_tick",0u},{"native_last_use_observed_tick",0u},
 {"native_last_use_proof","NOT_REQUIRED_NO_NATIVE_USE"},{"completed_GPU",false}};
 native_last_use(zero);++checks;
 auto input=c;input.update(J{{"job",0u},{"origin_source",50u},{"origin_epoch",1u},{"origin_history",1u},
 {"op","input"},{"status","ORIGIN_INPUT_READY"},{"native_input_tick",100u},{"native_input_GPU_complete",true},
 {"source_reset",false},{"model_reset",1u},{"source_jitter_metadata",J::object()}});
 for(auto i=spec()["source_jitter_metadata_exact"].begin();i!=spec()["source_jitter_metadata_exact"].end();++i)
  input["source_jitter_metadata"][i.key()]=.25;
 auto wire=parse(input.dump());Vulkan::NativeOriginWire::input(wire,c,0);++checks;
 reject([&]{auto x=wire;x["source_reset"]=0u;Vulkan::NativeOriginWire::input(x,c,0);});
 reject([&]{auto x=wire;x["native_input_GPU_complete"]=false;Vulkan::NativeOriginWire::input(x,c,0);});
 reject([&]{auto x=wire;x["source_jitter_metadata"]["wrong_alias"]=0.;Vulkan::NativeOriginWire::input(x,c,0);});
 count.finalize(wire,"captured");count.check(count.fields());++checks;
 reject([&]{count.finalize(wire,"captured");});
 reject([&]{auto x=wire;x["job"]=9007199254740992ull;job_fields(x);});
 const auto path=job_path("/private","complete",session,3);
 if(path!="/private/frames/bucket-000000/async-complete-cpu-scene-arm-g0-f3.json")return 2;
 ++checks;
 reject([&]{job_path("/private","complete",session,4);});
 std::cout<<"{\"status\":\"PASS_CPU_WIRE_ONLY\",\"checks\":"<<checks<<",\"GPU\":\"NOT_RUN\"}\n";
}
