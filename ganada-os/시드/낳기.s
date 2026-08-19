# 가나다 낳기 — .ㄱㅂ 숫자열 → x86-64 ELF
# 가상 해석 없음. CPU가 바로 달린다.

        .intel_syntax noprefix
        .global _start

        .equ HDR,      120
        .equ BASE,     0x400000
        .equ BONES_VA, 0x400078
        .equ CSTACK,   0x50C000
        .equ HEAP,     0x50D000
        .equ ONEBYTE,  0x54D420
        .equ ARGC,     0x54D428
        .equ ARGV,     0x54D430
        .equ MEMSZ,    0x160000

        .macro B x
        mov al, \x
        call 넣기8
        .endm

        .bss
        .align 8
inbuf:  .space 65536
outbuf: .space 2097152
mapbuf: .space 262144

        .data
msg_usage:
        .ascii "낳기: 쓰임: 낳기 파일.ㄱㅂ 낼파일\n"
        .equ msg_usage_len, . - msg_usage
msg_err:
        .ascii "낳기: 오류\n"
        .equ msg_err_len, . - msg_err

        .text
뼈물:
        .incbin "산출/뼈.bin"
뼈끝:
        .equ 뼈길이, 뼈끝 - 뼈물

_start:
        cmp qword ptr [rsp], 3
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
        jl die
        mov r8, rax
        xor eax, eax
        mov rdi, r8
        lea rsi, [rip + inbuf]
        mov rdx, 65536
        syscall
        cmp rax, 1
        jl die
        mov r9, rax
        mov rax, 3
        mov rdi, r8
        syscall

        lea r15, [rip + inbuf]
        lea rbx, [rip + mapbuf]
        xor r11, r11
        xor r8, r8
p1:
        cmp r11, r9
        je p1끝
        ja die
        mov [rbx + r11*4], r8d
        call 크기
        add r8, rax
        jmp p1
p1끝:
        mov [rbx + r11*4], r8d

        lea r14, [rip + outbuf]
        xor r12, r12
        call 머리쓰기
        lea rsi, [rip + 뼈물]
        mov rcx, 뼈길이
        lea rdi, [r14 + r12]
        rep movsb
        add r12, 뼈길이

        mov rax, BASE
        add rax, r12
        lea rdi, [rip + outbuf + HDR]
        mov rcx, 뼈길이
s_mark:
        cmp rcx, 8
        jb die
        mov r13, 0x4141414141414141
        cmp qword ptr [rdi], r13
        je s_ok
        inc rdi
        dec rcx
        jmp s_mark
s_ok:
        mov [rdi], rax

        xor r11, r11
p2:
        cmp r11, r9
        je p2끝
        call 낳음
        jmp p2
p2끝:
        mov [r14 + 96], r12
        mov qword ptr [r14 + 104], MEMSZ

        mov rdi, [rsp + 24]
        mov rax, 2
        mov rsi, 577
        mov rdx, 493
        syscall
        cmp rax, 0
        jl die
        mov r8, rax
        mov rax, 1
        mov rdi, r8
        mov rsi, r14
        mov rdx, r12
        syscall
        cmp rax, r12
        jne die
        mov rax, 3
        mov rdi, r8
        syscall
        mov rax, 60
        xor rdi, rdi
        syscall

die:
        mov rax, 1
        mov rdi, 2
        lea rsi, [rip + msg_err]
        mov rdx, msg_err_len
        syscall
        mov rax, 60
        mov rdi, 1
        syscall

머리쓰기:
        mov byte ptr [r14], 0x7f
        mov byte ptr [r14 + 1], 'E'
        mov byte ptr [r14 + 2], 'L'
        mov byte ptr [r14 + 3], 'F'
        mov byte ptr [r14 + 4], 2
        mov byte ptr [r14 + 5], 1
        mov byte ptr [r14 + 6], 1
        mov word ptr [r14 + 16], 2
        mov word ptr [r14 + 18], 62
        mov dword ptr [r14 + 20], 1
        mov rax, BONES_VA + 40
        mov [r14 + 24], rax
        mov qword ptr [r14 + 32], 64
        mov word ptr [r14 + 52], 64
        mov word ptr [r14 + 54], 56
        mov word ptr [r14 + 56], 1
        mov dword ptr [r14 + 64], 1
        mov dword ptr [r14 + 68], 7
        mov qword ptr [r14 + 80], BASE
        mov qword ptr [r14 + 88], BASE
        mov qword ptr [r14 + 112], 0x1000
        mov r12, HDR
        ret

