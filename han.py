#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""한(Han) — 한글 프로그래밍 언어 · 독립 인터프리터 (v0)

진짜 새 언어다: 자체 문법 + 자체 렉서·파서·트리워킹 인터프리터로 직접 실행한다.
파이썬으로 트랜스파일하지 않는다(구현 호스트가 파이썬일 뿐, 언어는 독립적이다).

사용법:
  python3 han.py 실행 프로그램.han      # 파일 실행
  python3 han.py            # 대화형(REPL)
"""
import sys


class HanError(Exception):
    pass


# ============================================================ 렉서
KEYWORDS = {
    '함수', '만약', '아니면', '동안', '반복', '반환',
    '참', '거짓', '없음', '부터', '까지', '를', '그리고', '또는', '아니다',
}
OPS = ['==', '!=', '<=', '>=', '+', '-', '*', '/', '%', '=', '<', '>', '(', ')', '{', '}', '[', ']', ',']


class Tok:
    __slots__ = ('kind', 'val', 'line')

    def __init__(self, kind, val, line):
        self.kind, self.val, self.line = kind, val, line


def lex(src):
    toks = []
    i, line, n = 0, 1, len(src)
    while i < n:
        ch = src[i]
        if ch == '\n':
            line += 1; i += 1; continue
        if ch in ' \t\r':
            i += 1; continue
        if ch == '#':                                   # 주석
            while i < n and src[i] != '\n':
                i += 1
            continue
        if ch == '"':                                   # 문자열
            i += 1; buf = []
            while i < n and src[i] != '"':
                if src[i] == '\\' and i + 1 < n:
                    buf.append({'n': '\n', 't': '\t', '"': '"', '\\': '\\'}.get(src[i + 1], src[i + 1]))
                    i += 2
                else:
                    buf.append(src[i]); i += 1
            if i >= n:
                raise HanError(f"[{line}행] 문자열이 닫히지 않았습니다")
            i += 1; toks.append(Tok('STR', ''.join(buf), line)); continue
        if ch.isdigit():                                # 숫자
            j, dot = i, False
            while j < n and (src[j].isdigit() or (src[j] == '.' and not dot)):
                if src[j] == '.':
                    dot = True
                j += 1
            text = src[i:j]
            toks.append(Tok('NUM', float(text) if dot else int(text), line)); i = j; continue
        if ch == '_' or ch.isalpha():                   # 식별자/키워드 (한글 포함)
            j = i
            while j < n and (src[j] == '_' or src[j].isalnum()):
                j += 1
            word = src[i:j]; i = j
            toks.append(Tok('KW' if word in KEYWORDS else 'ID', word, line)); continue
        for text in OPS:                                # 연산자(긴 것 우선)
            if src.startswith(text, i):
                toks.append(Tok('OP', text, line)); i += len(text); break
        else:
            raise HanError(f"[{line}행] 알 수 없는 문자: '{ch}'")
    toks.append(Tok('EOF', None, line))
    return toks


# ============================================================ 파서  (AST = 튜플)
class Parser:
    def __init__(self, toks):
        self.toks, self.p = toks, 0

    def peek(self):
        return self.toks[self.p]

    def at(self, kind, val=None):
        t = self.toks[self.p]
        return t.kind == kind and (val is None or t.val == val)

    def eat(self, kind=None, val=None):
        t = self.toks[self.p]
        if kind and (t.kind != kind or (val is not None and t.val != val)):
            raise HanError(f"[{t.line}행] 구문 오류: '{val or kind}' 자리에 '{t.val}'")
        self.p += 1
        return t

    def parse(self):
        stmts = []
        while not self.at('EOF'):
            stmts.append(self.statement())
        return ('block', stmts)

    def block(self):
        self.eat('OP', '{')
        stmts = []
        while not self.at('OP', '}'):
            if self.at('EOF'):
                raise HanError("'}' 가 필요합니다 (블록이 닫히지 않음)")
            stmts.append(self.statement())
        self.eat('OP', '}')
        return ('block', stmts)

    def statement(self):
        if self.at('KW', '함수'):
            return self.func_decl()
        if self.at('KW', '만약'):
            return self.if_stmt()
        if self.at('KW', '동안'):
            self.eat(); cond = self.expr(); body = self.block(); return ('while', cond, body)
        if self.at('KW', '반복'):
            return self.for_stmt()
        if self.at('KW', '반환'):
            self.eat()
            val = None if (self.at('OP', '}') or self.at('EOF')) else self.expr()
            return ('return', val)
        # 대입(변수/색인) vs 표현식
        node = self.expr()
        if self.at('OP', '='):
            self.eat('OP', '=')
            rhs = self.expr()
            if node[0] == 'var':
                return ('assign', node[1], rhs)
            if node[0] == 'index':
                return ('setindex', node[1], node[2], rhs)
            raise HanError("대입할 수 없는 대상입니다")
        return ('exprstmt', node)

    def func_decl(self):
        self.eat('KW', '함수'); name = self.eat('ID').val; self.eat('OP', '(')
        params = []
        while not self.at('OP', ')'):
            params.append(self.eat('ID').val)
            if self.at('OP', ','):
                self.eat()
        self.eat('OP', ')')
        return ('func', name, params, self.block())

    def if_stmt(self):
        self.eat('KW', '만약'); cond = self.expr(); then = self.block(); els = None
        if self.at('KW', '아니면'):
            self.eat()
            els = self.if_stmt() if self.at('KW', '만약') else self.block()
        return ('if', cond, then, els)

    def for_stmt(self):
        self.eat('KW', '반복'); var = self.eat('ID').val; self.eat('KW', '를')
        start = self.expr(); self.eat('KW', '부터'); end = self.expr(); self.eat('KW', '까지')
        return ('for', var, start, end, self.block())

    # 표현식 (우선순위)
    def expr(self):
        return self.logic_or()

    def logic_or(self):
        node = self.logic_and()
        while self.at('KW', '또는'):
            self.eat(); node = ('bin', '또는', node, self.logic_and())
        return node

    def logic_and(self):
        node = self.equality()
        while self.at('KW', '그리고'):
            self.eat(); node = ('bin', '그리고', node, self.equality())
        return node

    def equality(self):
        node = self.comparison()
        while self.at('OP', '==') or self.at('OP', '!='):
            op = self.eat().val; node = ('bin', op, node, self.comparison())
        return node

    def comparison(self):
        node = self.term()
        while self.at('OP', '<') or self.at('OP', '>') or self.at('OP', '<=') or self.at('OP', '>='):
            op = self.eat().val; node = ('bin', op, node, self.term())
        return node

    def term(self):
        node = self.factor()
        while self.at('OP', '+') or self.at('OP', '-'):
            op = self.eat().val; node = ('bin', op, node, self.factor())
        return node

    def factor(self):
        node = self.unary()
        while self.at('OP', '*') or self.at('OP', '/') or self.at('OP', '%'):
            op = self.eat().val; node = ('bin', op, node, self.unary())
        return node

    def unary(self):
        if self.at('KW', '아니다'):
            self.eat(); return ('un', '아니다', self.unary())
        if self.at('OP', '-'):
            self.eat(); return ('un', '-', self.unary())
        return self.call()

    def call(self):
        node = self.primary()
        while True:
            if self.at('OP', '('):
                self.eat('OP', '('); args = []
                while not self.at('OP', ')'):
                    args.append(self.expr())
                    if self.at('OP', ','):
                        self.eat()
                self.eat('OP', ')'); node = ('call', node, args)
            elif self.at('OP', '['):
                self.eat('OP', '['); idx = self.expr(); self.eat('OP', ']')
                node = ('index', node, idx)
            else:
                break
        return node

    def primary(self):
        t = self.peek()
        if t.kind == 'NUM':
            self.eat(); return ('lit', t.val)
        if t.kind == 'STR':
            self.eat(); return ('lit', t.val)
        if t.kind == 'KW' and t.val == '참':
            self.eat(); return ('lit', True)
        if t.kind == 'KW' and t.val == '거짓':
            self.eat(); return ('lit', False)
        if t.kind == 'KW' and t.val == '없음':
            self.eat(); return ('lit', None)
        if t.kind == 'ID':
            self.eat(); return ('var', t.val)
        if t.kind == 'OP' and t.val == '(':
            self.eat(); node = self.expr(); self.eat('OP', ')'); return node
        if t.kind == 'OP' and t.val == '[':
            self.eat('OP', '['); elems = []
            while not self.at('OP', ']'):
                elems.append(self.expr())
                if self.at('OP', ','):
                    self.eat()
            self.eat('OP', ']'); return ('list', elems)
        raise HanError(f"[{t.line}행] 구문 오류: 예기치 않은 '{t.val}'")


# ============================================================ 인터프리터
class Func:
    def __init__(self, name, params, body, env):
        self.name, self.params, self.body, self.env = name, params, body, env


class Return(Exception):
    def __init__(self, value):
        self.value = value


class Env:
    def __init__(self, parent=None):
        self.vars, self.parent = {}, parent

    def get(self, name):
        e = self
        while e:
            if name in e.vars:
                return e.vars[name]
            e = e.parent
        raise HanError(f"이름 오류: '{name}' 가 정의되지 않았습니다")

    def set_existing_or_define(self, name, val):
        e = self
        while e:
            if name in e.vars:
                e.vars[name] = val; return
            e = e.parent
        self.vars[name] = val           # 최상위는 아니고 현재 스코프에 새로 정의


def 문자열화(v):
    if v is True:
        return '참'
    if v is False:
        return '거짓'
    if v is None:
        return '없음'
    if isinstance(v, float) and v.is_integer():
        return str(int(v))
    if isinstance(v, Func):
        return f"<함수 {v.name}>"
    if isinstance(v, list):
        return '[' + ', '.join(문자열화(x) for x in v) + ']'
    return str(v)


def 참인가(v):
    if isinstance(v, (list, dict, str)):
        return len(v) > 0
    return not (v is False or v is None or v == 0)


class Interp:
    def __init__(self, out=None):
        self.g = Env()
        self.out = out if out is not None else sys.stdout

    def run(self, ast):
        self.exec_block(ast, self.g)

    def exec_block(self, block, env):
        for st in block[1]:
            self.exec(st, env)

    def exec(self, node, env):
        t = node[0]
        if t == 'block':
            inner = Env(env); self.exec_block(node, inner)
        elif t == 'assign':
            env.set_existing_or_define(node[1], self.eval(node[2], env))
        elif t == 'func':
            env.vars[node[1]] = Func(node[1], node[2], node[3], env)
        elif t == 'if':
            if 참인가(self.eval(node[1], env)):
                self.exec(node[2], env)
            elif node[3] is not None:
                self.exec(node[3], env)
        elif t == 'while':
            while 참인가(self.eval(node[1], env)):
                self.exec(node[2], env)
        elif t == 'for':
            a = self.eval(node[2], env); b = self.eval(node[3], env)
            i = a
            while i <= b:                       # 부터..까지 = 양끝 포함
                loop = Env(env); loop.vars[node[1]] = i
                self.exec_block(node[4], loop)
                i += 1
        elif t == 'return':
            raise Return(self.eval(node[1], env) if node[1] is not None else None)
        elif t == 'setindex':
            obj = self.eval(node[1], env); i = self.eval(node[2], env); v = self.eval(node[3], env)
            self._index_set(obj, i, v)
        elif t == 'exprstmt':
            self.eval(node[1], env)
        else:
            raise HanError(f"실행 오류: 알 수 없는 문장 {t}")

    def eval(self, node, env):
        t = node[0]
        if t == 'lit':
            return node[1]
        if t == 'var':
            return env.get(node[1])
        if t == 'un':
            v = self.eval(node[2], env)
            return (not 참인가(v)) if node[1] == '아니다' else -v
        if t == 'bin':
            return self.binop(node[1], node[2], node[3], env)
        if t == 'list':
            return [self.eval(e, env) for e in node[1]]
        if t == 'index':
            return self._index_get(self.eval(node[1], env), self.eval(node[2], env))
        if t == 'call':
            return self.call(node, env)
        raise HanError(f"실행 오류: 알 수 없는 식 {t}")

    def _index_get(self, obj, i):
        if isinstance(obj, (list, str)):
            if not isinstance(i, int):
                raise HanError("색인은 정수여야 합니다")
            if i < -len(obj) or i >= len(obj):
                raise HanError(f"색인 범위 오류: {i}")
            return obj[i]
        raise HanError("색인할 수 없는 값입니다")

    def _index_set(self, obj, i, v):
        if isinstance(obj, list):
            if not isinstance(i, int):
                raise HanError("색인은 정수여야 합니다")
            if i < -len(obj) or i >= len(obj):
                raise HanError(f"색인 범위 오류: {i}")
            obj[i] = v
            return
        raise HanError("색인 대입할 수 없는 값입니다")

    def binop(self, op, ln, rn, env):
        if op == '그리고':
            l = self.eval(ln, env); return self.eval(rn, env) if 참인가(l) else l
        if op == '또는':
            l = self.eval(ln, env); return l if 참인가(l) else self.eval(rn, env)
        a = self.eval(ln, env); b = self.eval(rn, env)
        if op == '+':
            if isinstance(a, str) or isinstance(b, str):
                return 문자열화(a) + 문자열화(b)
            return a + b
        if op == '-':
            return a - b
        if op == '*':
            return a * b
        if op == '/':
            if b == 0:
                raise HanError("0으로 나눌 수 없습니다")
            return a / b
        if op == '%':
            return a % b
        if op == '==':
            return a == b
        if op == '!=':
            return a != b
        if op == '<':
            return a < b
        if op == '>':
            return a > b
        if op == '<=':
            return a <= b
        if op == '>=':
            return a >= b
        raise HanError(f"알 수 없는 연산자 {op}")

    def call(self, node, env):
        callee = node[1]
        args = [self.eval(a, env) for a in node[2]]
        # 내장 함수
        if callee[0] == 'var' and callee[1] in BUILTINS:
            return BUILTINS[callee[1]](self, args)
        fn = self.eval(callee, env)
        if not isinstance(fn, Func):
            raise HanError("호출 오류: 함수가 아닙니다")
        if len(args) != len(fn.params):
            raise HanError(f"호출 오류: '{fn.name}' 는 인자 {len(fn.params)}개가 필요(받음 {len(args)}개)")
        local = Env(fn.env)
        for name, val in zip(fn.params, args):
            local.vars[name] = val
        try:
            self.exec_block(fn.body, local)
        except Return as r:
            return r.value
        return None


# ============================================================ 내장 함수
def _출력(interp, args):
    interp.out.write(' '.join(문자열화(a) for a in args) + '\n')
    return None


def _길이(interp, args):
    return len(args[0])


def _숫자(interp, args):
    s = args[0]
    return float(s) if (isinstance(s, str) and '.' in s) else int(s)


def _추가(interp, args):           # 목록 끝에 값 추가
    args[0].append(args[1]); return None


BUILTINS = {
    '출력': _출력,
    '길이': _길이,
    '숫자': _숫자,
    '추가': _추가,
}


# ============================================================ 진입점
def 실행소스(src, out=None):
    interp = Interp(out=out)
    interp.run(Parser(lex(src)).parse())
    return interp


def main(argv):
    if len(argv) >= 3 and argv[1] in ('실행', 'run'):
        with open(argv[2], encoding='utf-8') as f:
            src = f.read()
        try:
            실행소스(src)
        except HanError as e:
            print(f"오류: {e}", file=sys.stderr); sys.exit(1)
    elif len(argv) == 1 or (len(argv) == 2 and argv[1] in ('repl', '대화')):
        print("한(Han) v0 · 대화형. 종료는 Ctrl-D")
        interp = Interp()
        while True:
            try:
                line = input("한> ")
            except EOFError:
                print(); break
            if not line.strip():
                continue
            try:
                interp.run(Parser(lex(line)).parse())
            except (HanError, Return) as e:
                print(f"오류: {e}")
    else:
        print("사용법: python3 han.py 실행 <파일.han>  |  python3 han.py 대화", file=sys.stderr)
        sys.exit(2)


if __name__ == '__main__':
    main(sys.argv)
