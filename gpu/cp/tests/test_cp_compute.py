#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Exercise actual RunCompute with CPU collaborators; no Vulkan or guest execution."""
from pathlib import Path
import argparse,hashlib,json,os,runpy,subprocess,tempfile
root=Path(__file__).resolve().parents[3]
parser=argparse.ArgumentParser()
parser.add_argument('--output',type=Path)
args=parser.parse_args()
extract=runpy.run_path(str(root/'gpu/cp/tests/test_cp_ownership.py'))['function']
body=extract((root/'gpu/cp/cp_compute.cpp').read_text(),
             'CommandProcessor::Status CommandProcessor::RunCompute(')
prefix=r'''
#include <algorithm>
#include <array>
#include <cassert>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <span>
#include <stdexcept>
#include <vector>
#include "cp/cp_pm4.h"
#include "gcn/pm4_register_guard.h"
using u32=uint32_t;
#define UNREACHABLE_MSG(...) throw std::runtime_error("rejected")
namespace Common{struct SlotId{u32 value;};}
namespace BbCp{
struct CommandProcessor{
 enum class Status{Done,Wait};enum class Step{Next,Moved,Wait};
 struct Level{const u32*begin;const u32*at;const u32*end;};
 struct Walk{
  std::array<Level,10>levels;int top=-1;
  void Push(std::span<const u32>b,int){levels[++top]={b.data(),b.data(),b.data()+b.size()};}
  bool Done(){return top<0;}
 };
 struct Work{u32 queue=1;Walk walk;bool started=false;std::span<const u32>dcb;
  std::vector<u32>assembled;u32 assembled_used=0;};
 struct Ring{u32*read_addr;u32 ring_size_dw=16;std::array<u32,1024>tmp_packet{};u32 tmp_dwords=0;};
 struct Rings{Ring item;Ring&operator[](Common::SlotId){return item;}}asc_queues;
 std::vector<std::vector<u32>>seen;
 bool wait_once=false;
 void RunCommands(){}
 Step Compute(Work&,Level&,const u32*p,u32 n){
  if(wait_once){wait_once=false;return Step::Wait;}
  seen.emplace_back(p,p+n+1);return Step::Next;
 }
 Status RunCompute(Work&);
};
'''
main=r'''
}
int main(){
 using namespace BbCp;using CP=CommandProcessor;
 const u32 head=0xc0000000u|(3u<<16)|(u32(Op::SetShReg)<<8); // 5 words total
 std::array<u32,5> packet{head,0,11,22,33};
 CP cp;u32 read=0;cp.asc_queues.item.read_addr=&read;
 for(auto part:{std::span<const u32>{packet.data(),2},
                std::span<const u32>{packet.data()+2,1}}){
  CP::Work w;w.dcb=part;
  assert(cp.RunCompute(w)==CP::Status::Done);assert(cp.seen.empty());
 }
 assert(read==3&&cp.asc_queues.item.tmp_dwords==3);
 CP::Work tail;tail.dcb={packet.data()+3,2};cp.wait_once=true;
 assert(cp.RunCompute(tail)==CP::Status::Wait);assert(read==3);
 assert(cp.RunCompute(tail)==CP::Status::Done);
 assert(read==5 && cp.seen.size()==1 && cp.seen[0]==std::vector<u32>(packet.begin(),packet.end()));
 assert(cp.asc_queues.item.tmp_dwords==0 && tail.assembled_used==0);
 auto reject=[&](std::vector<u32> words,bool nested){
  CP bad;u32 p=0;bad.asc_queues.item.read_addr=&p;
  CP::Work w;w.dcb=words;
  if(nested){w.started=true;w.walk.Push({},0);w.walk.Push(words,1);}
  bool failed=false;try{bad.RunCompute(w);}catch(const std::runtime_error&){failed=true;}
  assert(failed&&bad.seen.empty()&&p==0);
 };
 reject({0xffff7600u,0,1},false);       // oversized split count before scratch copy
 reject({head,0,1},true);              // truncated IB, no ring continuation
 reject({0xc0017600u,0x400,1},false);  // outside SH bank
 puts("PASS: actual RunCompute three-fragment assembly/wait retry; oversized split/truncated IB/bank rejection");
}
'''
with tempfile.TemporaryDirectory(prefix="bb-cp-compute-") as temporary:
 out=args.output or Path(temporary)
 out.mkdir(parents=True,exist_ok=True)
 p=out/'compute-extracted.cpp';p.write_text(prefix+body+main)
 exe=out/'compute-check'
 subprocess.run(['clang++','-std=c++23','-O1','-g','-UNDEBUG','-Wall','-Wextra','-Werror',
  '-fsanitize=address,undefined','-fno-omit-frame-pointer','-I'+str(root/'gpu'),str(p),
  '-o',str(exe)],check=True)
 result=subprocess.run([str(exe)],capture_output=True,text=True)
 (out/'compute.log').write_text(result.stdout+result.stderr)
 print(result.stdout+result.stderr,end='')
 assert result.returncode==0,result.returncode
 (out/'source.json').write_text(json.dumps({
  'source_sha256':hashlib.sha256((root/'gpu/cp/cp_compute.cpp').read_bytes()).hexdigest(),
  'exit':result.returncode},indent=2)+'\n')