넣기8:
        mov [r14 + r12], al
        inc r12
        ret
넣기32:
        mov [r14 + r12], eax
        add r12, 4
        ret
넣기64:
        mov [r14 + r12], rax
        add r12, 8
        ret

상대32:
        mov rax, BASE
        add rax, r12
        add rax, 4
        sub rdi, rax
        mov eax, edi
        jmp 넣기32

# rdx = 바이트코드 오프 → rdi = 기계 가상주소
목표:
        mov eax, [rbx + rdx*4]
        add rax, BONES_VA
        add rax, 뼈길이
        mov rdi, rax
        ret

점프실패:
        B 0x0f
        B 0x84
        mov rdi, [rip + 뼈물 + 8]
        jmp 상대32

점프끝:
        B 0x0f
        B 0x84
        mov rdi, [rip + 뼈물 + 32]
        jmp 상대32

# ========== 크기 ==========
크기:
        movzx eax, byte ptr [r15 + r11]
        inc r11
        lea rcx, [rip + 크기타]
        cmp eax, 30
        ja die
        jmp qword ptr [rcx + rax*8]

        .align 8
크기타:
        .quad 크기0,크기1,크기2,크기3,크기4,크기4,크기6,크기7,크기8,크기9
        .quad 크기10,크기11,크기12,크기13,크기14,크기14,크기14,크기17,크기18,크기19
        .quad 크기20,크기21,크기22,크기23,크기24,크기25,크기26,크기27,크기28,크기29,크기30

크기0:  mov rax, 9; ret
크기1:  add r11, 8; mov rax, 17; ret
크기2:  mov rax, 39; ret
크기3:  mov ecx, [r15+r11]; add r11, 4; add r11, rcx; mov rax, 58; add rax, rcx; ret
크기4:  mov rax, 24; ret
크기6:  mov rax, 25; ret
크기7:  mov rax, 35; ret
크기8:  mov rax, 12; ret
크기9:  mov rax, 3; ret
크기10: add r11, 4; mov rax, 5; ret
크기11: add r11, 4; mov rax, 17; ret
크기12: inc r11; mov rax, 21; ret
크기13: inc r11; mov rax, 21; ret
크기14: mov rax, 29; ret
크기17: mov rax, 20; ret
크기18: mov rax, 38; ret
크기19: add r11, 4; mov rax, 58; ret
크기20: mov rax, 40; ret
크기21: mov ecx, [r15+r11]; add r11, 4; add r11, rcx; mov rax, 46; add rax, rcx; ret
크기22: mov ecx, [r15+r11]; add r11, 4; add r11, rcx; mov rax, 54; add rax, rcx; ret
크기23: mov rax, 57; ret
크기24: mov rax, 43; ret
크기25: mov rax, 14; ret
크기26: mov rax, 5; ret
크기27: mov rax, 69; ret
크기28: mov rax, 77; ret
크기29: mov rax, 40; ret
크기30: mov rax, 45; ret

# ========== 낳음 ==========
낳음:
        movzx eax, byte ptr [r15 + r11]
        inc r11
        lea rcx, [rip + 낳기타]
        cmp eax, 30
        ja die
        jmp qword ptr [rcx + rax*8]

        .align 8
낳기타:
        .quad e0,e1,e2,e3,e4,e5,e6,e7,e8,e9
        .quad e10,e11,e12,e13,e14,e15,e16,e17,e18,e19
        .quad e20,e21,e22,e23,e24,e25,e26,e27,e28,e29,e30

