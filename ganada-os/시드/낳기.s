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
        .equ HEAPCUR,  0x54D440
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
한인표: .space 65536
진입표: .space 65536
슬롯표: .space 65536
대상:   .space 2048
대상수: .quad 0
얇은중: .quad 0
얇슬롯: .quad 0

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
        call 분석
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
        call 넣기32
        ret

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

점프음수:
        B 0x0f
        B 0x8c
        mov rdi, [rip + 뼈물 + 8]
        jmp 상대32

점프초과:
        B 0x0f
        B 0x87
        mov rdi, [rip + 뼈물 + 8]
        jmp 상대32

점프이하:
        B 0x0f
        B 0x86
        mov rdi, [rip + 뼈물 + 8]
        jmp 상대32

점프넘침:
        B 0x0f
        B 0x80
        mov rdi, [rip + 뼈물 + 8]
        jmp 상대32

# IR 한 명령을 건너뛴다. r11 만 움직인다.
건너뛰기:
        cmp r11, r9
        jae die
        movzx eax, byte ptr [r15 + r11]
        inc r11
        cmp eax, 1
        je 건너8
        cmp eax, 3
        je 건너문
        cmp eax, 10
        je 건너4
        cmp eax, 11
        je 건너4
        cmp eax, 12
        je 건너1
        cmp eax, 13
        je 건너1
        cmp eax, 19
        je 건너4
        cmp eax, 21
        je 건너문
        cmp eax, 22
        je 건너문
        ret
건너8:
        add r11, 8
        ret
건너4:
        add r11, 4
        ret
건너1:
        inc r11
        ret
건너문:
        cmp r11, r9
        jae die
        mov ecx, [r15 + r11]
        add r11, 4
        add r11, rcx
        ret

대상넣기:
        # rcx = 목표 IR 오프. 중복 없이 대상[] 에 넣는다.
        cmp rcx, r9
        jae die
        mov rdi, [rip + 대상수]
        xor esi, esi
대겹:
        cmp rsi, rdi
        je 대새
        lea rdx, [rip + 대상]
        cmp qword ptr [rdx + rsi*8], rcx
        je 대끝
        inc rsi
        jmp 대겹
대새:
        cmp rdi, 256
        jae die
        lea rdx, [rip + 대상]
        mov [rdx + rdi*8], rcx
        inc rdi
        mov [rip + 대상수], rdi
대끝:
        ret

# 0·1인자이고 칸이 인자뿐인 묶음 → 얇은 부르기 (rdi/r9/rax, 막 없음)
분석:
        xor r11, r11
        mov qword ptr [rip + 대상수], 0
분수집:
        cmp r11, r9
        jae 분수집끝
        movzx eax, byte ptr [r15 + r11]
        cmp eax, 19
        jne 분다
        movsx rcx, dword ptr [r15 + r11 + 1]
        add rcx, r11
        add rcx, 5
        call 대상넣기
분다:
        call 건너뛰기
        jmp 분수집
분수집끝:
        # 삽입 정렬
        mov rdi, [rip + 대상수]
        xor esi, esi
분정:
        inc rsi
        cmp rsi, rdi
        jae 분정끝
        lea rdx, [rip + 대상]
        mov rax, [rdx + rsi*8]
        mov rcx, rsi
분옮:
        test rcx, rcx
        jz 분넣
        cmp [rdx + rcx*8 - 8], rax
        jbe 분넣
        mov r8, [rdx + rcx*8 - 8]
        mov [rdx + rcx*8], r8
        dec rcx
        jmp 분옮
분넣:
        mov [rdx + rcx*8], rax
        jmp 분정
분정끝:
        xor r10, r10
분각:
        mov rdi, [rip + 대상수]
        cmp r10, rdi
        jae 분끝
        lea rdx, [rip + 대상]
        mov r12, [rdx + r10*8]
        inc r10
        cmp r10, rdi
        jae 분마지막
        mov r13, [rdx + r10*8]
        jmp 분몸
분마지막:
        mov r13, r9
분몸:
        # r12=시작 r13=끝. 선행 13 개수 = 인자
        mov r11, r12
        xor r8, r8
        mov r14, 255
분인:
        cmp r11, r13
        jae 분인끝
        cmp byte ptr [r15 + r11], 13
        jne 분인끝
        cmp r8, 8
        jae 분인끝
        movzx eax, byte ptr [r15 + r11 + 1]
        inc r8
        cmp r8, 1
        jne 분인다음
        mov r14, rax
분인다음:
        add r11, 2
        jmp 분인
분인끝:
        cmp r8, 1
        ja 분각
        # 0인자: r14=255. 1인자: r14=슬롯
        # 본문에 다른 칸이 있으면 두껍다
        mov r11, r12
분칸:
        cmp r11, r13
        jae 분얇
        movzx eax, byte ptr [r15 + r11]
        cmp eax, 12
        je 분칸검
        cmp eax, 13
        je 분칸검
        call 건너뛰기
        jmp 분칸
분칸검:
        movzx ecx, byte ptr [r15 + r11 + 1]
        cmp r8, 0
        je 분각
        cmp rcx, r14
        jne 분각
        call 건너뛰기
        jmp 분칸
