"""CPU ownership/concurrency fixture; does not assert Vulkan completion."""
from pathlib import Path
import subprocess
import tempfile
import unittest


class SerialWorkerTests(unittest.TestCase):
    def test_move_only_work_serial_order_and_teardown_drain(self):
        root = Path(__file__).resolve().parents[2]
        code = r'''
#include "gpu/upscale/dlssg_serial_worker.h"
#include <cassert>
#include <atomic>
#include <memory>
#include <vector>
using namespace Vulkan;
int main() {
  std::promise<void> started, release;
  auto ready = started.get_future();
  auto blocked = release.get_future();
  std::atomic<unsigned> completed{0};
  std::vector<unsigned> order;
  {
    DlssgSerialWorker worker;
    worker.Submit([owned=std::make_unique<unsigned>(7), &started,
                   wait=std::move(blocked), &order, &completed]() mutable {
      started.set_value(); wait.get(); order.push_back(*owned); ++completed;
    });
    ready.get();
    assert(completed == 0); // Submission did not block its caller.
    worker.Submit([&] { assert(completed == 1); order.push_back(8); ++completed; });
    bool rejected=false;
    try { worker.Submit([]{}); } catch(const std::runtime_error&) { rejected=true; }
    assert(rejected); // Bounded admission, never unbounded game-frame buffering.
    release.set_value();
  } // Both owned jobs drained before backing storage disappears.
  assert(completed == 2 && order == std::vector<unsigned>({7,8}));
}
'''
        with tempfile.TemporaryDirectory() as temp:
            exe = Path(temp) / 'worker'
            subprocess.run(['c++', '-std=c++20', '-pthread', '-Wall', '-Wextra', '-Werror',
                            '-I', str(root), '-x', 'c++', '-', '-o', str(exe)],
                           input=code, text=True, check=True)
            subprocess.run([exe], check=True, timeout=5)


if __name__ == '__main__':
    unittest.main()
