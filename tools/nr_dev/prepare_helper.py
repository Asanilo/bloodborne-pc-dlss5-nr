#!/usr/bin/env python3
"""Export only f527 Helper compile closure; preserve effective wire specification bytes."""
import hashlib,json,re,subprocess,sys
from pathlib import Path
ROOT=Path('/home/arinp/Code/nrbridge-dev')
OLD=Path('/home/arinp/Code/nrbridge-design/game-poc-20261011/opti-async-pre-sr-play-worktree/poc/game_nr/async-pre-sr-play')
def sha(p):return hashlib.sha256(p.read_bytes()).hexdigest()
def rebuild():
 source=ROOT/'inputs/helper-f527';out=ROOT/'build/helper';info=json.loads((source/'manifest.json').read_text())
 for path,expected in info.get('dependency_pins',{}).items():
  if sha(Path(path))!=expected:raise ValueError('dependency drift '+path)
 for item in info['files']:
  if sha(Path(item['export']))!=item['sha256']:raise ValueError('exported source drift '+item['export'])
 if sha(source/'effective-generated-spec.json')!=info['effective_spec_sha256']:raise ValueError('effective spec drift')
 if sha(Path(info['compiler']['path']))!=info['compiler']['sha256']:raise ValueError('compiler drift')
 for i,command in enumerate(info['commands']):
  result=subprocess.run(command,stdout=subprocess.PIPE,stderr=subprocess.STDOUT,timeout=180);(out/f'rebuild-step-{i}.log').write_bytes(result.stdout)
  if result.returncode:raise RuntimeError(result.stdout.decode(errors='replace'))
 print('HELPER_REBUILD_ONLY_PASS',sha(out/'wine_async_presr_play.exe'))

def main():
 if '--rebuild' in sys.argv:return rebuild()
 info=json.loads((OLD/'out/build.json').read_text());pins=info['readonly_dependency_pins']
 source=ROOT/'inputs/helper-f527';out=ROOT/'build/helper';source.mkdir(parents=True,exist_ok=True);out.mkdir(parents=True,exist_ok=True)
 mapping={}; manifest=[]
 def export(p,desired=None):
  p=p.resolve()
  if p in mapping:return mapping[p]
  dest=source/(desired or ('closure/'+hashlib.sha256(str(p).encode()).hexdigest()[:12]+'-'+p.name));mapping[p]=dest;dest.parent.mkdir(parents=True,exist_ok=True)
  text=p.read_text()
  def inc(m):
   target=Path(m[1]);target=target if target.is_absolute() else p.parent/target
   if not target.is_file():
    choices=[Path(pins['ngx_sdk_root'])/m[1],Path(pins['sdk_root'])/m[1]]
    target=next((q for q in choices if q.is_file()),target)
   if not target.is_file():raise ValueError('missing include '+str(target))
   q=export(target)
   return '#include "'+str(q)+'"'
  text=re.sub(r'^#include\s+"([^"]+)"',inc,text,flags=re.M)
  dest.write_text(text);manifest.append({'original':str(p),'original_sha256':sha(p),'export':str(dest),'sha256':sha(dest),'change':'quoted include relocation only'})
  return dest
 cpp=export(OLD/'wine_async_presr_play.cpp','wine_async_presr_play.cpp');checker=export(OLD/'wire_check.cpp','wire_check.cpp')
 spec=OLD/'out/effective-generated-spec.json';assert sha(spec)=='671c7644c69de5f681b099fb4c3d402ba36f1e4538e65cd5538441e2bd47643b'
 (source/spec.name).write_bytes(spec.read_bytes())
 cc=Path(pins['compiler']['path']);assert sha(cc)==pins['compiler']['sha256']
 jh=Path(pins['json_header']['path']);assert sha(jh)==pins['json_header']['sha256']
 compile_steps=[['/usr/bin/c++','-std=c++17','-O2','-Wall','-Wextra','-Werror','-I'+str(jh.parent),str(checker),'-o',str(out/'wire_check')],[str(out/'wire_check')],[str(cc),'-std=c++17','-static','-O1','-Wall','-Wextra','-Wno-missing-field-initializers','-Wno-unused-function','-I'+pins['sdk_root'],'-I'+pins['ngx_sdk_root'],'-I'+str(jh.parent),str(cpp),'-o',str(out/'wine_async_presr_play.exe')]]
 for i,command in enumerate(compile_steps):
  result=subprocess.run(command,stdout=subprocess.PIPE,stderr=subprocess.STDOUT,timeout=180);(out/f'step-{i}.log').write_bytes(result.stdout)
  if result.returncode:raise RuntimeError(result.stdout.decode(errors='replace'))
 (source/'manifest.json').write_text(json.dumps({'source_commit':'f527d8f2f12d474a66bbb4c1e231d6cd9b673476','effective_spec_sha256':sha(spec),'files':manifest,'compiler':pins['compiler'],'dependency_pins':{**pins['SDK_headers'],**pins['ngx_sdk_headers'],pins['json_header']['path']:pins['json_header']['sha256']},'current_runtime_pins':info['current_runtime_pins'],'commands':compile_steps,'artifact_sha256':sha(out/'wine_async_presr_play.exe'),'wire_transport_logic_changed':False},indent=2)+'\n')
 print('HELPER_BUILD_ONLY_PASS',sha(out/'wine_async_presr_play.exe'))
if __name__=='__main__':main()