분얇:
        lea rdi, [rip + 진입표]
        mov byte ptr [rdi + r12], 1
        lea rdi, [rip + 슬롯표]
        mov byte ptr [rdi + r12], r14b
        mov rsi, r12
분표:
        cmp rsi, r13
        jae 분각
        lea rdi, [rip + 한인표]
        mov byte ptr [rdi + rsi], 1
        inc rsi
        jmp 분표
분끝:
        ret

얇생신:
        cmp r11, 65536
        jae die
        lea rdi, [rip + 한인표]
        cmp byte ptr [rdi + r11], 0
        jne 얇켜
        mov qword ptr [rip + 얇은중], 0
        ret
얇켜:
        lea rdi, [rip + 진입표]
        cmp byte ptr [rdi + r11], 0
        je 얇유지
        mov qword ptr [rip + 얇은중], 1
        lea rdi, [rip + 슬롯표]
        movzx eax, byte ptr [rdi + r11]
        mov [rip + 얇슬롯], rax
얇유지:
        ret

# ========== 크기 ==========
크기:
        call 얇생신
        movzx eax, byte ptr [r15 + r11]
        inc r11
        lea rcx, [rip + 크기타]
        cmp eax, 40
        ja die
        jmp qword ptr [rcx + rax*8]

        .align 8
크기타:
        .quad 크기0,크기1,크기2,크기3,크기4,크기5,크기6,크기7,크기8,크기9
        .quad 크기10,크기11,크기12,크기13,크기14,크기14,크기14,크기17,크기18,크기19
        .quad 크기20,크기21,크기22,크기23,크기24,크기25,크기26,크기27,크기28,크기29,크기30
        .quad 크기31,크기31,크기31,크기34,크기35,크기35,크기37,크기38,크기38,크기40

크기0:  mov rax, 9; ret
크기1:  add r11, 8; mov rax, 17; ret
크기2:  mov rax, 39; ret
크기3:  mov ecx, [r15+r11]; add r11, 4; add r11, rcx; mov rax, 58; add rax, rcx; ret
크기4:  mov rax, 18; ret
크기5:  mov rax, 18; ret
크기6:  mov rax, 27; ret
크기7:  mov rax, 35; ret
크기8:  mov rax, 12; ret
크기9:  mov rax, 3; ret
크기10: add r11, 4; mov rax, 5; ret
크기11: add r11, 4; mov rax, 17; ret
크기31: mov rax, 12; ret
크기12:
        movzx ecx, byte ptr [r15 + r11]
        inc r11
        cmp qword ptr [rip + 얇은중], 0
        je 크기12두
        cmp rcx, [rip + 얇슬롯]
        je 크기12얇
크기12두:
        mov rdx, r9
        sub rdx, r11
        cmp rdx, 5
        jb 크기12일반
        cmp byte ptr [r15 + r11], 12
        jne 크기12즉시
        cmp byte ptr [r15 + r11 + 2], 4
        jne 크기12일반
        cmp byte ptr [r15 + r11 + 3], 13
        jne 크기12일반
        cmp byte ptr [r15 + r11 + 4], cl
        jne 크기12일반
        add r11, 5
        mov rax, 22
        ret
크기12즉시:
        cmp byte ptr [r15 + r11], 1
        jne 크기12일반
        cmp rdx, 41
        jb 크기12수비교
        cmp byte ptr [r15 + r11 + 9], 15
        jne 크기12수비교
        cmp byte ptr [r15 + r11 + 10], 11
        jne 크기12수비교
        cmp byte ptr [r15 + r11 + 15], 12
        jne 크기12수비교
        cmp byte ptr [r15 + r11 + 17], 12
        jne 크기12수비교
        cmp byte ptr [r15 + r11 + 18], cl
        jne 크기12수비교
        cmp byte ptr [r15 + r11 + 19], 4
        jne 크기12수비교
        cmp byte ptr [r15 + r11 + 20], 13
        jne 크기12수비교
        mov al, [r15 + r11 + 16]
        cmp al, cl
        je 크기12수비교
        cmp byte ptr [r15 + r11 + 21], al
        jne 크기12수비교
        cmp byte ptr [r15 + r11 + 22], 12
        jne 크기12수비교
        cmp byte ptr [r15 + r11 + 23], cl
        jne 크기12수비교
        cmp byte ptr [r15 + r11 + 24], 1
        jne 크기12수비교
        cmp qword ptr [r15 + r11 + 25], 1
        jne 크기12수비교
        cmp byte ptr [r15 + r11 + 33], 4
        jne 크기12수비교
        cmp byte ptr [r15 + r11 + 34], 13
        jne 크기12수비교
        cmp byte ptr [r15 + r11 + 35], cl
        jne 크기12수비교
        cmp byte ptr [r15 + r11 + 36], 10
        jne 크기12수비교
        mov rax, [r15 + r11 + 1]
        movsxd rdx, eax
        cmp rdx, rax
        jne 크기12수비교
        add r11, 41
        mov rax, 65
        ret
크기12수비교:
        mov rdx, r9
        sub rdx, r11
        cmp rdx, 12
        jb 크기12일반
        cmp byte ptr [r15 + r11 + 9], 4
        jne 크기12점프
        cmp byte ptr [r15 + r11 + 10], 13
        jne 크기12점프
        cmp byte ptr [r15 + r11 + 11], cl
        jne 크기12점프
        mov rax, [r15 + r11 + 1]
        add r11, 12
        cmp rax, 1
        jne 크기12수32
        mov rax, 8
        ret