e0:
        B 0xb8
        mov eax, 60
        call 넣기32
        B 0x31
        B 0xff
        B 0x0f
        B 0x05
        ret

e1:
        mov rax, [r15 + r11]
        add r11, 8
        push rax
        B 0x48
        B 0xb8
        pop rax
        call 넣기64
        B 0x4b
        B 0x89
        B 0x04
        B 0xef
        B 0x49
        B 0xff
        B 0xc5
        ret

e2:
        B 0x49
        B 0xff
        B 0xcd
        B 0x4b
        B 0x8b
        B 0x04
        B 0xef
        B 0xe8
        mov rdi, [rip + 뼈물]
        call 상대32
        B 0xb8
        mov eax, 1
        call 넣기32
        B 0xbf
        mov eax, 1
        call 넣기32
        B 0x48
        B 0xbe
        mov rax, [rip + 뼈물 + 16]
        call 넣기64
        B 0xba
        mov eax, 1
        call 넣기32
        B 0x0f
        B 0x05
        ret

e3:
        mov ecx, [r15 + r11]
        add r11, 4
        push rcx
        B 0xb8
        mov eax, 1
        call 넣기32
        B 0xbf
        mov eax, 1
        call 넣기32
        B 0x48
        B 0x8d
        B 0x35
        mov eax, 41
        call 넣기32
        B 0x48
        B 0xc7
        B 0xc2
        pop rax
        push rax
        call 넣기32
        B 0x0f
        B 0x05
        B 0xb8
        mov eax, 1
        call 넣기32
        B 0xbf
        mov eax, 1
        call 넣기32
        B 0x48
        B 0xbe
        mov rax, [rip + 뼈물 + 16]
        call 넣기64
        B 0xba
        mov eax, 1
        call 넣기32
        B 0x0f
        B 0x05
        B 0xe9
        pop rax
        call 넣기32
        mov ecx, eax
        lea rsi, [r15 + r11]
        lea rdi, [r14 + r12]
        push rcx
        rep movsb
        pop rcx
        add r12, rcx
        add r11, rcx
        ret

쌍꺼냄:
        B 0x49
        B 0xff
        B 0xcd
        B 0x4b
        B 0x8b
        B 0x0c
        B 0xef
        B 0x49
        B 0xff
        B 0xcd
        B 0x4b
        B 0x8b
        B 0x04
        B 0xef
        ret
쌍넣음:
        B 0x4b
        B 0x89
        B 0x04
        B 0xef
        B 0x49
        B 0xff
        B 0xc5
        ret

e4:
        call 쌍꺼냄
        B 0x48
        B 0x01
        B 0xc8
        jmp 쌍넣음
e5:
        call 쌍꺼냄
        B 0x48
        B 0x29
        B 0xc8
        jmp 쌍넣음
e6:
        call 쌍꺼냄
        B 0x48
        B 0x0f
        B 0xaf
        B 0xc1
        jmp 쌍넣음

나눔몸:
        call 쌍꺼냄
        B 0x48
        B 0x85
        B 0xc9
        call 점프실패
        B 0x48
        B 0x99
        B 0x48
        B 0xf7
        B 0xf9
        ret
e7:
        call 나눔몸
        jmp 쌍넣음
e18:
        call 나눔몸
        B 0x48
        B 0x89
        B 0xd0
        jmp 쌍넣음

e8:
        B 0x4b
        B 0x8b
        B 0x44
        B 0xef
        B 0xf8
        B 0x4b
        B 0x89
        B 0x04
        B 0xef
        B 0x49
        B 0xff
        B 0xc5
        ret
e9:
        B 0x49
        B 0xff
        B 0xcd
        ret

e10:
        movsx rax, dword ptr [r15 + r11]
        add r11, 4
        mov rdx, r11
        add rdx, rax
        call 목표
        B 0xe9
        jmp 상대32

e11:
        movsx rax, dword ptr [r15 + r11]
        add r11, 4
        push rax
        B 0x49
        B 0xff
        B 0xcd
        B 0x4b
        B 0x8b
        B 0x04
        B 0xef
        B 0x48
        B 0x85
        B 0xc0
        B 0x75
        B 0x05
        pop rax
        mov rdx, r11
        add rdx, rax
        call 목표
        B 0xe9
        jmp 상대32

