#!/usr/bin/env python3
"""BOOTSTRAP ONLY — delete after 가나다 self-host. No C. .ㄱㄴㄷ → .gbc"""
from __future__ import annotations
import re, struct, sys
from pathlib import Path

HALT, PUSHI, PRINTI, PRINTS, ADD, SUB, MUL, DIV = range(8)
DUP, DROP, JMP, JZ, LOAD, STORE = range(8, 14)

def u8(x): return bytes([x & 255])
def i64(x): return struct.pack("<q", x)
def u32(x): return struct.pack("<I", x)
def i32(x): return struct.pack("<i", x)

def compile_source(src: str) -> bytes:
    raw = []
    for line in src.splitlines():
        if "#" in line: line = line[:line.index("#")]
        line = line.strip()
        if line: raw.append(line)
    # join braces to single stream of tokens by re-splitting carefully — line-based with { }
    lines = raw
    out = bytearray()
    locals_map: dict[str, int] = {}

    def loc(name: str) -> int:
        if name not in locals_map:
            locals_map[name] = len(locals_map)
        return locals_map[name]

    def emit_print_str(s: str) -> None:
        b = s.encode("utf-8")
        out.extend(u8(PRINTS)); out.extend(u32(len(b))); out.extend(b)

    def emit_expr(expr: str) -> None:
        expr = expr.strip()
        if re.fullmatch(r"-?\d+", expr):
            out.extend(u8(PUSHI)); out.extend(i64(int(expr))); return
        if re.fullmatch(r"[A-Za-z가-힣_][A-Za-z0-9가-힣_]*", expr):
            out.extend(u8(LOAD)); out.extend(u8(loc(expr))); return
        m = re.fullmatch(
            r"(-?\d+|[A-Za-z가-힣_][A-Za-z0-9가-힣_]*)\s*(==|!=|<=|>=|<|>|\+|\-|\*|/)\s*(-?\d+|[A-Za-z가-힣_][A-Za-z0-9가-힣_]*)",
            expr,
        )
        if not m:
            raise SystemExit(f"expr: {expr!r}")
        emit_expr(m.group(1)); emit_expr(m.group(3))
        op = m.group(2)
        if op in "+-*/":
            out.extend(u8({"+": ADD, "-": SUB, "*": MUL, "/": DIV}[op])); return
        # comparisons → push 1/0 via sub and flags... simplify: only == and < using SUB+JZ patterns later
        # emit a-b then for == JZ style — for bootstrap use:
        # < : a b → a-b, then we need sign — skip: only support == as (a-b) then not implemented
        # Simple: for < push (a<b) by emitting PUSHI after manual — use only for 만약 with == 0 style
        if op == "==":
            out.extend(u8(SUB))  # a-b, 0 if equal
            return
        if op == "<":
            # a < b → (a-b) negative — not easy without LT op; add LT as special using stack
            # For P0.1: treat as SUB then we document 만약 가 == 0 only
            raise SystemExit("use == for bootstrap if")
        raise SystemExit(f"op {op}")

    i = 0
    while i < len(lines):
        line = lines[i]
        m = re.fullmatch(r'출력\s*\(\s*"(.*)"\s*\)\s*', line)
        if m:
            emit_print_str(m.group(1).replace("\\n", "\n")); i += 1; continue
        m = re.fullmatch(r"출력\s*\(\s*(.+)\s*\)\s*", line)
        if m:
            emit_expr(m.group(1)); out.extend(u8(PRINTI)); i += 1; continue
        m = re.fullmatch(r"([A-Za-z가-힣_][A-Za-z0-9가-힣_]*)\s*=\s*(.+)\s*", line)
        if m:
            emit_expr(m.group(2)); out.extend(u8(STORE)); out.extend(u8(loc(m.group(1)))); i += 1; continue
        m = re.fullmatch(r"만약\s+(.+)\s*\{\s*", line)
        if m:
            emit_expr(m.group(1))  # leaves 0 if equal for ==
            # JZ skips body if zero... wait: we want if equal (a==b → sub 0) execute body
            # if top==0, body runs: JZ to body? JZ jumps if zero — so JZ body_start, JMP end, body, end
            # Actually: if a==b, sub is 0. We want run body when 0.
            # JZ rel means if pop==0 jump. So: JZ +body, JMP +end, body, end
            out.extend(u8(JZ)); jz_at = len(out); out.extend(i32(0))
            out.extend(u8(JMP)); jmp_at = len(out); out.extend(i32(0))
            body_pc = len(out)
            # patch JZ to body
            rel = body_pc - (jz_at + 4)
            out[jz_at:jz_at+4] = i32(rel)
            i += 1
            while i < len(lines) and lines[i] != "}":
                # recursive one stmt — reuse by re-queue: simple nested only assign/print
                sub = lines[i]
                mm = re.fullmatch(r'출력\s*\(\s*"(.*)"\s*\)\s*', sub)
                if mm: emit_print_str(mm.group(1)); i += 1; continue
                mm = re.fullmatch(r"출력\s*\(\s*(.+)\s*\)\s*", sub)
                if mm: emit_expr(mm.group(1)); out.extend(u8(PRINTI)); i += 1; continue
                mm = re.fullmatch(r"([A-Za-z가-힣_][A-Za-z0-9가-힣_]*)\s*=\s*(.+)\s*", sub)
                if mm:
                    emit_expr(mm.group(2)); out.extend(u8(STORE)); out.extend(u8(loc(mm.group(1)))); i += 1; continue
                raise SystemExit(f"if body: {sub!r}")
            if i >= len(lines) or lines[i] != "}":
                raise SystemExit("missing }")
            i += 1
            end_pc = len(out)
            rel = end_pc - (jmp_at + 4)
            out[jmp_at:jmp_at+4] = i32(rel)
            continue
        raise SystemExit(f"stmt: {line!r}")
    out.extend(u8(HALT))
    return bytes(out)

def main():
    if len(sys.argv) != 3:
        print("usage: gbcpack.py in.ㄱㄴㄷ out.gbc", file=sys.stderr); sys.exit(2)
    bc = compile_source(Path(sys.argv[1]).read_text(encoding="utf-8"))
    Path(sys.argv[2]).write_bytes(bc)
    print(f"wrote {sys.argv[2]} ({len(bc)} bytes)", file=sys.stderr)

if __name__ == "__main__":
    main()
