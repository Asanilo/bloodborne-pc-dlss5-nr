#!/usr/bin/env python3
"""CPU checks of production capture gates; no Vulkan submission or GPU claim."""
import pathlib
import shutil
import subprocess
import tempfile
import unittest

ROOT = pathlib.Path(__file__).resolve().parents[2]
SOURCE = ROOT / 'gpu/render/native_persistent_history_diagnostic.cpp'
TEXT = SOURCE.read_text()


class CandidateCapture(unittest.TestCase):
    def test_cpu_actual_source_gates(self):
        # Compile the actual gate and expiry bodies with minimal CPU-only types.
        expire = TEXT.split('void NativePersistentHistoryDiagnostic::Impl::ExpireCandidateWait() noexcept{', 1)[1].split('\nbool NativePersistentHistoryDiagnostic::Enabled()', 1)[0]
        record = TEXT.split('bool NativePersistentHistoryDiagnostic::Record(Scheduler& scheduler,const Snapshot& snapshot){', 1)[1].split(' p.scheduler=&scheduler;auto& frame=', 1)[0]
        program = r'''
#include <atomic>
#include <chrono>
#include <cstdio>
#include <array>
#include <cassert>
struct Scheduler {};
struct Snapshot {struct {unsigned long long source=0;} current; bool state=true,guard=true,host_history=true; std::array<bool,8> images{true,true,true,true,true,true,true,true}; unsigned candidate_count=1;};
struct Impl {
 bool ui_request=true,sealed=false; std::atomic<bool> candidate_recorded{false},unknown{false};
 std::atomic<unsigned> recorded{0}; unsigned limit=3,after_candidate=1; unsigned long long last_source=0,first=0,start=0,capture_seq=1;
 Scheduler* scheduler=nullptr; struct {struct {unsigned long long source=0;} current;} candidate;
 struct {Snapshot snapshot;} frames[4];
 std::chrono::steady_clock::time_point candidate_deadline=std::chrono::steady_clock::now()+std::chrono::seconds(10);
 void ExpireCandidateWait() noexcept;
};
struct Harness {Impl* impl; void HoldUnknown(){impl->unknown=true;} bool Record(Scheduler&,const Snapshot&);};
void Impl::ExpireCandidateWait() noexcept{
''' + expire + '\nbool Harness::Record(Scheduler& scheduler,const Snapshot& snapshot){' + record + r'''
 return true;
}
int main(){
 Scheduler scheduler,other; Impl p; Harness h{&p}; Snapshot s;s.current.source=100;
 assert(!h.Record(scheduler,s));assert(p.first==0); // UI cannot capture old history alone.
 p.candidate_recorded=true;p.candidate.current.source=102;
 assert(!h.Record(scheduler,s));assert(p.first==0);
 s.current.source=102;assert(h.Record(scheduler,s));assert(p.first==102);
 p.recorded=1;p.frames[0].snapshot=s;s.current.source=104;
 assert(h.Record(scheduler,s)); // Existing bounded gap reporting is retained.
 s.current.source=105;assert(!h.Record(scheduler,s));assert(p.sealed);
 Impl late; Harness wait{&late};late.candidate_deadline=std::chrono::steady_clock::now()-std::chrono::seconds(1);
 assert(!wait.Record(scheduler,s));assert(late.sealed);assert(late.recorded==0);
 Impl started;started.candidate_recorded=true;started.candidate_deadline=late.candidate_deadline;started.ExpireCandidateWait();assert(!started.sealed);
 Impl unknown;unknown.unknown=true;Harness u{&unknown};assert(!u.Record(scheduler,s));assert(unknown.first==0);
 Impl mismatch;mismatch.scheduler=&other;Harness m{&mismatch};assert(!m.Record(scheduler,s));assert(mismatch.unknown);
}
'''
        compiler = shutil.which('g++')
        self.assertIsNotNone(compiler)
        with tempfile.TemporaryDirectory() as tmp:
            source = pathlib.Path(tmp) / 'gates.cpp'
            source.write_text(program)
            binary = pathlib.Path(tmp) / 'gates'
            subprocess.run([compiler, '-std=c++20', str(source), '-o', str(binary)], check=True)
            subprocess.run([str(binary)], check=True)

    def test_budget_and_ui_candidate_allocation(self):
        frame = 1280 * 720 * (8+8+8+16+4+8+8+16) + 36
        candidate = 1280 * 720 * (16+16+4)
        self.assertLess(frame * 4 + candidate + 5*16384, 320*1024*1024)
        self.assertEqual(candidate, 33177600)
        self.assertIn('constexpr unsigned MaxFrames=4;', TEXT)
        self.assertNotIn('if(!ui_request)for(unsigned j=0;j<3;j++)candidate_staging', TEXT)
        self.assertIn('return FrameBytes*impl->limit+CandidateBytes;', TEXT)

    def test_candidate_proof_and_completion_remain_required(self):
        candidate_record = TEXT.split('bool NativePersistentHistoryDiagnostic::RecordCandidate(', 1)[1].split('bool NativePersistentHistoryDiagnostic::Impl::Ready()', 1)[0]
        self.assertNotIn('p.ui_request||', candidate_record)
        self.assertIn('p.sealed||p.unknown.load', candidate_record)
        self.assertIn('cmd.copyImageToBuffer(snapshot.images[j]', candidate_record)
        self.assertIn('scheduler.DeferPriorityOperation([proof]', candidate_record)
        succeeded = TEXT.split('bool NativePersistentHistoryDiagnostic::Succeeded()', 1)[1].split('std::string NativePersistentHistoryDiagnostic::Directory()', 1)[0]
        self.assertIn('!p.candidate_recorded.load(std::memory_order_acquire)', succeeded)
        observe = TEXT.split('void NativePersistentHistoryDiagnostic::ObserveCompletion(', 1)[1].split('uint64_t NativePersistentHistoryDiagnostic::StagingBytes()', 1)[0]
        self.assertLess(observe.index('p.ExpireCandidateWait()'), observe.index('!p.scheduler'))
        self.assertIn('not SDK tensor or history composite', TEXT)
        self.assertIn('does not cover full 16-frame lifetime', TEXT)


if __name__ == '__main__':
    unittest.main()
