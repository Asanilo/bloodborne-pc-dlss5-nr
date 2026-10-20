// SPDX-License-Identifier: GPL-2.0-or-later
//
// Source template for the native-mouse camera stub.  The production module embeds the bytes
// generated from this file and patches only the two trailing 64-bit slots (mailbox pointer and
// continuation pointer).  This file is not added to any build target.
//
// Entry: the camera site, before the original 16 bytes have executed.
// Exit:  the continuation, with the original 16 bytes replayed and, on real consumption, only
//        XMM1's low two floats changed.
//
// Field offsets mirror gpu/shim/native_mouse_mailbox.h.

    .intel_syntax noprefix
    .text
    .globl bb_native_mouse_stub_template
    .type bb_native_mouse_stub_template, @function
bb_native_mouse_stub_template:
    // Original site instructions (16 bytes, exactly as verified):
    //   vmulps xmm1, xmm10, xmm1
    //   vmovaps xmm2, [rbp-0x3f0]
    //   vmulps xmm1, xmm2, xmm1
    .byte 0xc5, 0xa8, 0x59, 0xc9
    .byte 0xc5, 0xf8, 0x28, 0x95, 0x10, 0xfc, 0xff, 0xff
    .byte 0xc5, 0xe8, 0x59, 0xc9

    // Private frame below the ABI red zone.  The red zone [old-rsp-128, old-rsp) is untouched.
    lea rsp, [rsp - 0x200]
    pushfq
    push rax
    push rbx
    push rcx
    push rdx
    push rsi
    push rdi
    push rbp
    push r8
    push r9
    push r10
    push r11
    push r12
    push r13
    push r14
    push r15
    sub rsp, 64
    // [rsp+0] XMM2 baseline, [rsp+16] XMM3 baseline, [rsp+32] MXCSR baseline,
    // [rsp+40] struct timespec, [rsp+56] spare.
    movdqu [rsp+0], xmm2
    movdqu [rsp+16], xmm3
    stmxcsr [rsp+32]

    // MXCSR exception-mask capability: all six masks must be set.
    mov eax, [rsp+32]
    and eax, 0x1f80
    cmp eax, 0x1f80
    jne .Lrestore

    // One try-acquire only; contention returns to the original path.
    mov rbx, qword ptr [rip + .Lmailbox_slot]
    xor eax, eax
    mov edx, 1
    lock cmpxchg dword ptr [rbx+0], edx
    jne .Lrestore

    cmp dword ptr [rbx+28], 0       // fatal
    jne .Lrelease
    cmp dword ptr [rbx+4], 0        // installed
    je .Lrelease
    cmp dword ptr [rbx+8], 0        // captured
    je .Lrelease

    // Required final time read after the gate was acquired.
    mov eax, 228                    // SYS_clock_gettime
    mov edi, 1                      // CLOCK_MONOTONIC
    lea rsi, [rsp+40]
    syscall
    test rax, rax
    jne .Lrelease
    mov rax, [rsp+40]               // tv_sec
    test rax, rax
    js .Lrelease
    mov rdx, [rsp+48]               // tv_nsec
    test rdx, rdx
    js .Lrelease
    cmp rdx, 1000000000
    jae .Lrelease
    mov rcx, 1000000
    mul rcx                         // rdx:rax = sec * 1e6
    test rdx, rdx
    jne .Lrelease
    mov r8, rax
    mov rax, [rsp+48]
    xor edx, edx
    mov ecx, 1000
    div rcx                         // rax = nsec / 1000
    add r8, rax
    jc .Lrelease                    // r8 = now_us

    mov rax, [rbx+80]               // last_seen_us
    cmp r8, rax
    jb .Lcancel_generation
    mov [rbx+80], r8

    cmp dword ptr [rbx+20], 0       // has_pending
    je .Lheartbeat

    cmp dword ptr [rbx+12], 0       // ready
    je .Lcancel_pending

    mov rax, [rbx+72]               // pending_since_us
    cmp r8, rax
    jb .Lcancel_pending
    mov r9, r8
    sub r9, rax
    cmp r9, 100000
    jae .Lcancel_pending            // exactly 100 ms is expired

    // A heartbeat sequence overflow disables the candidate before any guest mutation.
    mov rax, [rbx+96]
    cmp rax, -1
    je .Lfatal

    // Consume.  Only per-axis non-zero terms are added; signed zero/NaN lanes stay untouched.
    mov eax, [rbx+88]               // pitch bits
    mov edx, [rbx+92]               // yaw bits
    mov r10d, eax
    and r10d, 0x7fffffff
    mov r11d, edx
    and r11d, 0x7fffffff
    mov ecx, r10d
    or ecx, r11d
    test r10d, r10d
    jz .Lskip_pitch
    movd xmm2, eax
    addss xmm1, xmm2
.Lskip_pitch:
    test r11d, r11d
    jz .Lskip_yaw
    movd xmm2, edx
    movaps xmm3, xmm1
    shufps xmm3, xmm3, 0x55
    addss xmm3, xmm2
    insertps xmm1, xmm3, 0x10
