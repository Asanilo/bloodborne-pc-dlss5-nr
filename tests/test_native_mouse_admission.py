"""Compile actual admission/capability bodies without the GPU, network or guest assets."""
from pathlib import Path
import os
import subprocess
import tempfile
import unittest

from paths import ROOT


def function(source, marker):
    start = source.index(marker)
    return source[start:source.index('\n}', start) + 2]


class MouseAdmission(unittest.TestCase):
    def compile_run(self, source):
        with tempfile.TemporaryDirectory(prefix='bbport-mouse-admission-') as folder:
            path = Path(folder)
            (path / 'test.cpp').write_text(source)
            subprocess.run([os.environ.get('CXX', 'c++'), '-std=c++17', '-Wall', '-Wextra',
                            '-Werror', str(path / 'test.cpp'), '-o', str(path / 'test')], check=True)
            subprocess.run([str(path / 'test')], check=True, timeout=5)

    def test_actual_gpu_admission(self):
        glue = (ROOT / 'gpu/shim/bbgpu.cpp').read_text()
        self.compile_run('''
#include <cstdlib>
#include <cstring>
#include <cstdio>
#include <cassert>
#include <initializer_list>
static int capability_result, capability_queries;
int runtime_net_native_mouse_compatible() { ++capability_queries; return capability_result; }
''' + function(glue, 'static bool MouseOverlapRefused()') + '''
int main() {
    for (const char* name : {"BB_NATIVE_MOUSE", "BB_ONLINE", "BB_BBSC", "BB_RE_CAMERA", "BB_RE_ENTRY"})
        unsetenv(name);
    setenv("BB_ONLINE", "1", 1);
    for (const char* request : {"", "0", "01", "1foo"}) {
        setenv("BB_NATIVE_MOUSE", request, 1);
        assert(!MouseOverlapRefused());
        assert(capability_queries == 0);
    }
    setenv("BB_NATIVE_MOUSE", "1", 1);
    for (const char* online : {"1", "1foo"}) {
        setenv("BB_ONLINE", online, 1);
        capability_result = 0;
        assert(MouseOverlapRefused());
        capability_result = 1;
        assert(!MouseOverlapRefused());
        for (const char* bbsc : {"1", "1foo"}) {
            setenv("BB_BBSC", bbsc, 1);
            assert(!MouseOverlapRefused());
        }
        unsetenv("BB_BBSC");
    }
    for (const char* online : {"", "0", "01"}) {
        setenv("BB_ONLINE", online, 1);
        capability_result = 0;
        assert(!MouseOverlapRefused());
    }
    unsetenv("BB_ONLINE");
    setenv("BB_RE_CAMERA", "1foo", 1);
    assert(MouseOverlapRefused());
    unsetenv("BB_RE_CAMERA");
    setenv("BB_RE_ENTRY", "0", 1);
    assert(MouseOverlapRefused());
    unsetenv("BB_RE_ENTRY");
    assert(!MouseOverlapRefused());
}
''')

    def test_both_actual_builtin_capabilities(self):
        for path, marker in [
            ('gpu/bbnet/bbnet_glue.cpp', 'extern "C" __attribute__((visibility("default"))) int bbnet_native_mouse_compatible()'),
            ('gpu/bbnet20/bbnet_glue.cpp', 'BBNET_EXPORT int bbnet_native_mouse_compatible()'),
        ]:
            with self.subTest(module=path):
                body = function((ROOT / path).read_text(), marker).replace('BBNET_EXPORT ', '')
                self.compile_run('''
#include <cstdlib>
#include <cassert>
#include <initializer_list>
''' + body + '''
int main() {
    unsetenv("BB_BBSC");
    assert(bbnet_native_mouse_compatible() == 1);
    for (const char* value : {"", "0", "01"}) {
        setenv("BB_BBSC", value, 1);
        assert(bbnet_native_mouse_compatible() == 1);
    }
    for (const char* value : {"1", "1foo"}) {
        setenv("BB_BBSC", value, 1);
        assert(bbnet_native_mouse_compatible() == 1);
    }
}
''')


if __name__ == '__main__':
    unittest.main()