크기12수32:
        movsxd rdx, eax
        cmp rdx, rax
        jne 크기12수64
        mov rax, 18
        ret
크기12수64:
        mov rax, 24
        ret
크기12점프:
        cmp rdx, 15
        jb 크기12식
        mov al, [r15 + r11 + 9]
        cmp al, 14
        je 크기12점프맞
        cmp al, 15
        je 크기12점프맞
        cmp al, 16
        jne 크기12식
크기12점프맞:
        cmp byte ptr [r15 + r11 + 10], 11
        jne 크기12식
        mov rax, [r15 + r11 + 1]
        movsxd rdx, eax
        cmp rdx, rax
        jne 크기12식
        add r11, 15
        mov rax, 18
        ret
크기12식:
        mov rdx, r9
        sub rdx, r11
        cmp rdx, 10
        jb 크기12일반
        mov al, [r15 + r11 + 9]
        cmp al, 4
        je 크기12식맞
        cmp al, 5
        jne 크기12일반
크기12식맞:
        mov rax, [r15 + r11 + 1]
        movsx rdx, al
        cmp rdx, rax
        jne 크기12일반
        add r11, 10
        mov rax, 19
        ret
크기12일반:
        mov rax, 15
        ret
크기12얇:
        cmp byte ptr [r15 + r11], 20
        jne 크기12얇즉
        inc r11
        mov rax, 4
        ret
크기12얇즉:
        cmp byte ptr [r15 + r11], 1
        jne 크기12얇일반
        mov rdx, r9
        sub rdx, r11
        cmp rdx, 34
        jb 크기12얇부단
        mov al, [r15 + r11 + 9]
        cmp al, 4
        je 크기12얇쌍검
        cmp al, 5
        jne 크기12얇부단
크기12얇쌍검:
        cmp byte ptr [r15 + r11 + 10], 19
        jne 크기12얇부단
        cmp byte ptr [r15 + r11 + 15], 12
        jne 크기12얇부단
        cmp byte ptr [r15 + r11 + 16], cl
        jne 크기12얇부단
        cmp byte ptr [r15 + r11 + 17], 1
        jne 크기12얇부단
        mov al, [r15 + r11 + 26]
        cmp al, 4
        je 크기12얇쌍검2
        cmp al, 5
        jne 크기12얇부단
크기12얇쌍검2:
        cmp byte ptr [r15 + r11 + 27], 19
        jne 크기12얇부단
        cmp byte ptr [r15 + r11 + 32], 4
        jne 크기12얇부단
        cmp byte ptr [r15 + r11 + 33], 20
        jne 크기12얇부단
        mov rax, [r15 + r11 + 1]
        movsx rdx, al
        cmp rdx, rax
        jne 크기12얇부단
        mov rax, [r15 + r11 + 18]
        movsx rdx, al
        cmp rdx, rax
        jne 크기12얇부단
        movsx rax, dword ptr [r15 + r11 + 11]
        lea rdx, [r11 + 15]
        add rdx, rax
        cmp rdx, r9
        jae 크기12얇부단
        lea rsi, [rip + 진입표]
        cmp byte ptr [rsi + rdx], 1
        jne 크기12얇부단
        movsx rax, dword ptr [r15 + r11 + 28]
        lea rdx, [r11 + 32]
        add rdx, rax
        cmp rdx, r9
        jae 크기12얇부단
        cmp byte ptr [rsi + rdx], 1
        jne 크기12얇부단
        add r11, 34
        mov rax, 33
        ret
크기12얇부단:
        mov rdx, r9
        sub rdx, r11
        cmp rdx, 15
        jb 크기12얇더
        mov al, [r15 + r11 + 9]
        cmp al, 4
        je 크기12얇부
        cmp al, 5
        jne 크기12얇비
크기12얇부:
        cmp byte ptr [r15 + r11 + 10], 19
        jne 크기12얇비
        mov rax, [r15 + r11 + 1]
        movsx rdx, al
        cmp rdx, rax
        jne 크기12얇비
        movsx rax, dword ptr [r15 + r11 + 11]
        lea rdx, [r11 + 15]
        add rdx, rax
        cmp rdx, r9
        jae 크기12얇비
        lea rsi, [rip + 진입표]
        cmp byte ptr [rsi + rdx], 1
        jne 크기12얇비
        add r11, 15
        mov rax, 20
        ret
크기12얇비:
        mov rdx, r9
        sub rdx, r11
        cmp rdx, 15
        jb 크기12얇더
        mov al, [r15 + r11 + 9]
        cmp al, 14
        je 크기12얇점
        cmp al, 15
        je 크기12얇점
        cmp al, 16
        jne 크기12얇더
크기12얇점:
        cmp byte ptr [r15 + r11 + 10], 11
        jne 크기12얇더
        mov rax, [r15 + r11 + 1]
        movsxd rdx, eax
        cmp rdx, rax
        jne 크기12얇더
        add r11, 15
        mov rax, 13
        ret
