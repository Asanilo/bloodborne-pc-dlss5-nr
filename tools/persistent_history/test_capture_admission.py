"""CPU-only admission and exception-boundary regression; no Vulkan device."""
import pathlib
import subprocess
import tempfile
import unittest
ROOT=pathlib.Path(__file__).resolve().parents[2]
class CaptureAdmission(unittest.TestCase):
    def test_actual_dev_run_contract_accepts_configured_daily_root_only(self):
        program = r"""
#include "native_history_capture_admission.h"
#include <cassert>
int main(int argc,char** argv){
 const std::string root=argv[1],session="nr-quality-root-case",run=root+"/runs/"+session;
 std::filesystem::create_directories(run);
 setenv("BB_NR_DAILY","1",1);setenv("NRBRIDGE_HOME",root.c_str(),1);
 assert(BbNativeAsyncPreSr::ValidDevRun(run,session));
 setenv("BB_NR_NATIVE_ASYNC_RUN_DIR",run.c_str(),1);setenv("BB_NR_NATIVE_ASYNC_SESSION",session.c_str(),1);
 auto request=Vulkan::HistoryCapture::ValidateRequest(1,3,false,4);
 assert(request.ui_request&&request.limit==3&&request.session==session);
 std::filesystem::create_directory(request.directory);
 bool collision=false;try{Vulkan::HistoryCapture::ValidateRequest(1,3,false,4);}
 catch(const Vulkan::HistoryCapture::DiagnosticAdmissionRejected&){collision=true;}assert(collision);
 std::filesystem::remove(request.directory);
 std::filesystem::create_symlink(root+"/missing",request.directory);
 bool linked=false;try{Vulkan::HistoryCapture::ValidateRequest(1,3,false,4);}
 catch(const Vulkan::HistoryCapture::DiagnosticAdmissionRejected&){linked=true;}assert(linked);
 bool missingOptIn=false;try{Vulkan::HistoryCapture::ValidateRequest(0,3,false,4);}
 catch(const Vulkan::HistoryCapture::DiagnosticAdmissionRejected&){missingOptIn=true;}assert(missingOptIn);
 assert(!BbNativeAsyncPreSr::ValidDevRun(run,"other"));
 assert(!BbNativeAsyncPreSr::ValidDevRun(run+"/../"+session,session));
 std::filesystem::create_directory_symlink(root,root+"-alias");
 setenv("NRBRIDGE_HOME",(root+"-alias").c_str(),1);
 assert(!BbNativeAsyncPreSr::ValidDevRun(root+"-alias/runs/"+session,session));
 setenv("NRBRIDGE_HOME",root.c_str(),1);setenv("BB_NR_DAILY","0",1);
 assert(!BbNativeAsyncPreSr::ValidDevRun(run,session));
}
"""
        with tempfile.TemporaryDirectory() as temporary:
            directory = pathlib.Path(temporary)
            source = directory / 'paths.cpp'; source.write_text(program)
            binary = directory / 'paths'
            subprocess.run(['c++', '-std=c++20', '-I', str(ROOT / 'gpu/render'),
                            str(source), '-o', str(binary)], check=True)
            subprocess.run([str(binary), str(directory / 'private')], check=True)

    def test_typed_admission_rejection_consumes_request_and_continues_source(self):
        production = (ROOT / 'gpu/render/native_persistent_history.cpp').read_text()
        begin = production.index(' if(!p.diagnostic&&(p.pending_controls.capture_seq>p.handled_capture||p.env_capture_pending))')
        end = production.index(' // Job epoch changes', begin)
        body = production[begin:end]
        program = r"""
#include "native_history_capture_admission.h"
#include <cassert>
#include <cstdio>
#include <memory>
#include <new>
using namespace Vulkan;
struct Instance {};
int attempts=0,kind=1,downstream_source_continuations=0;
struct NativePersistentHistoryDiagnostic {
 NativePersistentHistoryDiagnostic(Instance&,uint64_t,unsigned){
  ++attempts;
  if(kind==1)throw HistoryCapture::DiagnosticAdmissionRejected("CPU rejected");
  if(kind==2)throw std::runtime_error("allocation or record failure");
  if(kind==3)throw std::bad_alloc();
 }
 std::string Directory(){return "CPU-only-fake";}
};
struct State {
 struct Controls {uint64_t capture_seq=0,revision=1;} pending_controls,applied_controls;
 uint64_t handled_capture=0;bool env_capture_pending=false;Instance instance_value;Instance* instance=&instance_value;
 struct Capture {uint64_t seq=0,revision=0;bool busy=false,complete=false,failed=false,request_rejected=false;std::string directory,reason;} capture_status;
 std::unique_ptr<NativePersistentHistoryDiagnostic> diagnostic;
};
struct Frame {uint64_t source=100;};
void source(State& p,const Frame& frame){
""" + body + r"""
 ++downstream_source_continuations; // CPU reachability marker, never GPU proof.
}
int main(){
 Frame frame;State rejected;rejected.pending_controls.capture_seq=1;
 source(rejected,frame);
 assert(attempts==1&&downstream_source_continuations==1);
 assert(rejected.handled_capture==1&&!rejected.env_capture_pending&&!rejected.diagnostic);
 assert(rejected.capture_status.request_rejected&&!rejected.capture_status.complete&&!rejected.capture_status.busy);
 source(rejected,frame);assert(attempts==1&&downstream_source_continuations==2);
 State environmental;environmental.env_capture_pending=true;
 source(environmental,frame);assert(!environmental.env_capture_pending&&environmental.capture_status.request_rejected);
 const int before=downstream_source_continuations;
 for(int failure: {2,3}){
  kind=failure;State unknown;unknown.pending_controls.capture_seq=1;bool propagated=false;
  try{source(unknown,frame);}catch(const HistoryCapture::DiagnosticAdmissionRejected&){assert(false);}
  catch(const std::exception&){propagated=true;}
  assert(propagated&&unknown.handled_capture==0&&!unknown.capture_status.request_rejected);
 }
 assert(downstream_source_continuations==before);
}
"""
        with tempfile.TemporaryDirectory() as temporary:
            source = pathlib.Path(temporary) / 'admission.cpp'; source.write_text(program)
            binary = pathlib.Path(temporary) / 'admission'
            subprocess.run(['c++', '-std=c++20', '-I', str(ROOT / 'gpu/render'),
                            str(source), '-o', str(binary)], check=True)
            subprocess.run([str(binary)], check=True)


if __name__=='__main__':unittest.main()
