"""Compile the production admission contract; no Vulkan instance/device or GPU work."""
from pathlib import Path
import subprocess
import tempfile
import unittest


class InputContractTests(unittest.TestCase):
    def test_unbound_first_frame_and_real_source_identity(self):
        root = Path(__file__).resolve().parents[2]
        code = r'''
#include "gpu/upscale/frame_generation_contract.h"
using namespace Vulkan::FgContract;
static_assert(!CanInterpolate(true, true, 0u, 0u, true)); // original crash
static_assert(!CanInterpolate(true, true, 1u, 0u, true));
static_assert(!CanInterpolate(true, true, 0u, 1u, true));
static_assert(!CanInterpolate(true, true, 2u, 1u, true)); // auxiliary LDR
static_assert(!CanInterpolate(true, false, 1u, 1u, true)); // missing scene
static_assert(!CanInterpolate(false, true, 1u, 1u, true)); // live FG off
static_assert(!CanInterpolate(true, true, 1u, 1u, false));
static_assert(CanInterpolate(true, true, 1u, 1u, true));
static_assert(!SameRealSource(0, 0));
static_assert(!SameRealSource(1816, 1817)); // next real source cannot own old FG
static_assert(!SameRealSource(1816, 0)); // missing NR Present ledger
static_assert(SameRealSource(1816, 1816));
int main() {}
'''
        with tempfile.TemporaryDirectory() as directory:
            executable = str(Path(directory) / 'contract')
            subprocess.run(['c++', '-std=c++20', '-Wall', '-Wextra', '-Werror',
                            '-I', str(root), '-x', 'c++', '-', '-o', executable],
                           input=code, text=True, check=True)
            subprocess.run([executable], check=True)


if __name__ == '__main__':
    unittest.main()
