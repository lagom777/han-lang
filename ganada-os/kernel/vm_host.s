// ganada-os host VM — aarch64 Apple (macOS)
// NO C. Product path runtime proof. Uses only syscalls (write/exit/open/read/close).
// Build: as -arch arm64 -o vm_host.o vm_host.s && ld -arch arm64 -e _main -o gvm vm_host.o -lSystem -syslibroot $(xcrun --show-sdk-path)
//
// Bytecode file path in argv[1]. See bc/FORMAT.md

.align 3
.data
.msg_usage: .ascii "gvm: usage: gvm file.gbc\n"
.msg_usage_len = . - .msg_usage
.msg_err:   .ascii "gvm: error\n"
.msg_err_len = . - .msg_err
.msg_nl:    .ascii "\n"
.digits:    .ascii "0123456789"

// 64KB code buffer
.comm code_buf, 65536, 3
// stack of i64, 4096 slots
.comm vstack, 32768, 3
// locals 256 * 8
.comm locals, 2048, 3
// print number buffer
.comm numbuf, 32, 3

.text
.global _main
.align 2
_main:
    // x0 = argc, x1 = argv
    mov x19, x0          // argc
    mov x20, x1          // argv
    cmp x19, #2
    b.ge 1f
    // usage
    mov x0, #2           // stderr
    adrp x1, .msg_usage@PAGE
    add x1, x1, .msg_usage@PAGEOFF
    mov x2, #.msg_usage_len
    bl sys_write
    mov x0, #2
    bl sys_exit

