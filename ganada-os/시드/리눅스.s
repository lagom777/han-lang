# 가나다 가상 — 리눅스 x86_64 시드
# C 없음. 시스템호출만. 점프표 + 스택 인라인.

        .intel_syntax noprefix
        .global _start

        .bss
        .align 8
code_buf:
        .space 65536
vstack:
        .space 32768
locals:
        .space 4096
cstack:
        .space 256
pathbuf:
        .space 1024
numbuf:
        .space 32
onebyte:
        .space 8

        .data
msg_usage:
        .ascii "가상: 쓰임: 가상 파일.ㄱㅂ\n"
        .equ msg_usage_len, . - msg_usage
msg_err:
        .ascii "가상: 오류\n"
        .equ msg_err_len, . - msg_err
nl:
        .ascii "\n"
digits:
        .ascii "0123456789"

        .text
        .align 8
optable:
        .quad op_halt
        .quad op_pushi
        .quad op_printi
        .quad op_prints
        .quad op_add
        .quad op_sub
        .quad op_mul
        .quad op_div
        .quad op_dup
        .quad op_drop
        .quad op_jmp
        .quad op_jz
        .quad op_load
        .quad op_store
        .quad op_eq
        .quad op_lt
        .quad op_gt
        .quad op_not
        .quad op_mod
        .quad op_call
        .quad op_ret
        .quad op_openr
        .quad op_openw
        .quad op_getc
        .quad op_putc
        .quad op_close
        .equ OP_MAX, 25

_start:
        mov rax, [rsp]
        cmp rax, 2
        jge 1f
        mov rax, 1
        mov rdi, 2
        lea rsi, [rip + msg_usage]
        mov rdx, msg_usage_len
        syscall
        mov rax, 60
        mov rdi, 2
        syscall
1:
        mov rdi, [rsp + 16]
        mov rax, 2
        xor rsi, rsi
        xor rdx, rdx
        syscall
        cmp rax, 0
        jl fail
        mov r8, rax
        mov rax, 0
        mov rdi, r8
        lea rsi, [rip + code_buf]
        mov rdx, 65536
        syscall
        cmp rax, 0
        jle fail
        mov r9, rax
        mov rax, 3
        mov rdi, r8
        syscall

        xor r12, r12
        xor r13, r13
        lea r14, [rip + code_buf]
        lea r15, [rip + vstack]
        lea rbx, [rip + locals]
        xor rbp, rbp
        mov r10, 32
        xor r8, r8

        .p2align 4
interp:
        cmp r12, r9
        jge halt_ok
        movzx eax, byte ptr [r14 + r12]
        inc r12
        cmp eax, OP_MAX
        ja fail
        lea rcx, [rip + optable]
        jmp qword ptr [rcx + rax*8]

op_halt:
        jmp halt_ok

op_pushi:
        mov rax, [r14 + r12]
        add r12, 8
        cmp r13, 4096
        jge fail
        mov [r15 + r13*8], rax
        inc r13
        jmp interp

op_printi:
        test r13, r13
        jz fail
        dec r13
        mov rax, [r15 + r13*8]
        call print_i64
        mov rax, 1
        mov rdi, 1
        lea rsi, [rip + nl]
        mov rdx, 1
        syscall
        jmp interp

op_prints:
        mov ecx, dword ptr [r14 + r12]
        add r12, 4
        mov rax, 1
        mov rdi, 1
        lea rsi, [r14 + r12]
        mov edx, ecx
        push rcx
        syscall
        pop rcx
        add r12, rcx
        mov rax, 1
        mov rdi, 1
        lea rsi, [rip + nl]
        mov rdx, 1
        syscall
        jmp interp

op_add:
        cmp r13, 2
        jl fail
        dec r13
        mov rcx, [r15 + r13*8]
        dec r13
        mov rax, [r15 + r13*8]
        add rax, rcx
        mov [r15 + r13*8], rax
        inc r13
        jmp interp
