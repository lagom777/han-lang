# 가나다 가상 — 리눅스 x86_64 시드
# C 없음. 시스템호출만 (읽기/쓰기/열기/닫기/끝내기).
# 값·명령·칸은 숫자. 기호는 묶기가 이 숫자로 바꾼다.

        .intel_syntax noprefix
        .global _start

        .bss
        .align 8
code_buf:
        .space 65536
vstack:
        .space 32768
locals:
        .space 2048
numbuf:
        .space 32

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

interp:
        cmp r12, r9
        jge halt_ok
        movzx eax, byte ptr [r14 + r12]
        inc r12
        cmp eax, 0
        je halt_ok
        cmp eax, 1
        je op_pushi
        cmp eax, 2
        je op_printi
        cmp eax, 3
        je op_prints
        cmp eax, 4
        je op_add
        cmp eax, 5
        je op_sub
        cmp eax, 6
        je op_mul
        cmp eax, 7
        je op_div
        cmp eax, 8
        je op_dup
        cmp eax, 9
        je op_drop
        cmp eax, 10
        je op_jmp
        cmp eax, 11
        je op_jz
        cmp eax, 12
        je op_load
        cmp eax, 13
        je op_store
        cmp eax, 14
        je op_eq
        cmp eax, 15
        je op_lt
        cmp eax, 16
        je op_gt
        cmp eax, 17
        je op_not
        jmp fail

op_pushi:
        mov rax, [r14 + r12]
        add r12, 8
        call push
        jmp interp

op_printi:
        call pop
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
        call pop
        mov rcx, rax
        call pop
        add rax, rcx
        call push
        jmp interp
op_sub:
        call pop
        mov rcx, rax
        call pop
        sub rax, rcx
        call push
        jmp interp
op_mul:
        call pop
        mov rcx, rax
        call pop
        imul rax, rcx
        call push
        jmp interp
op_div:
        call pop
        mov rcx, rax
        test rcx, rcx
        jz fail
        call pop
        cqo
        idiv rcx
        call push
        jmp interp

op_dup:
        test r13, r13
        jz fail
        mov rax, [r15 + r13*8 - 8]
        call push
        jmp interp
op_drop:
        call pop
        jmp interp

op_jmp:
        movsx rax, dword ptr [r14 + r12]
        add r12, 4
        add r12, rax
        jmp interp
op_jz:
        movsx rcx, dword ptr [r14 + r12]
        add r12, 4
        call pop
        test rax, rax
        jnz interp
        add r12, rcx
        jmp interp

op_load:
        movzx eax, byte ptr [r14 + r12]
        inc r12
        mov rax, [rbx + rax*8]
        call push
        jmp interp
op_store:
        movzx ecx, byte ptr [r14 + r12]
        inc r12
        call pop
        mov [rbx + rcx*8], rax
        jmp interp

op_eq:
        call pop
        mov rcx, rax
        call pop
        xor rdx, rdx
        cmp rax, rcx
        sete dl
        mov rax, rdx
        call push
        jmp interp
op_lt:
        call pop
        mov rcx, rax
        call pop
        xor rdx, rdx
        cmp rax, rcx
        setl dl
        mov rax, rdx
        call push
        jmp interp
op_gt:
        call pop
        mov rcx, rax
        call pop
        xor rdx, rdx
        cmp rax, rcx
        setg dl
        mov rax, rdx
        call push
        jmp interp
op_not:
        call pop
        test rax, rax
        setz al
        movzx rax, al
        call push
        jmp interp

push:
        cmp r13, 4096
        jge fail
        mov [r15 + r13*8], rax
        inc r13
        ret

pop:
        test r13, r13
        jz fail
        dec r13
        mov rax, [r15 + r13*8]
        ret

print_i64:
        push rbx
        push r12
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