1:
    // open argv[1] O_RDONLY=0
    ldr x0, [x20, #8]
    mov x1, #0
    mov x2, #0
    bl sys_open
    cmp x0, #0
    b.lt fail
    mov x21, x0          // fd

    // read into code_buf
    mov x0, x21
    adrp x1, code_buf@PAGE
    add x1, x1, code_buf@PAGEOFF
    mov x2, #65536
    bl sys_read
    cmp x0, #0
    b.le fail
    mov x22, x0          // code_len
    // close
    mov x0, x21
    bl sys_close

    // pc=0, sp=0 (stack index)
    mov x23, #0          // pc
    mov x24, #0          // stack count
    adrp x25, code_buf@PAGE
    add x25, x25, code_buf@PAGEOFF
    adrp x26, vstack@PAGE
    add x26, x26, vstack@PAGEOFF
    adrp x27, locals@PAGE
    add x27, x27, locals@PAGEOFF

// interpreter loop
interp:
    cmp x23, x22
    b.ge halt_ok
    // load opcode
    ldrb w0, [x25, x23]
    add x23, x23, #1
    cmp w0, #0
    b.eq halt_ok
    cmp w0, #1
    b.eq op_pushi
    cmp w0, #2
    b.eq op_printi
    cmp w0, #3
    b.eq op_prints
    cmp w0, #4
    b.eq op_add
    cmp w0, #5
    b.eq op_sub
    cmp w0, #6
    b.eq op_mul
    cmp w0, #7
    b.eq op_div
    cmp w0, #10
    b.eq op_jmp
    cmp w0, #11
    b.eq op_jz
    cmp w0, #12
    b.eq op_load
    cmp w0, #13
    b.eq op_store
    b fail

op_pushi:
    // read i64 le at pc
    add x1, x25, x23
    ldr x0, [x1]
    add x23, x23, #8
    bl push
    b interp

op_printi:
    bl pop
    // x0 = value — print decimal
    bl print_i64
    // newline
    mov x0, #1
    adrp x1, .msg_nl@PAGE
    add x1, x1, .msg_nl@PAGEOFF
    mov x2, #1
    bl sys_write
    b interp

op_prints:
    // u32 len
    add x1, x25, x23
    ldr w2, [x1]         // len
    add x23, x23, #4
    // ptr to bytes
    add x1, x25, x23
    // write stdout
    mov x0, #1
    // x1 already ptr, x2 len
    uxtw x2, w2
    bl sys_write
    add x23, x23, x2
    // newline
    mov x0, #1
    adrp x1, .msg_nl@PAGE
    add x1, x1, .msg_nl@PAGEOFF
    mov x2, #1
    bl sys_write
    b interp

op_add:
    bl pop
    mov x2, x0
    bl pop
    add x0, x0, x2
    bl push
    b interp
op_sub:
    bl pop
    mov x2, x0
    bl pop
    sub x0, x0, x2
    bl push
    b interp
op_mul:
    bl pop
    mov x2, x0
    bl pop
    mul x0, x0, x2
    bl push
    b interp
op_div:
    bl pop
    mov x2, x0
    cbz x2, fail
    bl pop
    sdiv x0, x0, x2
    bl push
    b interp

op_jmp:
    add x1, x25, x23
    ldrsw x2, [x1]       // rel
    add x23, x23, #4
    add x23, x23, x2
    b interp

op_jz:
    add x1, x25, x23
    ldrsw x2, [x1]
    add x23, x23, #4
    bl pop
    cbnz x0, interp
    add x23, x23, x2
    b interp

op_load:
    ldrb w0, [x25, x23]
    add x23, x23, #1
    // locals[slot]
    uxtw x0, w0
    ldr x0, [x27, x0, lsl #3]
    bl push
    b interp

op_store:
    ldrb w1, [x25, x23]
    add x23, x23, #1
    bl pop
    uxtw x1, w1
    str x0, [x27, x1, lsl #3]
    b interp

// push x0
push:
    cmp x24, #4096
    b.ge fail
    str x0, [x26, x24, lsl #3]
    add x24, x24, #1
    ret

// pop -> x0
pop:
    cbz x24, fail
    sub x24, x24, #1
    ldr x0, [x26, x24, lsl #3]
    ret

// print_i64: x0 value
print_i64:
    stp x29, x30, [sp, #-16]!
    stp x19, x20, [sp, #-16]!
    mov x19, x0
    // handle 0
    cbnz x19, 1f
    mov x0, #1
    adrp x1, .digits@PAGE
    add x1, x1, .digits@PAGEOFF
    mov x2, #1
    bl sys_write
    b 9f
1:
    // negative?
    mov x20, #0
    tbz x19, #63, 2f
    mov x20, #1
    neg x19, x19
2:
    // fill numbuf from end
    adrp x1, numbuf@PAGE
    add x1, x1, numbuf@PAGEOFF
    add x1, x1, #31
    mov x2, #0           // digit count
3:
    mov x3, #10
    udiv x4, x19, x3     // quot
    msub x5, x4, x3, x19 // rem
    // digit char
    adrp x6, .digits@PAGE
    add x6, x6, .digits@PAGEOFF
    ldrb w5, [x6, x5]
    strb w5, [x1]
    sub x1, x1, #1
    add x2, x2, #1
    mov x19, x4
    cbnz x19, 3b
    // sign
    cbz x20, 4f
    mov w5, #'-'
    strb w5, [x1]
    sub x1, x1, #1
    add x2, x2, #1
4:
    add x1, x1, #1
    mov x0, #1
    // x1 ptr, x2 len
    bl sys_write
9:
    ldp x19, x20, [sp], #16
    ldp x29, x30, [sp], #16
    ret

halt_ok:
    mov x0, #0
    bl sys_exit

fail:
    mov x0, #2
    adrp x1, .msg_err@PAGE
    add x1, x1, .msg_err@PAGEOFF
    mov x2, #.msg_err_len
    bl sys_write
    mov x0, #1
    bl sys_exit

// --- syscalls macOS arm64: x16 = 0x02000000 | nr ---
// encode as mov #nr ; movk #0x200, lsl #16
sys_write:
    mov x16, #4
    movk x16, #0x200, lsl #16
    svc #0x80
    ret
sys_exit:
    mov x16, #1
    movk x16, #0x200, lsl #16
    svc #0x80
    ret
sys_open:
    mov x16, #5
    movk x16, #0x200, lsl #16
    svc #0x80
    ret
sys_read:
    mov x16, #3
    movk x16, #0x200, lsl #16
    svc #0x80
    ret
sys_close:
    mov x16, #6
    movk x16, #0x200, lsl #16
    svc #0x80
    ret