op_sub:
        cmp r13, 2
        jl fail
        dec r13
        mov rcx, [r15 + r13*8]
        dec r13
        mov rax, [r15 + r13*8]
        sub rax, rcx
        mov [r15 + r13*8], rax
        inc r13
        jmp interp
op_mul:
        cmp r13, 2
        jl fail
        dec r13
        mov rcx, [r15 + r13*8]
        dec r13
        mov rax, [r15 + r13*8]
        imul rax, rcx
        mov [r15 + r13*8], rax
        inc r13
        jmp interp
op_div:
        cmp r13, 2
        jl fail
        dec r13
        mov rcx, [r15 + r13*8]
        test rcx, rcx
        jz fail
        dec r13
        mov rax, [r15 + r13*8]
        cqo
        idiv rcx
        mov [r15 + r13*8], rax
        inc r13
        jmp interp
op_mod:
        cmp r13, 2
        jl fail
        dec r13
        mov rcx, [r15 + r13*8]
        test rcx, rcx
        jz fail
        dec r13
        mov rax, [r15 + r13*8]
        cqo
        idiv rcx
        mov [r15 + r13*8], rdx
        inc r13
        jmp interp

op_dup:
        test r13, r13
        jz fail
        mov rax, [r15 + r13*8 - 8]
        cmp r13, 4096
        jge fail
        mov [r15 + r13*8], rax
        inc r13
        jmp interp
op_drop:
        test r13, r13
        jz fail
        dec r13
        jmp interp

op_jmp:
        movsx rax, dword ptr [r14 + r12]
        add r12, 4
        add r12, rax
        jmp interp
op_jz:
        movsx rcx, dword ptr [r14 + r12]
        add r12, 4
        test r13, r13
        jz fail
        dec r13
        mov rax, [r15 + r13*8]
        test rax, rax
        jnz interp
        add r12, rcx
        jmp interp

op_load:
        movzx ecx, byte ptr [r14 + r12]
        inc r12
        cmp ecx, 32
        jae fail
        add rcx, rbp
        mov rax, [rbx + rcx*8]
        cmp r13, 4096
        jge fail
        mov [r15 + r13*8], rax
        inc r13
        jmp interp
op_store:
        movzx ecx, byte ptr [r14 + r12]
        inc r12
        cmp ecx, 32
        jae fail
        test r13, r13
        jz fail
        dec r13
        mov rax, [r15 + r13*8]
        add rcx, rbp
        mov [rbx + rcx*8], rax
        jmp interp

op_eq:
        cmp r13, 2
        jl fail
        dec r13
        mov rcx, [r15 + r13*8]
        dec r13
        mov rax, [r15 + r13*8]
        xor rdx, rdx
        cmp rax, rcx
        sete dl
        mov [r15 + r13*8], rdx
        inc r13
        jmp interp
op_lt:
        cmp r13, 2
        jl fail
        dec r13
        mov rcx, [r15 + r13*8]
        dec r13
        mov rax, [r15 + r13*8]
        xor rdx, rdx
        cmp rax, rcx
        setl dl
        mov [r15 + r13*8], rdx
        inc r13
        jmp interp
op_gt:
        cmp r13, 2
        jl fail
        dec r13
        mov rcx, [r15 + r13*8]
        dec r13
        mov rax, [r15 + r13*8]
        xor rdx, rdx
        cmp rax, rcx
        setg dl
        mov [r15 + r13*8], rdx
        inc r13
        jmp interp
op_not:
        test r13, r13
        jz fail
        mov rax, [r15 + r13*8 - 8]
        test rax, rax
        setz al
        movzx rax, al
        mov [r15 + r13*8 - 8], rax
        jmp interp