크기12얇더:
        mov rdx, r9
        sub rdx, r11
        cmp rdx, 12
        jb 크기12얇식
        cmp byte ptr [r15 + r11 + 9], 4
        jne 크기12얇식
        cmp byte ptr [r15 + r11 + 10], 13
        jne 크기12얇식
        cmp byte ptr [r15 + r11 + 11], cl
        jne 크기12얇식
        mov rax, [r15 + r11 + 1]
        add r11, 12
        cmp rax, 1
        jne 크기12얇32
        mov rax, 3
        ret
크기12얇32:
        movsxd rdx, eax
        cmp rdx, rax
        jne 크기12얇64
        mov rax, 13
        ret
크기12얇64:
        mov rax, 19
        ret
크기12얇식:
        mov rdx, r9
        sub rdx, r11
        cmp rdx, 10
        jb 크기12얇일반
        mov al, [r15 + r11 + 9]
        cmp al, 4
        je 크기12얇식맞
        cmp al, 5
        jne 크기12얇일반
크기12얇식맞:
        mov rax, [r15 + r11 + 1]
        movsx rdx, al
        cmp rdx, rax
        jne 크기12얇일반
        add r11, 10
        mov rax, 11
        ret
크기12얇일반:
        mov rax, 7
        ret
크기13:
        movzx ecx, byte ptr [r15 + r11]
        inc r11
        lea rax, [r11 - 2]
        lea rsi, [rip + 진입표]
        cmp byte ptr [rsi + rax], 1
        jne 크기13칸
        mov rax, 3
        ret
크기13칸:
        cmp qword ptr [rip + 얇은중], 0
        je 크기13두
        cmp rcx, [rip + 얇슬롯]
        jne 크기13두
        mov rax, 7
        ret
크기13두:
        mov rax, 15
        ret
크기14: mov rax, 29; ret
크기17: mov rax, 20; ret
크기18: mov rax, 38; ret
크기19:
        movsx rax, dword ptr [r15 + r11]
        add r11, 4
        mov rdx, r11
        add rdx, rax
        cmp rdx, r9
        jae 크기19두
        lea rsi, [rip + 진입표]
        cmp byte ptr [rsi + rdx], 1
        jne 크기19두
        lea rsi, [rip + 슬롯표]
        cmp byte ptr [rsi + rdx], 255
        je 크기19영
        mov rax, 23
        ret
크기19영:
        mov rax, 12
        ret
크기19두:
        mov rax, 41
        ret
크기20:
        mov rax, 8
        ret
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
크기34: mov rax, 5; ret
크기35: mov rax, 16; ret
크기37: mov rax, 88; ret
크기38: mov rax, 68; ret
크기40: mov rax, 40; ret

# ========== 낳음 ==========
낳음:
        call 얇생신
        movzx eax, byte ptr [r15 + r11]
        inc r11
        lea rcx, [rip + 낳기타]
        cmp eax, 40
        ja die
        jmp qword ptr [rcx + rax*8]

        .align 8
낳기타:
        .quad e0,e1,e2,e3,e4,e5,e6,e7,e8,e9
        .quad e10,e11,e12,e13,e14,e15,e16,e17,e18,e19
        .quad e20,e21,e22,e23,e24,e25,e26,e27,e28,e29,e30
        .quad e31,e32,e33,e34,e35,e36,e37,e38,e39,e40

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
        B 0x49
        B 0xff
        B 0xcd
        B 0x4b
        B 0x8b
        B 0x0c
        B 0xef
        B 0x4b
        B 0x01
        B 0x4c
        B 0xef
        B 0xf8
        jmp 점프넘침
e5:
        B 0x49
        B 0xff
        B 0xcd
        B 0x4b
        B 0x8b
        B 0x0c
        B 0xef
        B 0x4b
        B 0x29
        B 0x4c
        B 0xef
        B 0xf8
        jmp 점프넘침
e6:
        B 0x49
        B 0xff
        B 0xcd
        B 0x4b
        B 0x8b
        B 0x0c
        B 0xef
        B 0x4b
        B 0x8b
        B 0x44
        B 0xef
        B 0xf8
        B 0x48
        B 0x0f
        B 0xaf
        B 0xc1
        B 0x4b
        B 0x89
        B 0x44
        B 0xef
        B 0xf8
        jmp 점프넘침

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
        cmp qword ptr [rip + 얇은중], 0
        je e12두
        cmp rcx, [rip + 얇슬롯]
        je e12얇
e12두:
        mov rdx, r9
        sub rdx, r11
        cmp rdx, 5
        jb e12일반
        cmp byte ptr [r15 + r11], 12
        jne e12즉시
        cmp byte ptr [r15 + r11 + 2], 4
        jne e12일반
        cmp byte ptr [r15 + r11 + 3], 13
        jne e12일반
        cmp byte ptr [r15 + r11 + 4], cl
        jne e12일반
        movzx edx, byte ptr [r15 + r11 + 1]
        add r11, 5
        B 0x48
        B 0x8b
        B 0x84
        B 0xeb
        mov eax, edx
        shl eax, 3
        call 넣기32
        B 0x48
        B 0x01
        B 0x84
        B 0xeb
        mov eax, ecx
        shl eax, 3
        call 넣기32
        jmp 점프넘침
