#ifndef BBPORT_CPU_TEST_NO_NETWORK_H
#define BBPORT_CPU_TEST_NO_NETWORK_H
/* CPU online tests must not contact a server or use an ambient account. */
#include <stdio.h>
#include <stdlib.h>
#if defined(__linux__)
#include <stddef.h>
#include <linux/filter.h>
#include <linux/seccomp.h>
#include <sys/prctl.h>
#include <sys/syscall.h>
static inline void bb_test_deny_network(void) {
    struct sock_filter code[] = {
        BPF_STMT(BPF_LD | BPF_W | BPF_ABS, offsetof(struct seccomp_data, nr)),
        BPF_JUMP(BPF_JMP | BPF_JEQ | BPF_K, SYS_socket, 0, 1),
        BPF_STMT(BPF_RET | BPF_K, SECCOMP_RET_ERRNO | 1),
        BPF_JUMP(BPF_JMP | BPF_JEQ | BPF_K, SYS_connect, 0, 1),
        BPF_STMT(BPF_RET | BPF_K, SECCOMP_RET_ERRNO | 1),
        BPF_JUMP(BPF_JMP | BPF_JEQ | BPF_K, SYS_sendto, 0, 1),
        BPF_STMT(BPF_RET | BPF_K, SECCOMP_RET_ERRNO | 1),
        BPF_JUMP(BPF_JMP | BPF_JEQ | BPF_K, SYS_sendmsg, 0, 1),
        BPF_STMT(BPF_RET | BPF_K, SECCOMP_RET_ERRNO | 1),
        BPF_STMT(BPF_RET | BPF_K, SECCOMP_RET_ALLOW),
    };
    struct sock_fprog filter = {(unsigned short)(sizeof(code)/sizeof(code[0])), code};
    if (prctl(PR_SET_NO_NEW_PRIVS, 1, 0, 0, 0) ||
        prctl(PR_SET_SECCOMP, SECCOMP_MODE_FILTER, &filter)) {
        perror("CPU online test requires network-denial filter");
        abort();
    }
}
#else
static inline void bb_test_deny_network(void) {
    fputs("CPU online test has no network-denial implementation on this platform\n", stderr);
    abort();
}
#endif
#endif