op_call:
        movsx rax, dword ptr [r14 + r12]
        add r12, 4
        cmp r8, 16
        jge fail
        mov rcx, r8
        shl rcx, 4
        lea rdx, [rip + cstack]
        mov [rdx + rcx], r12
        mov [rdx + rcx + 8], rbp
        inc r8
        mov rbp, r10
        add r10, 32
        cmp r10, 512
        jg fail
        add r12, rax
        jmp interp

op_ret:
        test r8, r8
        jz halt_ok
        dec r8
        mov rcx, r8
        shl rcx, 4
        lea rdx, [rip + cstack]
        mov r10, rbp
        mov r12, [rdx + rcx]
        mov rbp, [rdx + rcx + 8]
        jmp interp

op_openr:
        call copy_path
        mov rax, 2
        lea rdi, [rip + pathbuf]
        xor rsi, rsi
        xor rdx, rdx
        syscall
        cmp rax, 0
        jl fail
        cmp r13, 4096
        jge fail
        mov [r15 + r13*8], rax
        inc r13
        jmp interp

op_openw:
        call copy_path
        mov rax, 2
        lea rdi, [rip + pathbuf]
        mov rsi, 577
        mov rdx, 420
        syscall
        cmp rax, 0
        jl fail
        cmp r13, 4096
        jge fail
        mov [r15 + r13*8], rax
        inc r13
        jmp interp

op_getc:
        test r13, r13
        jz fail
        dec r13
        mov rdi, [r15 + r13*8]
        mov rax, 0
        lea rsi, [rip + onebyte]
        mov rdx, 1
        syscall
        cmp rax, 1
        je 1f
        mov rax, -1
        jmp 2f
1:
        movzx eax, byte ptr [rip + onebyte]
2:
        mov [r15 + r13*8], rax
        inc r13
        jmp interp

op_putc:
        cmp r13, 2
        jl fail
        dec r13
        mov rax, [r15 + r13*8]
        mov byte ptr [rip + onebyte], al
        dec r13
        mov rdi, [r15 + r13*8]
        mov rax, 1
        lea rsi, [rip + onebyte]
        mov rdx, 1
        syscall
        cmp rax, 1
        jne fail
        jmp interp

op_close:
        test r13, r13
        jz fail
        dec r13
        mov rdi, [r15 + r13*8]
        mov rax, 3
        syscall
        jmp interp

copy_path:
        mov ecx, dword ptr [r14 + r12]
        add r12, 4
        cmp ecx, 1023
        ja fail
        lea rdi, [rip + pathbuf]
        lea rsi, [r14 + r12]
        add r12, rcx
        mov edx, ecx
        rep movsb
        mov byte ptr [rdi], 0
        ret

print_i64:
        push rbx
        push r12
        push r8
        push r9
        mov r12, rax
        test r12, r12
        jnz 1f
        mov rax, 1
        mov rdi, 1
        lea rsi, [rip + digits]
        mov rdx, 1
        syscall
        jmp 9f
1:
        xor ebx, ebx
        test r12, r12
        jns 2f
        mov ebx, 1
        neg r12
2:
        lea rdi, [rip + numbuf]
        add rdi, 31
        xor ecx, ecx
3:
        mov rax, r12
        xor rdx, rdx
        mov r8, 10
        div r8
        mov r12, rax
        lea r9, [rip + digits]
        mov al, [r9 + rdx]
        mov [rdi], al
        dec rdi
        inc ecx
        test r12, r12
        jnz 3b
        test ebx, ebx
        jz 4f
        mov byte ptr [rdi], '-'
        dec rdi
        inc ecx
4:
        inc rdi
        mov rax, 1
        mov rsi, rdi
        mov rdi, 1
        mov edx, ecx
        syscall
9:
        pop r9
        pop r8
        pop r12
        pop rbx
        ret

halt_ok:
        mov rax, 60
        xor rdi, rdi
        syscall

fail:
        mov rax, 1
        mov rdi, 2
        lea rsi, [rip + msg_err]
        mov rdx, msg_err_len
        syscall
        mov rax, 60
        mov rdi, 1
        syscall