e12즉시:
        cmp byte ptr [r15 + r11], 1
        jne e12일반
        cmp rdx, 41
        jb e12수비교
        cmp byte ptr [r15 + r11 + 9], 15
        jne e12수비교
        cmp byte ptr [r15 + r11 + 10], 11
        jne e12수비교
        cmp byte ptr [r15 + r11 + 15], 12
        jne e12수비교
        cmp byte ptr [r15 + r11 + 17], 12
        jne e12수비교
        cmp byte ptr [r15 + r11 + 18], cl
        jne e12수비교
        cmp byte ptr [r15 + r11 + 19], 4
        jne e12수비교
        cmp byte ptr [r15 + r11 + 20], 13
        jne e12수비교
        mov al, [r15 + r11 + 16]
        cmp al, cl
        je e12수비교
        cmp byte ptr [r15 + r11 + 21], al
        jne e12수비교
        cmp byte ptr [r15 + r11 + 22], 12
        jne e12수비교
        cmp byte ptr [r15 + r11 + 23], cl
        jne e12수비교
        cmp byte ptr [r15 + r11 + 24], 1
        jne e12수비교
        cmp qword ptr [r15 + r11 + 25], 1
        jne e12수비교
        cmp byte ptr [r15 + r11 + 33], 4
        jne e12수비교
        cmp byte ptr [r15 + r11 + 34], 13
        jne e12수비교
        cmp byte ptr [r15 + r11 + 35], cl
        jne e12수비교
        cmp byte ptr [r15 + r11 + 36], 10
        jne e12수비교
        mov rsi, [r15 + r11 + 1]
        movsxd rax, esi
        cmp rax, rsi
        jne e12수비교
        movzx edx, byte ptr [r15 + r11 + 16]
        add r11, 41
        B 0x4c
        B 0x8b
        B 0xa4
        B 0xeb
        mov eax, ecx
        shl eax, 3
        call 넣기32
        B 0x4c
        B 0x8b
        B 0xb4
        B 0xeb
        mov eax, edx
        shl eax, 3
        call 넣기32
        B 0x48
        B 0xc7
        B 0xc1
        mov eax, esi
        call 넣기32
        B 0x49
        B 0x39
        B 0xcc
        B 0x0f
        B 0x8d
        mov eax, 17
        call 넣기32
        B 0x4d
        B 0x01
        B 0xe6
        call 점프넘침
        B 0x49
        B 0xff
        B 0xc4
        B 0xe9
        mov eax, -26
        call 넣기32
        B 0x4c
        B 0x89
        B 0xa4
        B 0xeb
        mov eax, ecx
        shl eax, 3
        call 넣기32
        B 0x4c
        B 0x89
        B 0xb4
        B 0xeb
        mov eax, edx
        shl eax, 3
        call 넣기32
        ret
e12수비교:
        mov rdx, r9
        sub rdx, r11
        cmp rdx, 12
        jb e12일반
        cmp byte ptr [r15 + r11 + 9], 4
        jne e12점프
        cmp byte ptr [r15 + r11 + 10], 13
        jne e12점프
        cmp byte ptr [r15 + r11 + 11], cl
        jne e12점프
        mov rax, [r15 + r11 + 1]
        add r11, 12
        cmp rax, 1
        jne e12수32
        B 0x48
        B 0xff
        B 0x84
        B 0xeb
        mov eax, ecx
        shl eax, 3
        call 넣기32
        ret
e12수32:
        movsxd rdx, eax
        cmp rdx, rax
        jne e12수64
        push rax
        B 0x48
        B 0x81
        B 0x84
        B 0xeb
        mov eax, ecx
        shl eax, 3
        call 넣기32
        pop rax
        call 넣기32
        jmp 점프넘침
e12수64:
        push rax
        B 0x48
        B 0xb8
        pop rax
        call 넣기64
        B 0x48
        B 0x01
        B 0x84
        B 0xeb
        mov eax, ecx
        shl eax, 3
        call 넣기32
        jmp 점프넘침
e12점프:
        cmp rdx, 15
        jb e12식
        mov al, [r15 + r11 + 9]
        cmp al, 14
        je e12점프맞
        cmp al, 15
        je e12점프맞
        cmp al, 16
        jne e12식
e12점프맞:
        cmp byte ptr [r15 + r11 + 10], 11
        jne e12식
        mov r8, [r15 + r11 + 1]
        movsxd rax, r8d
        cmp rax, r8
        jne e12식
        movzx r10d, byte ptr [r15 + r11 + 9]
        add r11, 11
        movsx rax, dword ptr [r15 + r11]
        add r11, 4
        push rax
        B 0x48
        B 0x81
        B 0xbc
        B 0xeb
        mov eax, ecx
        shl eax, 3
        call 넣기32
        mov eax, r8d
        call 넣기32
        B 0x0f
        cmp r10d, 15
        je e12jge
        cmp r10d, 16
        je e12jle
        B 0x85
        jmp e12jcc
e12jge:
        B 0x8d
        jmp e12jcc
e12jle:
        B 0x8e
e12jcc:
        pop rax
        mov rdx, r11
        add rdx, rax
        call 목표
        jmp 상대32
e12식:
        mov rdx, r9
        sub rdx, r11
        cmp rdx, 10
        jb e12일반
        mov al, [r15 + r11 + 9]
        cmp al, 4
        je e12식맞
        cmp al, 5
        jne e12일반