e12:
        movzx ecx, byte ptr [r15 + r11]
        inc r11
        B 0x48
        B 0xc7
        B 0xc1
        mov eax, ecx
        call 넣기32
        B 0x48
        B 0x01
        B 0xe9
        B 0x48
        B 0x8b
        B 0x04
        B 0xcb
        B 0x4b
        B 0x89
        B 0x04
        B 0xef
        B 0x49
        B 0xff
        B 0xc5
        ret

e13:
        movzx ecx, byte ptr [r15 + r11]
        inc r11
        B 0x49
        B 0xff
        B 0xcd
        B 0x4b
        B 0x8b
        B 0x04
        B 0xef
        B 0x48
        B 0xc7
        B 0xc1
        mov eax, ecx
        call 넣기32
        B 0x48
        B 0x01
        B 0xe9
        B 0x48
        B 0x89
        B 0x04
        B 0xcb
        ret

비교몸:
        call 쌍꺼냄
        B 0x31
        B 0xd2
        B 0x48
        B 0x39
        B 0xc8
        ret
e14:
        call 비교몸
        B 0x0f
        B 0x94
        B 0xc2
        B 0x4b
        B 0x89
        B 0x14
        B 0xef
        B 0x49
        B 0xff
        B 0xc5
        ret
e15:
        call 비교몸
        B 0x0f
        B 0x9c
        B 0xc2
        B 0x4b
        B 0x89
        B 0x14
        B 0xef
        B 0x49
        B 0xff
        B 0xc5
        ret
e16:
        call 비교몸
        B 0x0f
        B 0x9f
        B 0xc2
        B 0x4b
        B 0x89
        B 0x14
        B 0xef
        B 0x49
        B 0xff
        B 0xc5
        ret

e17:
        B 0x4b
        B 0x8b
        B 0x44
        B 0xef
        B 0xf8
        B 0x48
        B 0x85
        B 0xc0
        B 0x0f
        B 0x94
        B 0xc0
        B 0x48
        B 0x0f
        B 0xb6
        B 0xc0
        B 0x4b
        B 0x89
        B 0x44
        B 0xef
        B 0xf8
        ret

e19:
        movsx rax, dword ptr [r15 + r11]
        add r11, 4
        push rax
        B 0x49
        B 0x83
        B 0xf8
        B 0x40
        B 0x0f
        B 0x8d
        mov rdi, [rip + 뼈물 + 8]
        call 상대32
        B 0x4c
        B 0x89
        B 0xc1
        B 0x48
        B 0xc1
        B 0xe1
        B 0x04
        B 0x48
        B 0xb8
        mov rax, CSTACK
        call 넣기64
        B 0x48
        B 0x8d
        B 0x15
        mov eax, 24
        call 넣기32
        B 0x48
        B 0x89
        B 0x14
        B 0x08
        B 0x48
        B 0x89
        B 0x6c
        B 0x08
        B 0x08
        B 0x49
        B 0xff
        B 0xc0
        B 0x4c
        B 0x89
        B 0xd5
        B 0x49
        B 0x83
        B 0xc2
        B 0x20
        pop rax
        mov rdx, r11
        add rdx, rax
        call 목표
        B 0xe9
        jmp 상대32

e20:
        B 0x4d
        B 0x85
        B 0xc0
        call 점프끝
        B 0x49
        B 0xff
        B 0xc8
        B 0x4c
        B 0x89
        B 0xc1
        B 0x48
        B 0xc1
        B 0xe1
        B 0x04
        B 0x48
        B 0xb8
        mov rax, CSTACK
        call 넣기64
        B 0x49
        B 0x89
        B 0xea
        B 0x48
        B 0x8b
        B 0x6c
        B 0x08
        B 0x08
        B 0xff
        B 0x24
        B 0x08
        ret

열기공통:
        # rdi already to be set by caller via path
        # this helper unused
        ret

