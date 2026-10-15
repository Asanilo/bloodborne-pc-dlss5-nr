#!/usr/bin/env python3
"""Compile and validate precisely the checked-in persistent history shader."""
import hashlib,json,pathlib,struct,subprocess,tempfile
root=pathlib.Path(__file__).resolve().parents[2]
src=root/'tools/persistent_history/history.comp'
out=root/'gpu/shadps4/video_core/renderer_vulkan/native_persistent_history_spv.h'
with tempfile.TemporaryDirectory() as tmp:
    binary=pathlib.Path(tmp)/'history.spv'
    command=['glslangValidator','-V','--target-env','vulkan1.1',str(src),'-o',str(binary)]
    subprocess.run(command,check=True)
    subprocess.run(['spirv-val','--target-env','vulkan1.1',str(binary)],check=True)
    data=binary.read_bytes();words=struct.unpack('<%dI'%(len(data)//4),data)
    out.write_text('#pragma once\n#include <cstdint>\nnamespace Vulkan {\ninline constexpr uint32_t NativePersistentHistorySpv[] = {\n'+''.join('  '+','.join('0x%08xu'%v for v in words[i:i+8])+',\n' for i in range(0,len(words),8))+'};\n}\n')
    (src.parent/'provenance.json').write_text(json.dumps({'source':'tools/persistent_history/history.comp','source_sha256':hashlib.sha256(src.read_bytes()).hexdigest(),'spirv_sha256':hashlib.sha256(data).hexdigest(),'compiler':subprocess.check_output(['glslangValidator','--version'],text=True),'target':'vulkan1.1','validation':'spirv-val PASS'},indent=2)+'\n')