e12식맞:
        mov rsi, [r15 + r11 + 1]
        movsx rax, sil
        cmp rax, rsi
        jne e12일반
        movzx r10d, byte ptr [r15 + r11 + 9]
        add r11, 10
        B 0x48
        B 0x8b
        B 0x84
        B 0xeb
        mov eax, ecx
        shl eax, 3
        call 넣기32
        B 0x48
        B 0x83
        cmp r10d, 5
        je e12식빼
        B 0xc0
        jmp e12식즉
e12식빼:
        B 0xe8
e12식즉:
        mov eax, esi
        call 넣기8
        B 0x4b
        B 0x89
        B 0x04
        B 0xef
        B 0x49
        B 0xff
        B 0xc5
        ret
e12일반:
        B 0x48
        B 0x8b
        B 0x84
        B 0xeb
        mov eax, ecx
        shl eax, 3
        call 넣기32
        B 0x4b
        B 0x89
        B 0x04
        B 0xef
        B 0x49
        B 0xff
        B 0xc5
        ret

e12얇:
        cmp byte ptr [r15 + r11], 20
        jne e12얇즉
        inc r11
        B 0x4c
        B 0x89
        B 0xc8
        B 0xc3
        ret
e12얇즉:
        cmp byte ptr [r15 + r11], 1
        jne e12얇일반
        mov rdx, r9
        sub rdx, r11
        cmp rdx, 34
        jb e12얇부단
        mov al, [r15 + r11 + 9]
        cmp al, 4
        je e12얇쌍1
        cmp al, 5
        jne e12얇부단
e12얇쌍1:
        cmp byte ptr [r15 + r11 + 10], 19
        jne e12얇부단
        cmp byte ptr [r15 + r11 + 15], 12
        jne e12얇부단
        cmp byte ptr [r15 + r11 + 16], cl
        jne e12얇부단
        cmp byte ptr [r15 + r11 + 17], 1
        jne e12얇부단
        mov al, [r15 + r11 + 26]
        cmp al, 4
        je e12얇쌍2
        cmp al, 5
        jne e12얇부단
e12얇쌍2:
        cmp byte ptr [r15 + r11 + 27], 19
        jne e12얇부단
        cmp byte ptr [r15 + r11 + 32], 4
        jne e12얇부단
        cmp byte ptr [r15 + r11 + 33], 20
        jne e12얇부단
        mov rsi, [r15 + r11 + 1]
        movsx rax, sil
        cmp rax, rsi
        jne e12얇부단
        mov rsi, [r15 + r11 + 18]
        movsx rax, sil
        cmp rax, rsi
        jne e12얇부단
        movsx rax, dword ptr [r15 + r11 + 11]
        lea rdx, [r11 + 15]
        add rdx, rax
        cmp rdx, r9
        jae e12얇부단
        lea rdi, [rip + 진입표]
        cmp byte ptr [rdi + rdx], 1
        jne e12얇부단
        movsx rax, dword ptr [r15 + r11 + 28]
        lea rdx, [r11 + 32]
        add rdx, rax
        cmp rdx, r9
        jae e12얇부단
        cmp byte ptr [rdi + rdx], 1
        jne e12얇부단
        mov rsi, [r15 + r11 + 1]
        cmp byte ptr [r15 + r11 + 9], 5
        jne e12얇쌍s1
        neg rsi
e12얇쌍s1:
        mov r8, rsi
        mov rsi, [r15 + r11 + 18]
        cmp byte ptr [r15 + r11 + 26], 5
        jne e12얇쌍s2
        neg rsi
e12얇쌍s2:
        mov r10, rsi
        movsx rax, dword ptr [r15 + r11 + 28]
        lea rdx, [r11 + 32]
        add rdx, rax
        push rdx
        movsx rax, dword ptr [r15 + r11 + 11]
        lea rdx, [r11 + 15]
        add rdx, rax
        push rdx
        add r11, 34
        B 0x49
        B 0x8d
        B 0x79
        mov eax, r8d
        call 넣기8
        B 0x41
        B 0x51
        pop rdx
        call 목표
        B 0xe8
        call 상대32
        B 0x5f
        B 0x50
        B 0x48
        B 0x8d
        B 0x7f
        mov eax, r10d
        call 넣기8
        pop rdx
        call 목표
        B 0xe8
        call 상대32
        B 0x5a
        B 0x48
        B 0x01
        B 0xd0
        call 점프넘침
        B 0xc3
        ret
e12얇부단:
        mov rdx, r9
        sub rdx, r11
        cmp rdx, 15
        jb e12얇더
        mov al, [r15 + r11 + 9]
        cmp al, 4
        je e12얇부
        cmp al, 5
        jne e12얇비
e12얇부:
        cmp byte ptr [r15 + r11 + 10], 19
        jne e12얇비
        mov rsi, [r15 + r11 + 1]
        movsx rax, sil
        cmp rax, rsi
        jne e12얇비
        movsx rax, dword ptr [r15 + r11 + 11]
        lea rdx, [r11 + 15]
        add rdx, rax
        cmp rdx, r9
        jae e12얇비
        lea rdi, [rip + 진입표]
        cmp byte ptr [rdi + rdx], 1
        jne e12얇비
        movzx r10d, byte ptr [r15 + r11 + 9]
        add r11, 11
        movsx rax, dword ptr [r15 + r11]
        add r11, 4
        push rax
        B 0x41
        B 0x51
        B 0x49
        B 0x8d
        B 0x79
        mov rax, rsi
        cmp r10d, 5
        jne e12얇부부
        neg rax
