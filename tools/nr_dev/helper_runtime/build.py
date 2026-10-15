#!/usr/bin/env python3
"""Build a distinct runtime-model PE; never overwrite the validated f527 Helper."""
import hashlib,json,pathlib,subprocess
HERE=pathlib.Path(__file__).resolve().parent
SOURCE=HERE.parents[2]
BASE=pathlib.Path('/home/arinp/Code/nrbridge-dev/inputs/helper-f527')
def sha(p):return hashlib.sha256(p.read_bytes()).hexdigest()
def main():
    original=json.loads((BASE/'manifest.json').read_text())
    for p,h in original['dependency_pins'].items():
        if sha(pathlib.Path(p))!=h:raise ValueError('dependency drift '+p)
    for item in original['files']:
        if sha(pathlib.Path(item['export']))!=item['sha256']:raise ValueError('frozen closure drift '+item['export'])
    compiler=pathlib.Path(original['compiler']['path'])
    if sha(compiler)!=original['compiler']['sha256']:raise ValueError('compiler drift')
    proto=SOURCE/'gpu/shadps4/video_core/renderer_vulkan/native_runtime_model.h'
    inputs={str(p):sha(p) for p in (HERE/'wine_runtime_model.cpp',HERE/'runtime_frame.h',proto)}
    digest=hashlib.sha256(json.dumps(inputs,sort_keys=True).encode()).hexdigest()
    output=pathlib.Path('/home/arinp/Code/nrbridge-dev/build/helper-runtime-model')/digest[:16]
    output.mkdir(parents=True,exist_ok=True)
    binary=output/'wine_runtime_model.exe'
    # Run the host-only protocol boundary checks used by both actual peers.
    test=output/'test_protocol'
    host_include=[x for x in original['commands'][0] if x.startswith('-I')]
    subprocess.run(['/usr/bin/c++','-std=c++17','-O2','-Wall','-Wextra','-Werror',*host_include,'-I'+str(proto.parent),str(HERE/'test_protocol.cpp'),'-o',str(test)],check=True)
    subprocess.run([str(test)],check=True)
    oldcmd=original['commands'][-1]
    include=[x for x in oldcmd if x.startswith('-I')]
    command=[str(compiler),'-std=c++17','-static','-O1','-Wall','-Wextra','-Wno-missing-field-initializers','-Wno-unused-function',*include,'-I'+str(proto.parent),str(HERE/'wine_runtime_model.cpp'),'-o',str(binary)]
    if binary.exists():
        manifest=json.loads((output/'build.json').read_text())
        if manifest['inputs']!=inputs or manifest['artifact_sha256']!=sha(binary):raise ValueError('existing immutable artifact drift')
        print(binary);return
    result=subprocess.run(command,stdout=subprocess.PIPE,stderr=subprocess.STDOUT,timeout=180)
    (output/'compile.log').write_bytes(result.stdout)
    if result.returncode:raise RuntimeError(result.stdout.decode(errors='replace'))
    manifest={'status':'BUILD_ONLY_PASS','base':'f527d8f2f12d474a66bbb4c1e231d6cd9b673476','base_manifest_sha256':sha(BASE/'manifest.json'),'inputs':inputs,'compiler':original['compiler'],'command':command,'dependency_pins':original['dependency_pins'],'artifact':str(binary),'artifact_sha256':sha(binary),'GPU':'NOT_RUN','Wine':'NOT_RUN','new_protocol':'nr-runtime-model-v1 + nr-runtime-job-model-v1; original input/complete tuple unchanged'}
    (output/'build.json').write_text(json.dumps(manifest,indent=2)+'\n')
    print(binary)
if __name__=='__main__':main()
