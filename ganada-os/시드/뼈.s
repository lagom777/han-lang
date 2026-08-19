# 가나다 뼈 — 모든 낳은 ELF 안에 들어가는 손.
# 숫자 명령이 기계어가 된 뒤, 이 손이 출력·집·파일을 연다.
# C 없음. 시스템호출만. 적재 주소 0x400078.

        .intel_syntax noprefix
        .global _start

        .equ VSTACK,  0x500000
        .equ LOCALS,  0x508000
        .equ CSTACK,  0x50C000
        .equ HEAP,    0x50D000
        .equ PATHBUF, 0x54D000
        .equ NUMBUF,  0x54D400
        .equ ONEBYTE, 0x54D420
        .equ ARGC,    0x54D428
        .equ ARGV,    0x54D430

        .text
표:
        .quad print_i64
        .quad fail
        .quad nl
        .quad digits
        .quad halt_ok

_start:
        mov rax, [rsp]
        mov qword ptr [ARGC], rax
        lea rax, [rsp + 8]
        mov qword ptr [ARGV], rax
        mov r15, VSTACK
        xor r13, r13
        mov rbx, LOCALS
        xor rbp, rbp
        mov r10, 32
        xor r8, r8
        mov rax, 0x4141414141414141
        jmp rax

        .align 8
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
        mov rdi, NUMBUF
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

fail:
        mov rax, 1
        mov rdi, 2
        lea rsi, [rip + msg_err]
        mov rdx, 11
        syscall
        mov rax, 60
        mov rdi, 1
        syscall

halt_ok:
        mov rax, 60
        xor rdi, rdi
        syscall

nl:
        .ascii "\n"
digits:
        .ascii "0123456789"
msg_err:
        .ascii "낳기: 오류\n"