e12얇부부:
        call 넣기8
        pop rax
        mov rdx, r11
        add rdx, rax
        call 목표
        B 0xe8
        call 상대32
        B 0x41
        B 0x59
        B 0x4b
        B 0x89
        B 0x04
        B 0xef
        B 0x49
        B 0xff
        B 0xc5
        ret
e12얇비:
        mov rdx, r9
        sub rdx, r11
        cmp rdx, 15
        jb e12얇더
        mov al, [r15 + r11 + 9]
        cmp al, 14
        je e12얇점
        cmp al, 15
        je e12얇점
        cmp al, 16
        jne e12얇더
e12얇점:
        cmp byte ptr [r15 + r11 + 10], 11
        jne e12얇더
        mov r8, [r15 + r11 + 1]
        movsxd rax, r8d
        cmp rax, r8
        jne e12얇더
        movzx r10d, byte ptr [r15 + r11 + 9]
        add r11, 11
        movsx rax, dword ptr [r15 + r11]
        add r11, 4
        push rax
        B 0x49
        B 0x81
        B 0xf9
        mov eax, r8d
        call 넣기32
        B 0x0f
        cmp r10d, 15
        je e12얇jge
        cmp r10d, 16
        je e12얇jle
        B 0x85
        jmp e12얇jcc
e12얇jge:
        B 0x8d
        jmp e12얇jcc
e12얇jle:
        B 0x8e
e12얇jcc:
        pop rax
        mov rdx, r11
        add rdx, rax
        call 목표
        jmp 상대32
e12얇더:
        mov rdx, r9
        sub rdx, r11
        cmp rdx, 12
        jb e12얇식
        cmp byte ptr [r15 + r11 + 9], 4
        jne e12얇식
        cmp byte ptr [r15 + r11 + 10], 13
        jne e12얇식
        cmp byte ptr [r15 + r11 + 11], cl
        jne e12얇식
        mov rax, [r15 + r11 + 1]
        add r11, 12
        cmp rax, 1
        jne e12얇32
        B 0x49
        B 0xff
        B 0xc1
        ret
e12얇32:
        movsxd rdx, eax
        cmp rdx, rax
        jne e12얇64
        push rax
        B 0x49
        B 0x81
        B 0xc1
        pop rax
        call 넣기32
        jmp 점프넘침
e12얇64:
        push rax
        B 0x48
        B 0xb8
        pop rax
        call 넣기64
        B 0x49
        B 0x01
        B 0xc1
        jmp 점프넘침
e12얇식:
        mov rdx, r9
        sub rdx, r11
        cmp rdx, 10
        jb e12얇일반
        mov al, [r15 + r11 + 9]
        cmp al, 4
        je e12얇식맞
        cmp al, 5
        jne e12얇일반
e12얇식맞:
        mov rsi, [r15 + r11 + 1]
        movsx rax, sil
        cmp rax, rsi
        jne e12얇일반
        movzx r10d, byte ptr [r15 + r11 + 9]
        add r11, 10
        B 0x49
        B 0x8d
        B 0x41
        mov rax, rsi
        cmp r10d, 5
        jne e12얇식부
        neg rax
e12얇식부:
        call 넣기8
        B 0x4b
        B 0x89
        B 0x04
        B 0xef
        B 0x49
        B 0xff
        B 0xc5
        ret
e12얇일반:
        B 0x4f
        B 0x89
        B 0x0c
        B 0xef
        B 0x49
        B 0xff
        B 0xc5
        ret

e13:
        movzx ecx, byte ptr [r15 + r11]
        inc r11
        lea rax, [r11 - 2]
        lea rsi, [rip + 진입표]
        cmp byte ptr [rsi + rax], 1
        jne e13칸
        B 0x49
        B 0x89
        B 0xf9
        ret
e13칸:
        cmp qword ptr [rip + 얇은중], 0
        je e13두
        cmp rcx, [rip + 얇슬롯]
        jne e13두
        B 0x49
        B 0xff
        B 0xcd
        B 0x4f
        B 0x8b
        B 0x0c
        B 0xef
        ret
e13두:
        B 0x49
        B 0xff
        B 0xcd
        B 0x4b
        B 0x8b
        B 0x04
        B 0xef
        B 0x48
        B 0x89
        B 0x84
        B 0xeb
        mov eax, ecx
        shl eax, 3
        call 넣기32
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
        mov rdx, r11
        add rdx, rax
        lea rsi, [rip + 진입표]
        cmp rdx, r9
        jae e19두
        cmp byte ptr [rsi + rdx], 1
        jne e19두
        lea rsi, [rip + 슬롯표]
        cmp byte ptr [rsi + rdx], 255
        je e19영
        B 0x41
        B 0x51
        B 0x49
        B 0xff
        B 0xcd
        B 0x4b
        B 0x8b
        B 0x3c
        B 0xef
        pop rax
        mov rdx, r11
        add rdx, rax
        call 목표
        B 0xe8
        call 상대32
        B 0x41
        B 0x59
        B 0x4b
        B 0x89
        B 0x04
        B 0xef
        B 0x49
        B 0xff
        B 0xc5
        ret
