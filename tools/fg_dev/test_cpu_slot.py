"""CPU payload visibility and rejection checks; no GPU calls or Wine launch."""
from pathlib import Path
import subprocess
import tempfile
import unittest
import cpu_slot


class CpuSlotTests(unittest.TestCase):
    def test_two_process_views_and_cpp_layout(self):
        root=Path(__file__).resolve().parents[2]
        code=r'''
#include "gpu/upscale/dlssg_cpu_slot.h"
#include <cassert>
#include <fstream>
#include <iostream>
int main(int argc,char** argv){assert(argc==2);DlssgCpu::MappedSlot s;s.Open(argv[1]);
assert(*s.At(DlssgCpu::ColorOffset,1)==17);
assert(*s.At(DlssgCpu::DepthOffset,1)==29);
assert(*s.At(DlssgCpu::MotionOffset,1)==43);
*s.At(DlssgCpu::OutputOffset,1)=97;s.Publish();
bool rejected=false;try{s.At(DlssgCpu::SlotBytes,1);}catch(...){rejected=true;}assert(rejected);
std::cout<<DlssgCpu::SlotBytes;}
'''
        with tempfile.TemporaryDirectory() as temp:
            directory=Path(temp);exe=directory/'view'
            subprocess.run(['c++','-std=c++17','-Wall','-Wextra','-Werror','-I',str(root),
                            '-x','c++','-','-o',str(exe)],input=code,text=True,check=True)
            with cpu_slot.mapping(directory,create=True) as view:
                for role,value in [('color',17),('depth',29),('motion',43)]:
                    view[cpu_slot.OFFSETS[role][0]]=value
                size=subprocess.check_output([exe,directory/'slot.bin'],text=True)
                self.assertEqual(int(size),cpu_slot.SLOT_BYTES)
                self.assertEqual(view[cpu_slot.OFFSETS['output'][0]],97)
                with self.assertRaises(FileExistsError):
                    with cpu_slot.mapping(directory,create=True):pass

    def test_wrong_length_and_symlink_rejected(self):
        with tempfile.TemporaryDirectory() as temp:
            root=Path(temp);path=root/'slot.bin';path.write_bytes(b'bad');path.chmod(0o600)
            with self.assertRaisesRegex(ValueError,'length/mode'):
                with cpu_slot.mapping(root):pass
            path.unlink();(root/'target').write_bytes(b'cpu');path.symlink_to(root/'target')
            with self.assertRaises(OSError):
                with cpu_slot.mapping(root):pass


if __name__=='__main__':unittest.main()