e21:
        mov ecx, [r15 + r11]
        add r11, 4
        push rcx
        B 0xb8
        mov eax, 2
        call 넣기32
        B 0x48
        B 0xbf
        # path vaddr = current + remaining prefix + 5 (jmp)
        # now after these 1+4+1+1 = 7 bytes of this mov rdi
        # remaining: 10(addr)+ 3 xor rsi + 3 xor rdx + 2 sys + 4 cmp + 6 jl + 4 store + 3 inc + 5 jmp = 40
        # wait I'll emit mov rdi, imm64 with computed addr
        mov rax, BASE
        add rax, r12
        add rax, 38
        call 넣기64
        B 0x48
        B 0x31
        B 0xf6
        B 0x48
        B 0x31
        B 0xd2
        B 0x0f
        B 0x05
        B 0x48
        B 0x83
        B 0xf8
        B 0x00
        call 점프실패
        B 0x4b
        B 0x89
        B 0x04
        B 0xef
        B 0x49
        B 0xff
        B 0xc5
        B 0xe9
        pop rax
        push rax
        inc rax
        call 넣기32
        pop rcx
        lea rsi, [r15 + r11]
        lea rdi, [r14 + r12]
        push rcx
        rep movsb
        pop rcx
        add r12, rcx
        add r11, rcx
        B 0x00
        ret

e22:
        mov ecx, [r15 + r11]
        add r11, 4
        push rcx
        B 0xb8
        mov eax, 2
        call 넣기32
        B 0x48
        B 0xbf
        mov rax, BASE
        add rax, r12
        add rax, 46
        call 넣기64
        B 0x48
        B 0xc7
        B 0xc6
        mov eax, 577
        call 넣기32
        B 0x48
        B 0xc7
        B 0xc2
        mov eax, 420
        call 넣기32
        B 0x0f
        B 0x05
        B 0x48
        B 0x83
        B 0xf8
        B 0x00
        call 점프실패
        B 0x4b
        B 0x89
        B 0x04
        B 0xef
        B 0x49
        B 0xff
        B 0xc5
        B 0xe9
        pop rax
        push rax
        inc rax
        call 넣기32
        pop rcx
        lea rsi, [r15 + r11]
        lea rdi, [r14 + r12]
        push rcx
        rep movsb
        pop rcx
        add r12, rcx
        add r11, rcx
        B 0x00
        ret

e23:
        B 0x49
        B 0xff
        B 0xcd
        B 0x4b
        B 0x8b
        B 0x3c
        B 0xef
        B 0x31
        B 0xc0
        B 0x48
        B 0xbe
        mov rax, ONEBYTE
        call 넣기64
        B 0xba
        mov eax, 1
        call 넣기32
        B 0x0f
        B 0x05
        B 0x48
        B 0x83
        B 0xf8
        B 0x01
        B 0x74
        B 0x09
        B 0x48
        B 0xc7
        B 0xc0
        mov eax, -1
        call 넣기32
        B 0xeb
        B 0x09
        B 0x48
        B 0x0f
        B 0xb6
        B 0x04
        B 0x25
        mov eax, ONEBYTE
        call 넣기32
        B 0x4b
        B 0x89
        B 0x04
        B 0xef
        B 0x49
        B 0xff
        B 0xc5
        ret

e24:
        B 0x49
        B 0xff
        B 0xcd
        B 0x4b
        B 0x8b
        B 0x04
        B 0xef
        B 0x88
        B 0x04
        B 0x25
        mov eax, ONEBYTE
        call 넣기32
        B 0x49
        B 0xff
        B 0xcd
        B 0x4b
        B 0x8b
        B 0x3c
        B 0xef
        B 0xb8
        mov eax, 1
        call 넣기32
        B 0x48
        B 0xbe
        mov rax, ONEBYTE
        call 넣기64
        B 0xba
        mov eax, 1
        call 넣기32
        B 0x0f
        B 0x05
        ret

