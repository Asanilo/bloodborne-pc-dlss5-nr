// SPDX-License-Identifier: GPL-2.0-or-later
// Serial CPU fixture. Restore the same complete input XSTATE for reference and detour,
// snapshot architectural state before returning to C++, then restore the host's state.
    .intel_syntax noprefix
    .text
    .globl bb_mouse_xstate_run
    .type bb_mouse_xstate_run, @function
bb_mouse_xstate_run:
    pushfq
    push rbp
    push rbx
    push r12
    push r13
    push r14
    push r15
    mov [rip+bb_xstate_host_rsp], rsp
    mov rdi, [rip+bb_xstate_context]
    mov rsi, [rdi+24]             // host save area
    mov rax, [rdi+32]             // XCR0 mask
    mov rdx, rax
    shr rdx, 32
    xsave64 [rsi]
    mov rsi, [rdi+8]              // identical input state
    mov rax, [rdi+32]
    mov rdx, rax
    shr rdx, 32
    xrstor64 [rsi]
    // Set flags on the host stack, before entering the synthetic guest stack.
    push 0xad7                    // safe status bits, IF; DF/TF clear
    popfq
    mov rbp, [rdi+40]
    lea rbp, [rbp+0x400]
    mov r13, [rdi+48]
    mov rsp, [rdi+56]
    lea rax, [rip+.Lxstate_done]
    mov [rsp], rax
    mov rax, 0xaaaaaaaaaaaaaaaa
    mov rbx, 0x1111111111111111
    mov rcx, 0x2222222222222222
    mov rdx, 0x3333333333333333
    mov rsi, 0x4444444444444444
    mov rdi, 0x5555555555555555
    mov r8,  0x8888888888888888
    mov r9,  0x9999999999999999
    mov r10, 0x1010101010101010
    mov r11, 0x1111222233334444
    mov r12, 0x5555666677778888
    mov r14, 0xaaaabbbbccccdddd
    mov r15, 0xeeeeffff00001111
    jmp qword ptr [rip+bb_xstate_site]
.Lxstate_done:
    mov [rip+bb_xstate_gprs+0], rax
    mov [rip+bb_xstate_gprs+8], rbx
    mov [rip+bb_xstate_gprs+16], rcx
    mov [rip+bb_xstate_gprs+24], rdx
    mov [rip+bb_xstate_gprs+32], rsi
    mov [rip+bb_xstate_gprs+40], rdi
    mov [rip+bb_xstate_gprs+48], rbp
    mov [rip+bb_xstate_gprs+56], rsp
    mov [rip+bb_xstate_gprs+64], r8
    mov [rip+bb_xstate_gprs+72], r9
    mov [rip+bb_xstate_gprs+80], r10
    mov [rip+bb_xstate_gprs+88], r11
    mov [rip+bb_xstate_gprs+96], r12
    mov [rip+bb_xstate_gprs+104], r13
    mov [rip+bb_xstate_gprs+112], r14
    mov [rip+bb_xstate_gprs+120], r15
    // Return to the host stack before saving flags: do not overwrite guest red zone.
    mov rsp, [rip+bb_xstate_host_rsp]
    pushfq
    pop rax
    mov [rip+bb_xstate_flags], rax
    mov rdi, [rip+bb_xstate_context]
    mov rsi, [rdi+16]             // actual output XSTATE
    mov rax, [rdi+32]
    mov rdx, rax
    shr rdx, 32
    xsave64 [rsi]
    mov rsi, [rdi+24]
    mov rax, [rdi+32]
    mov rdx, rax
    shr rdx, 32
    xrstor64 [rsi]
    pop r15
    pop r14
    pop r13
    pop r12
    pop rbx
    pop rbp
    popfq
    ret
    .size bb_mouse_xstate_run, .-bb_mouse_xstate_run
    .section .note.GNU-stack,"",@progbits