.Lskip_yaw:
    test ecx, ecx
    jz .Lretire_batch

    // Only a real non-zero displacement writes the three camera flags.
    mov dword ptr [rbp-0x3c4], 1
    mov byte ptr [r13+0x15c], 1
    mov dword ptr [r13+0x154], 0

.Lretire_batch:
    mov rax, [rbx+48]               // pending_last_seq
    mov [rbx+56], rax               // last_consumed_seq
    mov dword ptr [rbx+20], 0
    mov qword ptr [rbx+40], 0
    mov qword ptr [rbx+48], 0
    mov qword ptr [rbx+72], 0
    mov dword ptr [rbx+88], 0
    mov dword ptr [rbx+92], 0
    jmp .Lheartbeat_valid

.Lheartbeat:
    mov rax, [rbx+96]
    cmp rax, -1
    je .Lfatal
    inc rax
    mov [rbx+96], rax
    mov rdx, [rbx+104]              // reset_watermark
    cmp rax, rdx
    jbe .Lrelease
    mov rdx, [rbx+112]              // fallback_watermark
    cmp rax, rdx
    jbe .Lrelease

    // Recovering from reset/fallback also advances the published generation, so host deltas
    // accumulated before recovery cannot be queued as native.
    cmp dword ptr [rbx+12], 0
    jne .Lheartbeat_commit
    mov eax, [rbx+16]
    cmp eax, -1
    je .Lfatal
    inc eax
    mov [rbx+16], eax
    mov [rbx+24], eax
.Lheartbeat_commit:
    mov [rbx+64], r8
    mov dword ptr [rbx+12], 1
    mov dword ptr [rbx+0], 0
    jmp .Lrestore

.Lheartbeat_valid:
    mov rax, [rbx+96]
    inc rax
    mov [rbx+96], rax
    mov [rbx+64], r8
    mov dword ptr [rbx+12], 1
    mov dword ptr [rbx+0], 0
    jmp .Lrestore

.Lcancel_pending:
    mov r9d, 1
    jmp .Lcancel_common

.Lcancel_generation:
    xor r9d, r9d
    mov [rbx+80], r8

.Lcancel_common:
    cmp dword ptr [rbx+20], 0
    je .Lcancel_clear_pending
    mov rax, [rbx+48]
    mov [rbx+120], rax              // cancelled_seq
.Lcancel_clear_pending:
    mov dword ptr [rbx+20], 0
    mov qword ptr [rbx+40], 0
    mov qword ptr [rbx+48], 0
    mov qword ptr [rbx+72], 0
    mov dword ptr [rbx+88], 0
    mov dword ptr [rbx+92], 0
    mov eax, [rbx+16]
    cmp eax, -1
    je .Lfatal
    inc eax
    mov [rbx+16], eax
    mov [rbx+24], eax               // release-published epoch (x86 TSO store)
    mov rax, [rbx+96]
    test r9d, r9d
    jnz .Lcancel_mark_fallback
    mov [rbx+104], rax              // reset_watermark
    jmp .Lcancel_finish
.Lcancel_mark_fallback:
    mov [rbx+112], rax              // fallback_watermark
.Lcancel_finish:
    mov dword ptr [rbx+12], 0
    mov qword ptr [rbx+64], 0
    mov dword ptr [rbx+0], 0
    jmp .Lrestore

.Lfatal:
    mov dword ptr [rbx+28], 1
    cmp dword ptr [rbx+20], 0
    je .Lfatal_clear_pending
    mov rax, [rbx+48]
    mov [rbx+120], rax
.Lfatal_clear_pending:
    mov dword ptr [rbx+20], 0
    mov qword ptr [rbx+40], 0
    mov qword ptr [rbx+48], 0
    mov qword ptr [rbx+72], 0
    mov dword ptr [rbx+88], 0
    mov dword ptr [rbx+92], 0
    mov qword ptr [rbx+64], 0
    mov dword ptr [rbx+12], 0
    mov dword ptr [rbx+24], 0       // published_epoch = 0 disables the candidate
    mov dword ptr [rbx+0], 0
    jmp .Lrestore

.Lrelease:
    mov dword ptr [rbx+0], 0
    jmp .Lrestore

.Lrestore:
    ldmxcsr [rsp+32]
    movdqu xmm2, [rsp+0]
    movdqu xmm3, [rsp+16]
    add rsp, 64
    pop r15
    pop r14
    pop r13
    pop r12
    pop r11
    pop r10
    pop r9
    pop r8
    pop rbp
    pop rdi
    pop rsi
    pop rdx
    pop rcx
    pop rbx
    pop rax
    popfq
    lea rsp, [rsp + 0x200]
    jmp qword ptr [rip + .Lcontinuation_slot]

    .p2align 3
.Lmailbox_slot:
    .quad 0x1122334455667788
.Lcontinuation_slot:
    .quad 0x8877665544332211
    .size bb_native_mouse_stub_template, .-bb_native_mouse_stub_template

    .section .note.GNU-stack,"",@progbits
