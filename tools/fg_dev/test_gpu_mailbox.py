"""CPU contract rejection checks; none of these assert actual GPU completion."""
from pathlib import Path
import subprocess
import tempfile
import unittest


class GpuWireTests(unittest.TestCase):
    def test_request_epoch_and_consumption_ownership(self):
        root=Path(__file__).resolve().parents[2]
        code=r'''
#include "gpu/upscale/dlssg_gpu_contract.h"
#include <cassert>
using namespace DlssgGpu;
template<class F> void rejects(F f){bool bad=false;try{f();}catch(...){bad=true;}assert(bad);}
int main(){
 J request{{"schema",Schema},{"session","cpu"},{"sequence",1},{"epoch",1},
 {"source_game_frame",42},{"previous_source_game_frame",0},{"display",123},
 {"reset",true},{"native_input_GPU_complete",true},{"native_input_tick",10},
 {"wire_order","game-scene-bytes"}};
 Request(request,"cpu",0,0,0,0);
 J ack=Pair(request);ack.update(J{{"helper_serial_echo",2},{"native_last_use_tick",11},
 {"native_GPU_complete",true},{"output_released",true}});Consumed(ack,request,2);
 auto bad=ack;bad["native_last_use_tick"]=10;rejects([&]{Consumed(bad,request,2);});
 bad=ack;bad["output_released"]=false;rejects([&]{Consumed(bad,request,2);});
 bad=ack;bad["session"]="old";rejects([&]{Consumed(bad,request,2);});
 bad=ack;bad["display"]=124;rejects([&]{Consumed(bad,request,2);});
 rejects([&]{Consumed(ack,request,3);});
 auto split=request;split["native_completion_mode"]="split-v1";split["native_input_domain"]="draw";
 Request(split,"cpu",0,0,0,0);
 auto sa=Pair(split);sa.update(J{{"helper_serial_echo",2},{"native_last_use_tick",1},{"native_last_use_domain","consumer"},{"native_GPU_complete",true},{"output_released",true}});
 Consumed(sa,split,2); // Independent timeline tick may be smaller than draw tick.
 bad=sa;bad["native_last_use_domain"]="draw";rejects([&]{Consumed(bad,split,2);});
 bad=sa;bad["native_last_use_tick"]=0;rejects([&]{Consumed(bad,split,2);});
 bad=sa;bad["native_GPU_complete"]=false;rejects([&]{Consumed(bad,split,2);});
 rejects([&]{Consumed(ack,split,2);});
 bad=split;bad["native_input_domain"]="consumer";rejects([&]{Request(bad,"cpu",0,0,0,0);});
 for(unsigned count=2;count<=5;++count){auto multi=split;multi["generated_count"]=count;
  auto ma=Pair(multi);ma.update(J{{"helper_serial_echo",2},{"native_last_use_tick",1},{"native_last_use_domain","consumer"},{"native_GPU_complete",true},{"output_released",true}});
  ma["completed_outputs"]=J::array();for(unsigned i=1;i<=count;++i)ma["completed_outputs"].push_back(i);
  Consumed(ma,multi,2);auto missing=ma;missing["completed_outputs"].erase(count-1);rejects([&]{Consumed(missing,multi,2);});
  auto duplicate=ma;duplicate["completed_outputs"][count-1]=1;rejects([&]{Consumed(duplicate,multi,2);});
  auto drift=multi;drift["generated_count"]=count==5?2:5;rejects([&]{Consumed(ma,drift,2);});
 }
 request["sequence"]=2;request["previous_source_game_frame"]=42;request["source_game_frame"]=44;
 request["native_input_tick"]=12;request["reset"]=false;
 rejects([&]{Request(request,"cpu",1,1,42,11);});
 request["epoch"]=2;request["reset"]=true;Request(request,"cpu",1,1,42,11);
 request["native_input_tick"]=11;rejects([&]{Request(request,"cpu",1,1,42,11);});
 request["native_input_tick"]=12.0;rejects([&]{Request(request,"cpu",1,1,42,11);});
}
'''
        with tempfile.TemporaryDirectory() as temp:
            exe=Path(temp)/'contract'
            subprocess.run(['c++','-std=c++17','-Wall','-Wextra','-Werror','-I',str(root),
                            '-I',str(root/'gpu/third_party/nlohmann/include'),'-x','c++','-','-o',str(exe)],
                           input=code,text=True,check=True)
            subprocess.run([exe],check=True)


if __name__=='__main__':unittest.main()
