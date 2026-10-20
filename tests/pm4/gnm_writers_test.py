#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Compile actual GNM shader writers with CPU-only observers; no Vulkan or guest execution."""
from pathlib import Path
import argparse,subprocess,tempfile
root=Path(__file__).resolve().parents[2]
parser=argparse.ArgumentParser()
parser.add_argument('--output',type=Path)
args=parser.parse_args()
s=(root/'gpu/platform/gnm/gnm_commands.cpp').read_text()
def fun(sig):
 a=s.index(sig); p=s.index('{',a); n=1; e=p+1
 while n:
  n+=(s[e]=='{')-(s[e]=='}');e+=1
 return s[a:e]
names=['sceGnmSetCsShaderWithModifier','sceGnmSetCsShader','sceGnmSetPsShader','sceGnmSetPsShader350','sceGnmUpdatePsShader','sceGnmUpdatePsShader350','sceGnmSetVsShader','sceGnmUpdateVsShader','sceGnmSetEmbeddedPsShader','sceGnmSetEmbeddedVsShader']
code='''#include "bbport_pm4_cold_trace.h"
#include "platform/gnm/gnm_pm4.h"
#include <cassert>
#define LOG_ERROR(...) ((void)0)
namespace BbErr { constexpr int Ok=0,GnmFailure=-1; }
namespace BbGnmHooks { ProducerContext CurrentProducerContext(const uint32_t*) { return {}; } }
namespace BbGnm { using namespace Pm4; struct Observe { Observe(u32*,u32) {} };
'''
code+='template <typename... Values>\n'+fun('u32* EmitUpdate(')+'\n'
for sig in ['bool CheckRegs(', 'u32 ApplyModifier(', 'u32* EmitPsRegs(', 's32 PsShader(', 's32 VsShader(']: code+=fun(sig)+'\n'
a=s.index('alignas(256) constexpr u32 EmbeddedPs0[]');b=s.index('// clang-format on',a);code+=s[a:b]
for name in names: code+=fun('s32 PS4_SYSV_ABI '+name+'(')+'\n'
code+='''}
int main(){setenv("BB_NR_PM4_COLD_TRACE","1",1);using namespace BbGnm;
 std::array<u32,12> regs{}; regs[0]=0x12345;
 auto check=[&](auto fn,unsigned footprint,bool valid=true){
  std::array<u32,48> out;out.fill(0xa5a5a5a5);
  const auto seq=BbPm4Cold::Records().sequence;
  int rc=fn(out.data()+1);
  assert(BbPm4Cold::Records().sequence==seq+1);
  auto &r=BbPm4Cold::Records().ring[seq%256];
  assert(r.footprint==(valid?footprint:0));
  assert(out.front()==0xa5a5a5a5&&out[1+footprint]==0xa5a5a5a5);
  if(valid){assert(rc==0);assert(BbPm4Cold::Validate({out.data()+1,footprint}).valid);
   assert(r.snapshot_dw==footprint);for(unsigned i=0;i<footprint;i++)assert(r.head[i]==out[i+1]);
  }else {assert(rc!=0);for(auto w:out)assert(w==0xa5a5a5a5);}
 };
'''
for name in names:
 f=25 if 'Cs' in name else 29 if 'Vs' in name else 40
 extra=(',0,0' if 'Embedded' in name else ',regs.data(),0' if 'Vs' in name or 'Modifier' in name else ',regs.data()')
 code+=f'check([&](u32*p){{return {name}(p,{f}{extra});}},{f});\n'
 if 'Embedded' not in name:
  code+=f'check([&](u32*p){{return {name}(p,{f-1}{extra});}},{f},false);\n'
 if 'Ps' in name and 'Embedded' not in name:
  code+=f'check([&](u32*p){{return {name}(p,40,nullptr);}},{f});\n'
code+='puts("PASS actual PS/VS/CS/embedded writers: framing, footprints, canaries, failed reservation, null PS, single CS record");}\n'
with tempfile.TemporaryDirectory(prefix="bb-gnm-writers-") as temporary:
 out=args.output or Path(temporary)
 out.mkdir(parents=True,exist_ok=True)
 src=out/'writers.cpp';src.write_text(code)
 exe=out/'writers'
 subprocess.run(['clang++','-std=c++20','-O1','-g','-UNDEBUG',
  '-I'+str(root/'gpu'),'-I'+str(root/'gpu/shim'),str(src),'-o',str(exe)],check=True)
 result=subprocess.run([str(exe)],capture_output=True,text=True)
 (out/'writers.log').write_text(result.stdout+result.stderr)
 print(result.stdout+result.stderr,end='')
 assert result.returncode==0,result.returncode