e25:
        B 0x49
        B 0xff
        B 0xcd
        B 0x4b
        B 0x8b
        B 0x3c
        B 0xef
        B 0xb8
        mov eax, 3
        call 넣기32
        B 0x0f
        B 0x05
        ret

e26:
        B 0xe9
        mov rdi, [rip + 뼈물 + 8]
        jmp 상대32

# 인수열기: argv[n+1]  (기계어는 가상+2가 아님)
e27:
        B 0x49
        B 0xff
        B 0xcd
        B 0x4b
        B 0x8b
        B 0x04
        B 0xef
        B 0x48
        B 0xff
        B 0xc0
        B 0x48
        B 0x8b
        B 0x0c
        B 0x25
        mov eax, ARGC
        call 넣기32
        B 0x48
        B 0x39
        B 0xc8
        B 0x0f
        B 0x83
        mov rdi, [rip + 뼈물 + 8]
        call 상대32
        B 0x48
        B 0x8b
        B 0x14
        B 0x25
        mov eax, ARGV
        call 넣기32
        B 0x48
        B 0x8b
        B 0x3c
        B 0xc2
        B 0xb8
        mov eax, 2
        call 넣기32
        B 0x48
        B 0x31
        B 0xf6
        B 0x48
        B 0x31
        B 0xd2
        B 0x0f
        B 0x05
        B 0x48
        B 0x83
        B 0xf8
        B 0x00
        call 점프실패
        B 0x4b
        B 0x89
        B 0x04
        B 0xef
        B 0x49
        B 0xff
        B 0xc5
        ret

e28:
        B 0x49
        B 0xff
        B 0xcd
        B 0x4b
        B 0x8b
        B 0x04
        B 0xef
        B 0x48
        B 0xff
        B 0xc0
        B 0x48
        B 0x8b
        B 0x0c
        B 0x25
        mov eax, ARGC
        call 넣기32
        B 0x48
        B 0x39
        B 0xc8
        B 0x0f
        B 0x83
        mov rdi, [rip + 뼈물 + 8]
        call 상대32
        B 0x48
        B 0x8b
        B 0x14
        B 0x25
        mov eax, ARGV
        call 넣기32
        B 0x48
        B 0x8b
        B 0x3c
        B 0xc2
        B 0xb8
        mov eax, 2
        call 넣기32
        B 0x48
        B 0xc7
        B 0xc6
        mov eax, 577
        call 넣기32
        B 0x48
        B 0xc7
        B 0xc2
        mov eax, 420
        call 넣기32
        B 0x0f
        B 0x05
        B 0x48
        B 0x83
        B 0xf8
        B 0x00
        call 점프실패
        B 0x4b
        B 0x89
        B 0x04
        B 0xef
        B 0x49
        B 0xff
        B 0xc5
        ret

e29:
        B 0x49
        B 0xff
        B 0xcd
        B 0x4b
        B 0x8b
        B 0x04
        B 0xef
        B 0x48
        B 0x3d
        mov eax, 262144
        call 넣기32
        B 0x0f
        B 0x83
        mov rdi, [rip + 뼈물 + 8]
        call 상대32
        B 0x48
        B 0xba
        mov rax, HEAP
        call 넣기64
        B 0x0f
        B 0xb6
        B 0x04
        B 0x02
        B 0x4b
        B 0x89
        B 0x04
        B 0xef
        B 0x49
        B 0xff
        B 0xc5
        ret

e30:
        B 0x49
        B 0xff
        B 0xcd
        B 0x4b
        B 0x8b
        B 0x0c
        B 0xef
        B 0x49
        B 0xff
        B 0xcd
        B 0x4b
        B 0x8b
        B 0x04
        B 0xef
        B 0x48
        B 0x3d
        mov eax, 262144
        call 넣기32
        B 0x0f
        B 0x83
        mov rdi, [rip + 뼈물 + 8]
        call 상대32
        B 0x81
        B 0xe1
        mov eax, 255
        call 넣기32
        B 0x48
        B 0xba
        mov rax, HEAP
        call 넣기64
        B 0x88
        B 0x0c
        B 0x02
        ret
