#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Execute the actual integration installation block and producer without guest/GPU code."""
from pathlib import Path
import argparse
import runpy
import subprocess
import tempfile

root = Path(__file__).resolve().parents[2]
parser = argparse.ArgumentParser()
parser.add_argument('--output', type=Path)
args = parser.parse_args()
text = (root / 'gpu/shim/bbport_guest_hooks.cpp').read_text()
function = runpy.run_path(str(root / 'gpu/cp/tests/test_cp_ownership.py'))['function']
start = text.index('        constexpr bool NativeMouseHookValidated')
end = text.index('        if (const char* env = std::getenv("BB_RE_ADVANCE")', start)
block = text[start:end]
source = r'''
#include <array>
#include <atomic>
#include <cassert>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
using u64=uint64_t;
std::array<unsigned char,16> site{};
unsigned installs=0;
std::atomic<bool> look_installed{false};
std::atomic<u64> look_delta{0};
std::atomic<long long> look_since_ms{0};
bool InstallLookHook(){++installs;site.fill(0xcc);return true;}
void InstallBlock(){
''' + block + '\n}\n' + function(text, 'bool CameraLookAvailable(') + '\n' + function(text, 'bool CameraLookAdd(') + r'''
int main(){
 site.fill(0x90); const auto before=site;
 for(const char* value : std::array<const char*,6>{nullptr,"","0","1","01","true"}) {
  if(value)setenv("BB_NATIVE_MOUSE",value,1);else unsetenv("BB_NATIVE_MOUSE");
  InstallBlock();
  assert(installs==0 && site==before && !CameraLookAvailable());
  assert(!CameraLookAdd(0.1f,0.2f)); // actual router returns the sample to the stick
  assert(look_delta==0 && look_since_ms==0); // no hidden native enqueue
 }
 std::puts("PASS: actual install block never patches the site; native producer declines without enqueuing for every environment setting");
}
'''
with tempfile.TemporaryDirectory(prefix='bb-mouse-gate-') as temporary:
    out = args.output or Path(temporary)
    out.mkdir(parents=True, exist_ok=True)
    path = out / 'gate.cpp'
    path.write_text(source)
    executable = out / 'gate'
    subprocess.run(['clang++', '-std=c++23', '-O1', '-g', '-UNDEBUG', '-Wall', '-Wextra',
                    '-Werror', str(path), '-o', str(executable)], check=True)
    result = subprocess.run([str(executable)], capture_output=True, text=True)
    (out / 'gate.log').write_text(result.stdout + result.stderr)
    print(result.stdout + result.stderr, end='')
    assert result.returncode == 0, result.returncode
