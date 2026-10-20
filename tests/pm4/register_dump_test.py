#!/usr/bin/env python3
"""Compile actual dump source against synthetic contexts; no game or signals."""
from pathlib import Path
import re
import shutil
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]


class RegisterDumpTest(unittest.TestCase):
    def test_actualsource_ucontext_pipe(self):
        source = (ROOT / 'src/probe.c').read_text()
        actual = source[source.index('static void write_hex('):source.index('static void thread_dump(')]
        dump = actual[actual.index('static void dump_frames('):]
        # New diagnostic output may not introduce unsafe formatting/allocation.
        for token in ('malloc(', 'calloc(', 'printf(', 'snprintf(', 'fwrite(', 'pthread_', 'bbgpu_'):
            self.assertNotIn(token, dump)
        self.assertEqual(dump.count('process_vm_readv('), 3)  # bounded pointer, stack, frame reads
        harness = r'''
#define _GNU_SOURCE
#include <stdint.h>
#include <unistd.h>
#include <signal.h>
#include <ucontext.h>
#include <sys/uio.h>
#include <string.h>
static unsigned char *image=(unsigned char *)0x1000;
static uintptr_t exe_base=0x2000;
/* Isolate the existing frame-read OS dependency: permit only the two explicitly
 * owned synthetic frames. Sandbox policy may deny real process_vm_readv. */
static uintptr_t test_frames[2][2];
static const uintptr_t guest_pointer=UINT64_C(0x800001000), guest_stack=UINT64_C(0x800002000);
static uint64_t pointer_words[8], stack_words[24];
static ssize_t synthetic_read(pid_t pid,const struct iovec *local,unsigned long lc,
                              const struct iovec *remote,unsigned long rc,unsigned long flags) {
    if(pid!=getpid() || lc!=1 || rc!=1 || flags || local->iov_len!=remote->iov_len)return -1;
    if(remote->iov_base==(void *)guest_pointer && remote->iov_len==sizeof(pointer_words)) {
        memcpy(local->iov_base,pointer_words,sizeof(pointer_words)); return sizeof(pointer_words);
    }
    const uintptr_t address=(uintptr_t)remote->iov_base;
    if(address>=guest_stack && address-guest_stack<sizeof(stack_words) &&
       !(address&7) && remote->iov_len==sizeof(uint64_t)) {
        memcpy(local->iov_base,&stack_words[(address-guest_stack)/8],sizeof(uint64_t));
        return sizeof(uint64_t);
    }
    if(remote->iov_len!=sizeof(test_frames[0]))return -1;
    if(remote->iov_base!=test_frames[0] && remote->iov_base!=test_frames[1])return -1;
    memcpy(local->iov_base,remote->iov_base,sizeof(test_frames[0]));
    return sizeof(test_frames[0]);
}
#define process_vm_readv synthetic_read
'''+actual+r'''
int main(int argc,char **argv) {
    (void)argv;
    ucontext_t uc={0};
    struct _libc_fpstate fp={0};
    const int regs[]={REG_RAX,REG_RBX,REG_RCX,REG_RDX,REG_RSI,REG_RSP,REG_RBP,REG_R12,REG_R15,REG_RDI};
    for (unsigned i=0;i<sizeof(regs)/sizeof(regs[0]);++i)
        uc.uc_mcontext.gregs[regs[i]]=(greg_t)(UINT64_C(0xfedcba9876543200)+i);
    uc.uc_mcontext.gregs[REG_RIP]=0x4567;
    fp.mxcsr=0xffc01f80;
    for (unsigned i=0;i<4;++i) {
        fp._xmm[i].element[0]=0x01234567u+i;
        fp._xmm[i].element[1]=0x89abcdefu+i;
        fp._xmm[i].element[2]=0x7fc00001u+i;
        fp._xmm[i].element[3]=0xff800000u+i;
    }
    uc.uc_mcontext.fpregs=argc>1 ? 0 : &fp;
    if(argc>2) {
        test_frames[0][0]=(uintptr_t)&test_frames[1][0];
        test_frames[0][1]=0x5678;
        uc.uc_mcontext.gregs[REG_RBP]=(greg_t)(uintptr_t)&test_frames[0][0];
    }
    if(argc>3) {
        for(unsigned i=0;i<8;++i)pointer_words[i]=UINT64_C(0xabc0000000000000)+i;
        for(unsigned i=0;i<24;++i)stack_words[i]=UINT64_C(0xdef0000000000000)+i;
        uc.uc_mcontext.gregs[REG_RDI]=(greg_t)guest_pointer;
        uc.uc_mcontext.gregs[REG_RSP]=(greg_t)guest_stack;
    }
    int fds[2]; if(pipe(fds))return 10;
    if(dup2(fds[1],2)<0)return 11;
    close(fds[1]);
    dump_frames(&uc);
    close(2);
    char buffer[2048]; ssize_t count;
    while((count=read(fds[0],buffer,sizeof(buffer)))>0)
        if(write(1,buffer,(size_t)count)!=count)return 12;
    close(fds[0]);
    return count<0?13:0;
}
'''
        with tempfile.TemporaryDirectory() as tmp:
            path = Path(tmp)
            c = path / 'dump.c'
            exe = path / 'dump'
            c.write_text(harness)
            compiler = shutil.which('cc')
            self.assertIsNotNone(compiler)
            subprocess.run([compiler, '-std=c11', '-Wall', '-Wextra', '-Werror', '-O2', str(c), '-o', str(exe)], check=True)
            full = subprocess.run([str(exe)], capture_output=True, check=True, text=True, timeout=5).stdout
            null = subprocess.run([str(exe), 'null'], capture_output=True, check=True, text=True, timeout=5).stdout
            chain = subprocess.run([str(exe), 'null', 'chain'], capture_output=True, check=True, text=True, timeout=5).stdout
            memory = subprocess.run([str(exe), 'null', 'chain', 'memory'], capture_output=True, check=True, text=True, timeout=5).stdout
        # Per-thread records retain full high bits, and existing RDI/frame syntax.
        tid = re.search(r'tid=([0-9a-f]{16})', full).group(1)
        for i, name in enumerate(('rax', 'rbx', 'rcx', 'rdx', 'rsi', 'rsp', 'rbp', 'r12', 'r15')):
            self.assertIn(f'  tid={tid} {name}={0xfedcba9876543200+i:016x}\n', full)
        self.assertIn(f'  tid={tid} rdi=fedcba9876543209\n', full)
        self.assertIn(f'  tid={tid} mxcsr=00000000ffc01f80\n', full)
        for i in range(4):
            expected = f'{0xff800000+i:08x}{0x7fc00001+i:08x}{0x89abcdef+i:08x}{0x01234567+i:08x}'
            self.assertIn(f'  tid={tid} xmm{i}={expected}\n', full)
        self.assertIn(f'  tid={tid} rip=0000000000004567 image-relative=0000000000003567 host-relative=0000000000002567\n', full)
        self.assertIn('fpregs=unavailable\n', null)
        self.assertNotIn('mxcsr=', null)
        self.assertNotIn('xmm0=', null)
        self.assertRegex(null, r'rdi=fedcba9876543209\n')
        self.assertRegex(null, r'rip=0000000000004567 image-relative=0000000000003567 host-relative=0000000000002567\n')
        self.assertRegex(chain, r'rip=0000000000005678 image-relative=0000000000004678 host-relative=0000000000003678\n')
        self.assertEqual(chain.count(' rip='), 2)
        self.assertTrue(all(len(line) < 160 for line in full.splitlines()))
        self.assertEqual(memory.count(' at 0000000800001000+'), 8)
        self.assertIn('+56=abc0000000000007\n', memory)
        self.assertEqual(memory.count(' stack+'), 24)
        self.assertIn('stack+184=def0000000000017\n', memory)


if __name__ == '__main__':
    unittest.main()
