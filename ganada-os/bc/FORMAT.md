# 가나다 바이트코드 (`.ㄱㅂ` / `.gbc`)

리틀엔디안. 스택 머신.

| opcode | 이름 | 피연산자 | 동작 |
|--------|------|----------|------|
| 0x00 | HALT | — | 종료 (exit 0) |
| 0x01 | PUSHI | i64 | 정수 푸시 |
| 0x02 | PRINTI | — | 팝 → 십진 출력 + `\n` |
| 0x03 | PRINTS | u32 len, bytes | 문자열 출력 + `\n` (스택 무관) |
| 0x04 | ADD | — | a b → a+b |
| 0x05 | SUB | — | a b → a-b |
| 0x06 | MUL | — | a b → a*b |
| 0x07 | DIV | — | a b → a/b (b≠0) |
| 0x08 | DUP | — | 복제 |
| 0x09 | DROP | — | 버림 |
| 0x0A | JMP | i32 rel | pc += rel (from after operand) |
| 0x0B | JZ | i32 rel | 팝, 0이면 점프 |
| 0x0C | LOAD | u8 slot | 로컬 → 스택 |
| 0x0D | STORE | u8 slot | 스택 → 로컬 |
| 0x0E | CALL | u32 abs_addr, u8 argc | 반환주소 푸시 후 점프 (간단 규약 후기) |
| 0x0F | RET | — | 반환 |

P0 구현: 0x00–0x07, 0x0C–0x0D, 0x0A–0x0B (제어·로컬 최소).
