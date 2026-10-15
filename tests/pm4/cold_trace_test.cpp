#include "bbport_pm4_cold_trace.h"
#include <cassert>
#include <string>
#include <fstream>
int main(int argc,char**){
 if(argc>1){unsetenv("BB_NR_PM4_COLD_TRACE");assert(!BbPm4Cold::Enabled());uint32_t data=0;{BbPm4Cold::CsWrite disabled("disabled",&data,1,0);disabled.Written(1);}assert(BbPm4Cold::Records().sequence==0);return 0;}
 setenv("BB_NR_PM4_COLD_TRACE","1",1);assert(BbPm4Cold::Enabled());
 std::array<uint32_t,15> broken{0xc00a1000,0,0xc0016900,0x358,0,0xc0016900,0x367,0,0xc0016900,0x376,0,0xc0016900,0x385,0,0xc0086900};
 auto bad=BbPm4Cold::Validate(broken);assert(!bad.valid&&bad.offset==12&&bad.word==0x385);
 auto legal=broken;legal[12]=0xc0016900;legal[13]=0x385;legal[14]=0;assert(BbPm4Cold::Validate(legal).valid);
 std::array<uint32_t,1> one_nop{0xffff1000};assert(BbPm4Cold::Validate(one_nop).valid);
 std::array<uint32_t,2> short_packet{0xc0086900,0};assert(!BbPm4Cold::Validate(short_packet).valid);
 const auto original=BbPm4Cold::Digest(legal);legal[13]++;assert(original!=BbPm4Cold::Digest(legal));
 for(unsigned i=0;i<100;i++){BbPm4Cold::CsWrite trace("cpu",broken.data(),broken.size(),0x1234);trace.Written(15);}
 auto& records=BbPm4Cold::Records();assert(records.sequence==100&&records.ring.size()==64);assert(records.ring[99%64].written==15&&records.ring[99%64].head[12]==0x385);
 char path[]="/tmp/pm4-cold-trace-test-XXXXXX";int fd=mkstemp(path);assert(fd>=0);int previous=dup(STDERR_FILENO);assert(dup2(fd,STDERR_FILENO)>=0);close(fd);
 for(unsigned i=0;i<10;i++)BbPm4Cold::Dump("cpu",reinterpret_cast<uintptr_t>(broken.data()),broken,bad,BbPm4Cold::Digest(broken));
 assert(records.dumps==4);assert(dup2(previous,STDERR_FILENO)>=0);close(previous);
 std::ifstream file(path);std::string text((std::istreambuf_iterator<char>(file)),{});assert(text.find("actual_written_dw=15")!=std::string::npos&&text.find("PM4_COLD_DCB")!=std::string::npos&&text.find("dump=5")==std::string::npos);unlink(path);
}