e19영:
        pop rax
        mov rdx, r11
        add rdx, rax
        call 목표
        B 0xe8
        call 상대32
        B 0x4b
        B 0x89
        B 0x04
        B 0xef
        B 0x49
        B 0xff
        B 0xc5
        ret
e19두:
        B 0x49
        B 0x83
        B 0xf8
        B 0x40
        B 0x0f
        B 0x8d
        mov rdi, [rip + 뼈물 + 8]
        call 상대32
        B 0x55
        B 0x4c
        B 0x89
        B 0xd5
        B 0x49
        B 0x83
        B 0xc2
        B 0x20
        B 0x49
        B 0xff
        B 0xc0
        pop rax
        mov rdx, r11
        add rdx, rax
        call 목표
        B 0xe8
        call 상대32
        B 0x49
        B 0xff
        B 0xc8
        B 0x49
        B 0x83
        B 0xea
        B 0x20
        B 0x5d
        B 0x4b
        B 0x89
        B 0x04
        B 0xef
        B 0x49
        B 0xff
        B 0xc5
        ret

e20:
        B 0x49
        B 0xff
        B 0xcd
        B 0x4b
        B 0x8b
        B 0x04
        B 0xef
        B 0xc3
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

e31:
        B 0x49
        B 0xff
        B 0xcd
        B 0x4b
        B 0x8b
        B 0x0c
        B 0xef
        B 0x4b
        B 0x21
        B 0x4c
        B 0xef
        B 0xf8
        ret
e32:
        B 0x49
        B 0xff
        B 0xcd
        B 0x4b
        B 0x8b
        B 0x0c
        B 0xef
        B 0x4b
        B 0x09
        B 0x4c
        B 0xef
        B 0xf8
        ret
e33:
        B 0x49
        B 0xff
        B 0xcd
        B 0x4b
        B 0x8b
        B 0x0c
        B 0xef
        B 0x4b
        B 0x31
        B 0x4c
        B 0xef
        B 0xf8
        ret
e34:
        B 0x4b
        B 0xf7
        B 0x54
        B 0xef
        B 0xf8
        ret
e35:
        B 0x49
        B 0xff
        B 0xcd
        B 0x4b
        B 0x8b
        B 0x0c
        B 0xef
        B 0x48
        B 0x83
        B 0xe1
        B 0x3f
        B 0x4b
        B 0xd3
        B 0x64
        B 0xef
        B 0xf8
        ret
e36:
        B 0x49
        B 0xff
        B 0xcd
        B 0x4b
        B 0x8b
        B 0x0c
        B 0xef
        B 0x48
        B 0x83
        B 0xe1
        B 0x3f
        B 0x4b
        B 0xd3
        B 0x7c
        B 0xef
        B 0xf8
        ret
e37:
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
        call 점프음수
        B 0x48
        B 0x89
        B 0xc1
        B 0x48
        B 0xc1
        B 0xe1
        B 0x03
        B 0x48
        B 0x83
        B 0xc1
        B 0x08
        B 0x48
        B 0xba
        mov rax, HEAPCUR
        call 넣기64
        B 0x48
        B 0x8b
        B 0x32
        B 0x48
        B 0x89
        B 0xf7
        B 0x48
        B 0x01
        B 0xcf
        B 0x48
        B 0x81
        B 0xff
        mov eax, 262144
        call 넣기32
        call 점프초과
        B 0x48
        B 0x89
        B 0x3a
        B 0x48
        B 0xbf
        mov rax, HEAP
        call 넣기64
        B 0x48
        B 0x01
        B 0xf7
        B 0x48
        B 0x89
        B 0x07
        B 0x48
        B 0x89
        B 0xf0
        jmp 쌍넣음
e38:
        call 쌍꺼냄
        B 0x48
        B 0x85
        B 0xc9
        call 점프음수
        B 0x48
        B 0xba
        mov rax, HEAP
        call 넣기64
        B 0x48
        B 0x8b
        B 0x34
        B 0x02
        B 0x48
        B 0x39
        B 0xce
        call 점프이하
        B 0x48
        B 0xc1
        B 0xe1
        B 0x03
        B 0x48
        B 0x83
        B 0xc0
        B 0x08
        B 0x48
        B 0x01
        B 0xc2
        B 0x48
        B 0x8b
        B 0x04
        B 0x0a
        jmp 쌍넣음
e39:
        B 0x49
        B 0xff
        B 0xcd
        B 0x4b
        B 0x8b
        B 0x3c
        B 0xef
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
        B 0x85
        B 0xc9
        call 점프음수
        B 0x48
        B 0xba
        mov rax, HEAP
        call 넣기64
        B 0x48
        B 0x8b
        B 0x34
        B 0x02
        B 0x48
        B 0x39
        B 0xce
        call 점프이하
        B 0x48
        B 0xc1
        B 0xe1
        B 0x03
        B 0x48
        B 0x83
        B 0xc0
        B 0x08
        B 0x48
        B 0x01
        B 0xc2
        B 0x48
        B 0x89
        B 0x3c
        B 0x0a
        ret
e40:
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
        B 0x48
        B 0x8b
        B 0x04
        B 0x02
        jmp 쌍넣음
