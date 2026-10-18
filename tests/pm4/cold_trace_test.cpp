#include "bbport_pm4_cold_trace.h"
#include <cassert>
#include <string>
#include <fstream>
#include <sys/mman.h>
static unsigned context_calls=0;
static bool known_context=true,failed_context=false;
static uint64_t context_wrapper=0x1075840,context_caller=0x8011fe65f;
static BbGnmHooks::ProducerContext FakeContext(const uint32_t* p){
 ++context_calls;
 BbGnmHooks::ProducerContext c{};
 c.known=known_context;c.read_failed=failed_context;c.exact_record=true;c.record_type_matches=false;c.record_target_matches=false;
 c.wrapper=context_wrapper;c.caller=context_caller;c.cb=0x1234;c.cursor=reinterpret_cast<uintptr_t>(p);
 c.exact_record_index=3;c.record_count=5;c.record_target=0x9876;
 c.record[0]=0x9f;c.record[1]=0x0604;c.record[2]=4;
 c.target_word_count=1;c.target_before[0]=p[0];
 return c;
}
int main(int argc,char**){
 if(argc>1){
  unsetenv("BB_NR_PM4_COLD_TRACE");assert(!BbPm4Cold::Enabled());
  // Even an inaccessible pointer must remain untouched on the disabled path.
  {BbPm4Cold::ShaderWrite disabled("disabled",reinterpret_cast<uint32_t*>(1),40,0,FakeContext);disabled.Footprint(40);}
  assert(BbPm4Cold::Records().sequence==0&&BbPm4Cold::Records().control_count==0&&context_calls==0);return 0;
 }
 setenv("BB_NR_PM4_COLD_TRACE","1",1);assert(BbPm4Cold::Enabled());
 std::array<uint32_t,15> broken{0xc00a1000,0,0xc0016900,0x358,0,0xc0016900,0x367,0,0xc0016900,0x376,0,0xc0016900,0x385,0,0xc0086900};
 auto bad=BbPm4Cold::Validate(broken);assert(!bad.valid&&bad.offset==12&&bad.word==0x385);
 auto legal=broken;legal[12]=0xc0016900;legal[13]=0x385;legal[14]=0;assert(BbPm4Cold::Validate(legal).valid);
 std::array<uint32_t,1> one_nop{0xffff1000};assert(BbPm4Cold::Validate(one_nop).valid);
 std::array<uint32_t,2> short_packet{0xc0086900,0};assert(!BbPm4Cold::Validate(short_packet).valid);
 const auto original=BbPm4Cold::Digest(legal);legal[13]++;assert(original!=BbPm4Cold::Digest(legal));
 auto& records=BbPm4Cold::Records();
 // Guard page proves the snapshot cannot read past declared logical footprint or reservation.
 const long page=sysconf(_SC_PAGESIZE);assert(page>0);
 auto* mapping=static_cast<unsigned char*>(mmap(nullptr,page*2,PROT_READ|PROT_WRITE,MAP_PRIVATE|MAP_ANONYMOUS,-1,0));
 assert(mapping!=MAP_FAILED);assert(mprotect(mapping+page,page,PROT_NONE)==0);
 auto* last=reinterpret_cast<uint32_t*>(mapping+page-sizeof(uint32_t));*last=0xdeadbeef;
 {BbPm4Cold::ShaderWrite t("one-footprint",last,40,0);t.Footprint(1);}
 assert(records.ring[0].snapshot_dw==1&&records.ring[0].head[0]==0xdeadbeef);
 {BbPm4Cold::ShaderWrite t("one-reserved",last,1,0);t.Footprint(40);}
 assert(records.ring[1].snapshot_dw==1&&records.ring[1].footprint==40);
 {BbPm4Cold::ShaderWrite failure("failed-validation",reinterpret_cast<uint32_t*>(mapping+page),40,0);}
 assert(records.ring[2].footprint==0&&records.ring[2].snapshot_dw==0);
 {BbPm4Cold::ShaderWrite zero("zero-reservation",reinterpret_cast<uint32_t*>(mapping+page),0,0);zero.Footprint(40);}
 assert(records.ring[3].snapshot_dw==0);
 assert(munmap(mapping,page*2)==0);
 for(unsigned i=0;i<300;i++){BbPm4Cold::ShaderWrite trace("cpu",broken.data(),broken.size(),0x1234);trace.Footprint(15);}
 assert(records.sequence==304&&records.ring.size()==256);
 assert(records.ring[303%256].footprint==15&&records.ring[303%256].head[12]==0x385);
 // The final 12-DWORD NOP contains ten residual words; preserve them as observations,
 // without claiming that the shader builder physically wrote those payload words.
 char path[]="/tmp/pm4-cold-trace-test-XXXXXX";int fd=mkstemp(path);assert(fd>=0);int previous=dup(STDERR_FILENO);assert(dup2(fd,STDERR_FILENO)>=0);close(fd);
 std::array<uint32_t,40> ps{};ps.fill(0xa5a5a5a5);ps[28]=0xc00a1000;ps[29]=0;
 {BbPm4Cold::ShaderWrite trace("ps40",ps.data(),40,0x5678,FakeContext);trace.Footprint(40);}
 const auto& psrecord=records.ring[304%256];
 assert(context_calls==1&&psrecord.context.exact_record_index==3&&psrecord.context.record_target==0x9876);
 assert(psrecord.snapshot_dw==40&&psrecord.head[28]==0xc00a1000&&psrecord.head[39]==0xa5a5a5a5);
 std::array<uint32_t,16385> oversized{};
 BbPm4Cold::Dump("oversized",reinterpret_cast<uintptr_t>(oversized.data()),oversized,BbPm4Cold::Validate(oversized),BbPm4Cold::Digest(oversized));
 for(unsigned i=0;i<10;i++)BbPm4Cold::Dump("cpu",reinterpret_cast<uintptr_t>(ps.data()),ps,BbPm4Cold::Validate(ps),BbPm4Cold::Digest(ps));
 assert(records.dumps==4&&records.control_count==1);
 auto sample=[&](bool success=true){BbPm4Cold::ShaderWrite trace("control",ps.data(),40,0x5678,FakeContext);if(success)trace.Footprint(40);};
 sample();sample();assert(records.control_count==1); // Repeat wrapper/caller emits once.
 context_caller++;known_context=false;sample();known_context=true;assert(records.control_count==1);
 sample(false);assert(records.control_count==1);
 // Exhaust all eight PS/VS slots first; CS remains independently available for loading callers.
 context_wrapper=0x10753c0;
 for(unsigned i=0;i<20;i++){context_caller=0x70000000+i;failed_context=(i==0);sample();}
 failed_context=false;
 assert(records.control_count==9&&records.control_other_count==8&&records.control_cs_count==1);
 context_wrapper=0x1075840;
 for(unsigned i=0;i<20;i++){context_caller=0x80000000+i;sample();}
 assert(records.control_count==16&&records.control_other_count==8&&records.control_cs_count==8);
 assert(records.dumps==4); // Control samples never consume or reset failure dump quota.
 BbPm4Cold::Producer mismatch{};mismatch.sequence=999;mismatch.context.known=false;
 mismatch.context.target_word_count=1;mismatch.context.target_before[0]=0x11223344;
 mismatch.context.entry_rbp=0x10000;mismatch.context.caller_frame_count=1;
 mismatch.context.caller_frames[0]={0x10000,0x10020,0x8026b26be};
 mismatch.context.caller_chain_stop="read_failed";mismatch.context.read_failed=true;
 BbPm4Cold::PrintProducer(mismatch,"mismatch_sample");
 assert(dup2(previous,STDERR_FILENO)>=0);close(previous);
 std::ifstream file(path);std::string text((std::istreambuf_iterator<char>(file)),{});
 unsigned control_lines=0,control_producers=0,control_contexts=0,control_records=0,control_before=0;
 size_t line_begin=0;
 while(line_begin<text.size()){
  const size_t line_end=text.find('\n',line_begin);const auto line=text.substr(line_begin,line_end-line_begin);
  if(line.find("phase=control_sample")!=std::string::npos){
   ++control_lines;
   control_producers+=line.find("PM4_COLD_PRODUCER seq=")==0;
   control_contexts+=line.find("PM4_COLD_CONTEXT seq=")==0;
   control_records+=line.find("PM4_COLD_RECORD seq=")==0;
   control_before+=line.find("PM4_COLD_BEFORE seq=")==0;
   assert(line.find("PM4_COLD_DCB")==std::string::npos);
  }
  if(line_end==std::string::npos)break;
  line_begin=line_end+1;
 }
 assert(text.find("known=1 read_failed=1 exact_record=1")!=std::string::npos);
 assert(control_lines==64&&control_producers==16&&control_contexts==16&&control_records==16&&control_before==16);
 assert(text.find("logical_footprint_dw=40")!=std::string::npos&&text.find("logical_end=")!=std::string::npos);
 assert(text.find("physical_store_count_unknown=1")!=std::string::npos&&text.find("NOP_payload_may_be_residual=1")!=std::string::npos);
 assert(text.find("snapshot_dw=40 snapshot_truncated=0")!=std::string::npos);
 assert(text.find("retained_first_seq=50 retained_last_seq=305 retained_count=256 evicted_count=49 coverage_complete=0")!=std::string::npos);
 assert(text.find("exact_record=1 record_type_matches=0 record_target_matches=0")!=std::string::npos);
 assert(text.find("record_index=3 record_target=0x9876")!=std::string::npos);
 assert(text.find("PM4_COLD_RECORD seq=305 words=0000009f,00000604,00000004,")!=std::string::npos);
 assert(text.find("PM4_COLD_BEFORE seq=305 words=a5a5a5a5,")!=std::string::npos);
 assert(text.find("PM4_COLD_BEFORE seq=999 words=11223344, phase=mismatch_sample")!=std::string::npos);
 assert(text.find("PM4_COLD_CALLER_CHAIN seq=999 entry_rbp=0x10000 count=1 stop=read_failed raw_frames=0x10000:0x10020:0x8026b26be,")!=std::string::npos);
 assert(text.find("actual_written_dw=")==std::string::npos&&text.find("actual_end=")==std::string::npos);
 assert(text.find("full_dump_omitted=1 reason=above_64KiB")!=std::string::npos);
 assert(text.find("PM4_COLD_DCB")!=std::string::npos&&text.find("dump=5")==std::string::npos);unlink(path);
}
