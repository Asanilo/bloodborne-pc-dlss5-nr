// SPDX-License-Identifier: GPL-2.0-or-later
//
// Synthetic guest camera frame for the CPU stub tests.  It deliberately uses fixed, benign
// operands so the baseline after the original 16 bytes is exactly the XMM1 input.  The site is
// supplied by C++ and is normally the installed synthetic image.

    .intel_syntax noprefix
    .text
    .globl bb_native_mouse_fixture_run
    .type bb_native_mouse_fixture_run, @function
bb_native_mouse_fixture_run:
    push rbx
    push rbp
    push r12
    push r13
    push r14
    push r15
    mov [rip + bb_fixture_saved_rsp], rsp

    lea rbp, [rip + bb_fixture_frame]
    add rbp, 0x400
    lea r13, [rip + bb_fixture_camera]

    mov dword ptr [rbp - 0x3c4], 0xdeadbeef
    mov eax, [rbp - 0x3c4]
    mov [rip + bb_fixture_output + 132], eax
    mov dword ptr [r13 + 0x154], 0x11111111
    mov dword ptr [r13 + 0x158], 0x22222222
    mov byte ptr [r13 + 0x15c], 0x33

    movdqu xmm0, [rip + fixture_one]
    movdqu [rbp - 0x3f0], xmm0
    movdqu xmm10, [rip + fixture_one]

    movdqu xmm1, [rip + fixture_xmm1]
    movdqu xmm2, [rip + fixture_xmm2]
    movdqu xmm3, [rip + fixture_xmm3]
    movdqu xmm4, [rip + fixture_xmm4]
    movdqu xmm5, [rip + fixture_xmm5]
    movdqu xmm0, [rip + fixture_xmm0]
    ldmxcsr [rip + bb_fixture_mxcsr_in]

    pushfq
    or qword ptr [rsp], 0x8d5
    popfq
    pushfq
    pop rax
    mov [rip + bb_fixture_flags_in], rax

    lea rsp, [rip + bb_fixture_stack + 0xff8]
    mov [rip + bb_fixture_test_rsp], rsp
    lea rdi, [rsp - 128]
    mov ecx, 128
    mov al, 0xa5
    rep stosb
    lea rax, [rip + .Lfixture_done]
    mov [rsp], rax

    mov rax, 0xaaaaaaaaaaaaaaaa
    mov rbx, 0x1111111111111111
    mov rcx, 0x2222222222222222
    mov rdx, 0x3333333333333333
    mov rsi, 0x4444444444444444
    mov rdi, 0x5555555555555555
    mov r8, 0x8888888888888888
    mov r9, 0x9999999999999999
    mov r10, 0x1010101010101010
    mov r11, 0x1111222233334444
    mov r12, 0x5555666677778888
    mov r14, 0xaaaabbbbccccdddd
    mov r15, 0xeeeeffff00001111

    mov rax, 0xaaaaaaaaaaaaaaaa

    jmp qword ptr [rip + bb_fixture_site]

.Lfixture_done:
    mov [rip + bb_fixture_output + 152], rsp

    mov [rip + bb_fixture_output + 0], rax
    mov [rip + bb_fixture_output + 8], rbx
    mov [rip + bb_fixture_output + 16], rcx
    mov [rip + bb_fixture_output + 24], rdx
    mov [rip + bb_fixture_output + 32], rsi
    mov [rip + bb_fixture_output + 40], rdi
    mov [rip + bb_fixture_output + 48], rbp
    mov [rip + bb_fixture_output + 56], r8
    mov [rip + bb_fixture_output + 64], r9
    mov [rip + bb_fixture_output + 72], r10
    mov [rip + bb_fixture_output + 80], r11
    mov [rip + bb_fixture_output + 88], r12
    mov [rip + bb_fixture_output + 96], r13
    mov [rip + bb_fixture_output + 104], r14
    mov [rip + bb_fixture_output + 112], r15

    pushfq
    pop rax
    mov [rip + bb_fixture_output + 120], rax
    stmxcsr [rip + bb_fixture_output + 128]

    mov eax, [rbp - 0x3c4]
    mov [rip + bb_fixture_output + 136], eax
    mov eax, [r13 + 0x158]
    mov [rip + bb_fixture_output + 140], eax
    mov eax, [r13 + 0x154]
    mov [rip + bb_fixture_output + 144], eax
    mov al, [r13 + 0x15c]
    mov [rip + bb_fixture_output + 148], al

    movdqu [rip + bb_fixture_output + 160], xmm0
    movdqu [rip + bb_fixture_output + 176], xmm1
    movdqu [rip + bb_fixture_output + 192], xmm2
    movdqu [rip + bb_fixture_output + 208], xmm3
    movdqu [rip + bb_fixture_output + 224], xmm4
    movdqu [rip + bb_fixture_output + 240], xmm5
    movdqu [rip + bb_fixture_output + 256], xmm10

    mov rsi, [rip + bb_fixture_test_rsp]
    sub rsi, 128
    lea rdi, [rip + bb_fixture_output + 272]
    mov ecx, 128
    rep movsb

    mov rsp, [rip + bb_fixture_saved_rsp]
    pop r15
    pop r14
    pop r13
    pop r12
    pop rbp
    pop rbx
    ret

    .section .rodata
    .p2align 4
fixture_one:
    .long 0x3f800000, 0x3f800000, 0x3f800000, 0x3f800000
fixture_xmm1:
    .long 0x3f800000, 0x40000000, 0x7fc00001, 0x80000000
fixture_xmm2:
    .long 0xaaaaaaaa, 0xaaaaaaaa, 0xaaaaaaaa, 0xaaaaaaaa
fixture_xmm3:
    .long 0xbbbbbbbb, 0xbbbbbbbb, 0xbbbbbbbb, 0xbbbbbbbb
fixture_xmm4:
    .long 0xcccccccc, 0xcccccccc, 0xcccccccc, 0xcccccccc
fixture_xmm5:
    .long 0xdddddddd, 0xdddddddd, 0xdddddddd, 0xdddddddd
fixture_xmm0:
    .long 0xeeeeeeee, 0xeeeeeeee, 0xeeeeeeee, 0xeeeeeeee

    .section .note.GNU-stack,"",@progbits
