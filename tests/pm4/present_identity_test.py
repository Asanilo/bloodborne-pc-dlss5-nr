#!/usr/bin/env python3
"""CPU routing of the actual Presenter::Present body; no Vulkan completion claim."""
from pathlib import Path
import os
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]


class PresentIdentity(unittest.TestCase):
    def test_real_reused_blank_and_interpolated_calls(self):
        text = (ROOT / 'gpu/render/presenter.cpp').read_text()
        start = text.index('void Presenter::Present(Frame*')
        end = text.index('\nbool Presenter::PresentImage(', start)
        body = text[start:end]
        prefix = r'''
#include <atomic>
#include <cassert>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <algorithm>
#include <thread>
#include <vector>
using u64=std::uint64_t;
struct Frame { bool has_interp=false; int interp_image=0; };
struct Presenter {
    struct Call { int source; bool last, game, reused; };
    Frame blank_frame;
    unsigned released=0;
    std::vector<Call> calls;
    void ReleaseFrame(Frame*) { ++released; }
    bool PresentImage(Frame*,int source,bool last,bool game,bool reused) {
        calls.push_back({source,last,game,reused});
        return false; // no swapchain: no interpolation pacing sleep
    }
    void Present(Frame*,bool,bool);
};
'''
        main = r'''
int main(int argc,char**) {
    Presenter p;
    Frame frame;
    if(argc>1) {
        frame.has_interp=true; frame.interp_image=99;
        p.Present(&frame,false,true);
        assert(p.calls.size()==1 && p.calls[0].source==99 && !p.calls[0].game);
        assert(p.calls[0].last && !p.calls[0].reused && p.released==1);
        return 0;
    }
    p.Present(&frame,false,true);
    assert(p.calls.size()==1 && p.calls[0].game && !p.calls[0].reused && p.calls[0].last);
    assert(p.calls[0].source==0 && p.released==1);
    p.calls.clear();
    p.Present(&frame,true,true);
    assert(p.calls.size()==1 && p.calls[0].game && p.calls[0].reused && p.released==1);
    p.calls.clear();
    p.Present(&p.blank_frame,false,true);
    assert(p.calls.size()==1 && !p.calls[0].game);
    p.calls.clear();
    p.Present(&frame,false,false);
    assert(p.calls.size()==1 && !p.calls[0].game);
    p.calls.clear();
    frame.has_interp=true; frame.interp_image=99;
    p.Present(&frame,false,true);
    assert(p.calls.size()==2 && !frame.has_interp);
    assert(p.calls[0].source==99 && !p.calls[0].last && !p.calls[0].game);
    assert(p.calls[1].source==0 && p.calls[1].last && p.calls[1].game && !p.calls[1].reused);
}
'''
        variants = {
            'current': body,
            'blank_as_game': body.replace('is_game_frame && !blank', 'is_game_frame'),
            'reuse_as_new': body.replace(
                'PresentImage(frame, {}, true, is_game_frame && !blank, is_reusing_frame)',
                'PresentImage(frame, {}, true, is_game_frame && !blank, false)'),
            'interp_as_game': body.replace(
                'frame->interp_image, false, false, is_reusing_frame',
                'frame->interp_image, false, true, is_reusing_frame'),
        }
        env = dict(os.environ)
        env.pop('BB_FRAME_GEN_ONLY', None)
        with tempfile.TemporaryDirectory(prefix='bb-present-routing-') as temp:
            for name, source in variants.items():
                self.assertTrue(name == 'current' or source != body)
                path = Path(temp) / name
                cpp = path.with_suffix('.cpp')
                cpp.write_text(prefix + source + main)
                subprocess.run(['clang++', '-std=c++20', '-O1', '-g', '-Wall', '-Wextra',
                                '-Werror', str(cpp), '-o', str(path)], check=True)
                result = subprocess.run([str(path)], env=env, capture_output=True, timeout=5)
                self.assertEqual(result.returncode == 0, name == 'current', result.stderr)
                if name == 'current':
                    subprocess.run([str(path), 'only-interp'], env=env | {'BB_FRAME_GEN_ONLY': '1'},
                                   check=True, capture_output=True, timeout=5)


if __name__ == '__main__':
    unittest.main()
