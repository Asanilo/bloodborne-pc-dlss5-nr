"""CPU filesystem wake regressions; events are never GPU completion proofs."""
from pathlib import Path
import subprocess
import tempfile
import unittest

class DirectoryWakeTests(unittest.TestCase):
    def test_atomic_replacement_before_and_during_wait(self):
        root=Path(__file__).resolve().parents[2]
        code=r'''
#include "gpu/upscale/dlssg_directory_wake.h"
#include <cassert>
#include <filesystem>
#include <fstream>
#include <sys/wait.h>
static void publish(const std::string& dir, const char* name) {
    { std::ofstream f(dir+"/temp"); f << "metadata"; }
    assert(rename((dir+"/temp").c_str(),(dir+"/"+name).c_str())==0);
}
int main() {
    char temp[]="/tmp/dlssg-notify-test-XXXXXX";assert(mkdtemp(temp));std::string dir=temp;
    DlssgGpu::DirectoryWake wake;assert(wake.Open(dir));
    publish(dir,"response.json");assert(wake.Pause(0)); // Rename before wait cannot be lost.
    assert(!wake.Pause(0)); // Notification drained, no busy wake loop.
    auto child=fork();assert(child>=0);
    if(!child){usleep(15000);publish(dir,"response.json");_exit(0);}
    assert(wake.Pause(1000));int status=0;assert(waitpid(child,&status,0)==child&&status==0);
    publish(dir,"unrelated.json");assert(wake.Pause(0));
    DlssgGpu::DirectoryWake fallback;assert(!fallback.Open(dir+"/missing"));assert(!fallback.Pause(0));
    std::filesystem::remove_all(dir);
}
'''
        with tempfile.TemporaryDirectory() as temp:
            exe=Path(temp)/'wake'
            subprocess.run(['c++','-std=c++20','-Wall','-Wextra','-Werror','-I',str(root),'-x','c++','-','-o',str(exe)],input=code,text=True,check=True)
            subprocess.run([exe],check=True,timeout=5)

if __name__=='__main__':unittest.main()
