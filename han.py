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
import os
import math
import random as _random
import json as _json
import urllib.request


class HanError(Exception):
    pass


# ============================================================ 렉서
KEYWORDS = {
    '함수', '만약', '아니면', '동안', '반복', '반환',
    '참', '거짓', '없음', '부터', '까지', '에서', '를', '그리고', '또는', '아니다', '가져오기',
    '멈춤', '계속', '람다', '시도', '잡기',
}
OPS = ['==', '!=', '<=', '>=', '+=', '-=', '*=', '/=', '+', '-', '*', '/', '%', '=', '<', '>', '(', ')', '{', '}', '[', ']', ':', ',']


class Tok:
    __slots__ = ('kind', 'val', 'line', 'col')

    def __init__(self, kind, val, line, col=0):
        self.kind, self.val, self.line, self.col = kind, val, line, col


def lex(src):
    toks = []
    i, line, n = 0, 1, len(src)
    bol = 0                                             # 현재 줄 시작 인덱스 (열 = i - bol + 1)
    while i < n:
        ch = src[i]
        if ch == '\n':
            line += 1; i += 1; bol = i; continue
        if ch in ' \t\r':
            i += 1; continue
        if ch == '#':                                   # 주석
            while i < n and src[i] != '\n':
                i += 1
            continue
        col = i - bol + 1                               # 토큰 시작 열
        if ch == '"':                                   # 문자열
            i += 1; buf = []
            while i < n and src[i] != '"':
                if src[i] == '\\' and i + 1 < n:
                    buf.append({'n': '\n', 't': '\t', '"': '"', '\\': '\\'}.get(src[i + 1], src[i + 1]))
                    i += 2
                else:
                    buf.append(src[i]); i += 1
            if i >= n:
                raise HanError(f"[{line}행 {col}열] 문자열이 닫히지 않았습니다")
            i += 1; toks.append(Tok('STR', ''.join(buf), line, col)); continue
        if ch.isdigit():                                # 숫자
            j, dot = i, False
            while j < n and (src[j].isdigit() or (src[j] == '.' and not dot)):
                if src[j] == '.':
                    dot = True
                j += 1
            text = src[i:j]
            toks.append(Tok('NUM', float(text) if dot else int(text), line, col)); i = j; continue
        if ch == '_' or ch.isalpha():                   # 식별자/키워드 (한글 포함)
            j = i
            while j < n and (src[j] == '_' or src[j].isalnum()):
                j += 1
            word = src[i:j]; i = j
            toks.append(Tok('KW' if word in KEYWORDS else 'ID', word, line, col)); continue
        for text in OPS:                                # 연산자(긴 것 우선)
            if src.startswith(text, i):
                toks.append(Tok('OP', text, line, col)); i += len(text); break
        else:
            raise HanError(f"[{line}행 {col}열] 알 수 없는 문자: '{ch}'")
    toks.append(Tok('EOF', None, line, i - bol + 1))
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
            raise HanError(f"[{t.line}행 {t.col}열] 구문 오류: '{val or kind}' 자리에 '{t.val}'")
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
        ln = self.peek().line
        return ('stmt', ln, self._statement_inner())   # 문장에 행 번호 부착(런타임 오류 보고용)

    def _statement_inner(self):
        if self.at('KW', '가져오기'):
            self.eat('KW', '가져오기')
            t = self.peek()
            if t.kind != 'STR':
                raise HanError(f"[{t.line}행 {t.col}열] 가져오기 뒤에는 \"경로\" 문자열이 필요합니다")
            self.eat()
            return ('import', t.val)
        if self.at('KW', '멈춤'):
            self.eat(); return ('break',)
        if self.at('KW', '계속'):
            self.eat(); return ('continue',)
        if self.at('KW', '시도'):                 # 시도 { } 잡기(오류) { }
            self.eat('KW', '시도')
            try_block = self.block()
            self.eat('KW', '잡기'); self.eat('OP', '(')
            err_var = self.eat('ID').val
            self.eat('OP', ')')
            return ('try', try_block, err_var, self.block())
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
        if node[0] == 'var' and self.at('OP', ','):   # 구조 분해: 가, 나 = 목록
            names = [node[1]]
            while self.at('OP', ','):
                self.eat('OP', ','); names.append(self.eat('ID').val)
            self.eat('OP', '=')
            return ('destructure', names, self.expr())
        if self.at('OP', '='):
            self.eat('OP', '=')
            rhs = self.expr()
            if node[0] == 'var':
                return ('assign', node[1], rhs)
            if node[0] == 'index':
                return ('setindex', node[1], node[2], rhs)
            raise HanError("대입할 수 없는 대상입니다")
        for cop in ('+=', '-=', '*=', '/='):       # 복합 대입
            if self.at('OP', cop):
                self.eat('OP', cop)
                combined = ('bin', cop[0], node, self.expr())
                if node[0] == 'var':
                    return ('assign', node[1], combined)
                if node[0] == 'index':
                    return ('setindex', node[1], node[2], combined)
                raise HanError("복합 대입 대상이 올바르지 않습니다")
        return ('exprstmt', node)

    def parse_params(self):
        # 매개변수 목록 → [(이름, 기본식 또는 None)]. 기본값 있는 건 뒤쪽에만.
        self.eat('OP', '(')
        params = []
        seen_default = False
        while not self.at('OP', ')'):
            t = self.eat('ID'); pname = t.val
            default = None
            if self.at('OP', '='):
                self.eat('OP', '='); default = self.expr(); seen_default = True
            elif seen_default:
                raise HanError(f"[{t.line}행 {t.col}열] 기본값 있는 매개변수 뒤에는 기본값 없는 매개변수를 둘 수 없습니다")
            params.append((pname, default))
            if self.at('OP', ','):
                self.eat()
        self.eat('OP', ')')
        return params

    def func_decl(self):
        self.eat('KW', '함수'); name = self.eat('ID').val
        params = self.parse_params()
        return ('func', name, params, self.block())

    def if_stmt(self):
        self.eat('KW', '만약'); cond = self.expr(); then = self.block(); els = None
        if self.at('KW', '아니면'):
            self.eat()
            els = self.if_stmt() if self.at('KW', '만약') else self.block()
        return ('if', cond, then, els)

    def for_stmt(self):
        self.eat('KW', '반복'); var = self.eat('ID').val
        idx_var = None
        if self.at('OP', ','):               # 반복 인덱스, 값 를 목록 에서 { }
            self.eat('OP', ','); idx_var = var; var = self.eat('ID').val
        self.eat('KW', '를')
        first = self.expr()
        if self.at('KW', '에서'):            # 반복 x 를 [목록] 에서 { }
            self.eat('KW', '에서')
            return ('foreach', var, first, self.block(), idx_var)
        if idx_var is not None:
            raise HanError("범위 반복(부터..까지)에는 인덱스 변수를 쓸 수 없습니다 (순회 전용)")
        self.eat('KW', '부터'); end = self.expr(); self.eat('KW', '까지')   # 반복 i 를 a 부터 b 까지 { }
        return ('for', var, first, end, self.block())

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
                self.eat('OP', '[')
                start = None if self.at('OP', ':') else self.expr()
                if self.at('OP', ':'):              # 슬라이스 [시작:끝]
                    self.eat('OP', ':')
                    end = None if self.at('OP', ']') else self.expr()
                    self.eat('OP', ']')
                    node = ('slice', node, start, end)
                else:
                    self.eat('OP', ']')
                    node = ('index', node, start)
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
        if t.kind == 'OP' and t.val == '{':       # 사전(맵) 리터럴 {키: 값, ...}
            self.eat('OP', '{'); pairs = []
            while not self.at('OP', '}'):
                k = self.expr(); self.eat('OP', ':'); v = self.expr()
                pairs.append((k, v))
                if self.at('OP', ','):
                    self.eat()
            self.eat('OP', '}'); return ('dict', pairs)
        if t.kind == 'KW' and t.val == '람다':       # 익명 함수 람다(매개변수){ 본문 }
            self.eat('KW', '람다')
            params = self.parse_params()
            return ('lambda', params, self.block())
        raise HanError(f"[{t.line}행 {t.col}열] 구문 오류: 예기치 않은 '{t.val}'")


# ============================================================ 인터프리터
class Func:
    def __init__(self, name, params, body, env):
        self.name, self.params, self.body, self.env = name, params, body, env


class Return(Exception):
    def __init__(self, value):
        self.value = value


class BreakSignal(Exception):
    pass


class ContinueSignal(Exception):
    pass


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
    if isinstance(v, dict):
        return '{' + ', '.join(문자열화(k) + ': ' + 문자열화(x) for k, x in v.items()) + '}'
    return str(v)


def 참인가(v):
    if isinstance(v, (list, dict, str)):
        return len(v) > 0
    return not (v is False or v is None or v == 0)


class Interp:
    def __init__(self, out=None):
        self.g = Env()
        self.out = out if out is not None else sys.stdout
        self.cur_line = 0
        self.base_dir = '.'
        self.imported = set()

    def run(self, ast):
        try:
            self.exec_block(ast, self.g)
        except (BreakSignal, ContinueSignal):
            raise HanError(f"[{self.cur_line}행] '멈춤'/'계속'은 반복문 안에서만 쓸 수 있습니다")
        except HanError as e:
            msg = str(e)
            if not msg.startswith('['):     # 행 정보 없는 런타임 오류에 현재 행 부착
                raise HanError(f"[{self.cur_line}행] {msg}")
            raise

    def exec_block(self, block, env):
        for st in block[1]:
            if st[0] == 'stmt':
                self.cur_line = st[1]
                self.exec(st[2], env)
            else:
                self.exec(st, env)

    def _do_import(self, path):
        full = os.path.normpath(os.path.join(self.base_dir, path))
        if full in self.imported:   # 중복/순환 방지
            return
        self.imported.add(full)
        try:
            with open(full, encoding='utf-8') as f:
                src = f.read()
        except OSError:
            raise HanError(f"가져올 수 없습니다: {path}")
        sub = Parser(lex(src)).parse()
        prev = self.base_dir
        self.base_dir = os.path.dirname(full) or '.'   # 중첩 import 상대경로
        self.exec_block(sub, self.g)                    # 정의를 전역에 주입
        self.base_dir = prev

    def exec(self, node, env):
        t = node[0]
        if t == 'block':
            inner = Env(env); self.exec_block(node, inner)
        elif t == 'import':
            self._do_import(node[1])
        elif t == 'assign':
            env.set_existing_or_define(node[1], self.eval(node[2], env))
        elif t == 'destructure':
            vals = self.eval(node[2], env)
            if not isinstance(vals, (list, str)) or len(vals) != len(node[1]):
                raise HanError(f"구조 분해 오류: 값 {len(node[1])}개가 필요합니다 (목록/문자열만)")
            for name, v in zip(node[1], vals):
                env.set_existing_or_define(name, v)
        elif t == 'func':
            env.vars[node[1]] = Func(node[1], node[2], node[3], env)
        elif t == 'if':
            if 참인가(self.eval(node[1], env)):
                self.exec(node[2], env)
            elif node[3] is not None:
                self.exec(node[3], env)
        elif t == 'while':
            while 참인가(self.eval(node[1], env)):
                try:
                    self.exec(node[2], env)
                except BreakSignal:
                    break
                except ContinueSignal:
                    continue
        elif t == 'for':
            a = self.eval(node[2], env); b = self.eval(node[3], env)
            i = a
            while i <= b:                       # 부터..까지 = 양끝 포함
                loop = Env(env); loop.vars[node[1]] = i
                try:
                    self.exec_block(node[4], loop)
                except BreakSignal:
                    break
                except ContinueSignal:
                    pass
                i += 1
        elif t == 'foreach':
            coll = self.eval(node[2], env)
            if isinstance(coll, dict):
                items = list(coll.keys())
            elif isinstance(coll, (list, str)):
                items = coll
            else:
                raise HanError("반복할 수 없는 값입니다 (목록·문자열·사전만)")
            idx_var = node[4] if len(node) > 4 else None
            for idx, it in enumerate(items):
                loop = Env(env); loop.vars[node[1]] = it
                if idx_var is not None:
                    loop.vars[idx_var] = idx
                try:
                    self.exec_block(node[3], loop)
                except BreakSignal:
                    break
                except ContinueSignal:
                    continue
        elif t == 'break':
            raise BreakSignal()
        elif t == 'continue':
            raise ContinueSignal()
        elif t == 'try':
            try:
                self.exec(node[1], env)
            except HanError as e:
                scope = Env(env)
                scope.vars[node[2]] = str(e)
                self.exec_block(node[3], scope)
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
        if t == 'dict':
            return {self.eval(k, env): self.eval(v, env) for k, v in node[1]}
        if t == 'lambda':
            return Func('<람다>', node[1], node[2], env)
        if t == 'index':
            return self._index_get(self.eval(node[1], env), self.eval(node[2], env))
        if t == 'slice':
            obj = self.eval(node[1], env)
            s = self.eval(node[2], env) if node[2] is not None else None
            e = self.eval(node[3], env) if node[3] is not None else None
            if isinstance(obj, (list, str)):
                return obj[s:e]
            raise HanError("자를 수 없는 값입니다 (목록·문자열만)")
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
        if isinstance(obj, dict):
            if i not in obj:
                raise HanError(f"키 없음: {문자열화(i)}")
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
        if isinstance(obj, dict):       # 사전은 새 키 대입 허용
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
            try:
                return BUILTINS[callee[1]](self, args)
            except HanError:
                raise
            except Exception as e:
                raise HanError(f"'{callee[1]}' 호출 오류: {e}")
        return self.apply_func(self.eval(callee, env), args)

    def apply_func(self, fn, args):      # 값 인자로 Func 호출 (call·고차 내장함수 공유)
        if not isinstance(fn, Func):
            raise HanError("호출 오류: 함수가 아닙니다")
        params = fn.params
        required = sum(1 for (_, d) in params if d is None)
        if len(args) < required or len(args) > len(params):
            need = str(required) if required == len(params) else f"{required}~{len(params)}"
            raise HanError(f"호출 오류: '{fn.name}' 는 인자 {need}개가 필요(받음 {len(args)}개)")
        local = Env(fn.env)
        for i, (pname, default) in enumerate(params):
            local.vars[pname] = args[i] if i < len(args) else self.eval(default, local)
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


def _합(interp, args):             # 목록 원소 합
    return sum(args[0])


def _평균(interp, args):           # 평균(목록) → 산술 평균(빈 목록 오류)
    items = args[0]
    if not items:
        raise HanError('평균: 빈 목록')
    return sum(items) / len(items)


def _중앙값(interp, args):         # 중앙값(목록) → 정렬 후 가운데 값(짝수 개면 가운데 두 값의 평균)
    items = sorted(args[0])
    n = len(items)
    if n == 0:
        raise HanError('중앙값: 빈 목록')
    mid = n // 2
    return items[mid] if n % 2 == 1 else (items[mid - 1] + items[mid]) / 2


def _정렬(interp, args):           # 정렬된 새 목록
    return sorted(args[0])


def _최대(interp, args):
    return max(args[0])


def _최소(interp, args):
    return min(args[0])


def _범위(interp, args):           # 범위(끝) | 범위(시작,끝) | 범위(시작,끝,간격) → 목록
    if len(args) == 1:
        return list(range(int(args[0])))
    if len(args) == 2:
        return list(range(int(args[0]), int(args[1])))
    return list(range(int(args[0]), int(args[1]), int(args[2])))


def _나누기(interp, args):         # 문자열 나누기 → 목록
    return args[0].split(args[1]) if len(args) > 1 else args[0].split()


def _위치(interp, args):           # 위치(문자열, 부분) → 부분의 첫 위치(0부터), 없으면 -1
    return 문자열화(args[0]).find(문자열화(args[1]))


def _합치기(interp, args):         # 목록 → 문자열 (구분자)
    sep = args[1] if len(args) > 1 else ''
    return sep.join(문자열화(x) for x in args[0])


def _거꾸로(interp, args):         # 뒤집은 새 목록
    return list(reversed(args[0]))


def _ai_call(messages, model):     # 공통 OpenRouter 호출 (질문·체계질문 공유)
    key = os.environ.get('OPENROUTER_API_KEY')
    if not key:                    # 키 없으면 안내 stub(오프라인에서도 안전)
        last = messages[-1]['content'] if messages else ''
        return '[AI 키 없음] OPENROUTER_API_KEY를 설정하면 실제 답을 받아요. (물음: ' + last[:30] + ')'
    try:
        req = urllib.request.Request(
            'https://openrouter.ai/api/v1/chat/completions',
            data=_json.dumps({'model': model, 'messages': messages}).encode('utf-8'),
            headers={'Authorization': 'Bearer ' + key, 'Content-Type': 'application/json'},
        )
        with urllib.request.urlopen(req, timeout=60) as r:
            data = _json.loads(r.read().decode('utf-8'))
        return data['choices'][0]['message']['content']
    except Exception as e:
        raise HanError('AI 호출 실패: ' + str(e))


def _질문(interp, args):           # AI에게 묻기 — 질문(프롬프트[, 모델]). OpenRouter 경유.
    prompt = 문자열화(args[0]) if args else ''
    model = args[1] if len(args) > 1 else 'openai/gpt-4o-mini'
    return _ai_call([{'role': 'user', 'content': prompt}], model)


def _체계질문(interp, args):       # 체계질문(시스템, 사용자[, 모델]) — 시스템 프롬프트로 AI 행동 제어
    system = 문자열화(args[0]) if args else ''
    user = 문자열화(args[1]) if len(args) > 1 else ''
    model = args[2] if len(args) > 2 else 'openai/gpt-4o-mini'
    return _ai_call([
        {'role': 'system', 'content': system},
        {'role': 'user', 'content': user},
    ], model)


def _대문자(interp, args):
    return args[0].upper()


def _소문자(interp, args):
    return args[0].lower()


def _다듬기(interp, args):          # 앞뒤 공백 제거
    return args[0].strip()


def _바꾸기(interp, args):          # 바꾸기(문자열, 옛, 새)
    return args[0].replace(args[1], args[2])


def _포함(interp, args):           # 포함(컨테이너, 값) → 참/거짓 (문자열·목록·사전)
    return args[1] in args[0]


def _시작(interp, args):
    return args[0].startswith(args[1])


def _끝(interp, args):
    return args[0].endswith(args[1])


def _타입(interp, args):           # 값의 타입 이름
    v = args[0]
    if v is True or v is False:
        return '참거짓'
    if v is None:
        return '없음'
    if isinstance(v, int):
        return '정수'
    if isinstance(v, float):
        return '실수'
    if isinstance(v, str):
        return '문자열'
    if isinstance(v, list):
        return '목록'
    if isinstance(v, dict):
        return '사전'
    if isinstance(v, Func):
        return '함수'
    return '알수없음'


def _절댓값(interp, args):
    return abs(args[0])


def _반올림(interp, args):          # 반올림(수[, 소수자리])
    return round(args[0], int(args[1])) if len(args) > 1 else round(args[0])


def _올림(interp, args):
    return math.ceil(args[0])


def _내림(interp, args):
    return math.floor(args[0])


def _제곱근(interp, args):
    return math.sqrt(args[0])


def _거듭제곱(interp, args):        # 거듭제곱(밑, 지수)
    return args[0] ** args[1]


def _입력(interp, args):           # 한 줄 입력받기 — 입력([프롬프트]) → 문자열
    if args:
        interp.out.write(문자열화(args[0]))
        try:
            interp.out.flush()
        except Exception:
            pass
    line = sys.stdin.readline()
    return line.rstrip('\n') if line else ''


def _키들(interp, args):           # 사전의 키 목록
    return list(args[0].keys())


def _값들(interp, args):           # 사전의 값 목록
    return list(args[0].values())


def _항목들(interp, args):         # 사전의 [키, 값] 목록
    return [[k, v] for k, v in args[0].items()]


def _값얻기(interp, args):         # 값얻기(사전, 키[, 기본값]) → 키 있으면 값, 없으면 기본값(기본 미지정 시 없음)
    d, k = args[0], args[1]
    if isinstance(d, dict) and k in d:
        return d[k]
    return args[2] if len(args) > 2 else None


def _키있나(interp, args):         # 키있나(사전, 키) → 키 존재 여부(참/거짓)
    return isinstance(args[0], dict) and args[1] in args[0]


def _제이슨파싱(interp, args):      # JSON 문자열 → 값 (LLM 출력·API 응답)
    return _json.loads(args[0])


def _제이슨문자열(interp, args):    # 값 → JSON 문자열 (제이슨문자열(값[, 들여쓰기]))
    indent = int(args[1]) if len(args) > 1 else None
    return _json.dumps(args[0], ensure_ascii=False, indent=indent)


def _변환(interp, args):           # 변환(목록, 함수) → 각 원소에 함수 적용한 새 목록 (map)
    return [interp.apply_func(args[1], [x]) for x in args[0]]


def _거르기(interp, args):         # 거르기(목록, 함수) → 함수가 참인 원소만 (filter)
    return [x for x in args[0] if 참인가(interp.apply_func(args[1], [x]))]


def _접기(interp, args):           # 접기(목록, 초기값, 함수(누적,원소)) → 하나로 누적 (reduce)
    acc = args[1]
    for x in args[0]:
        acc = interp.apply_func(args[2], [acc, x])
    return acc


def _서식(interp, args):           # 서식(틀, ...값) — 틀의 {} 를 값으로 순서대로 치환
    template = 문자열화(args[0]) if args else ''
    vals = args[1:]
    parts = template.split('{}')
    result = parts[0]
    for k in range(1, len(parts)):
        result += (문자열화(vals[k - 1]) if k - 1 < len(vals) else '{}') + parts[k]
    return result


def _정렬기준(interp, args):       # 정렬기준(목록, 키함수) → 키함수(원소) 기준 오름차순 새 목록
    return sorted(args[0], key=lambda x: interp.apply_func(args[1], [x]))


def _최대기준(interp, args):       # 최대기준(목록, 키함수) → 키함수 값이 최대인 원소 (argmax)
    items = args[0]
    if not items:
        raise HanError('최대기준: 빈 목록')
    best = items[0]; bestk = interp.apply_func(args[1], [best])
    for x in items[1:]:
        k = interp.apply_func(args[1], [x])
        if k > bestk:
            best = x; bestk = k
    return best


def _최소기준(interp, args):       # 최소기준(목록, 키함수) → 키함수 값이 최소인 원소 (argmin)
    items = args[0]
    if not items:
        raise HanError('최소기준: 빈 목록')
    best = items[0]; bestk = interp.apply_func(args[1], [best])
    for x in items[1:]:
        k = interp.apply_func(args[1], [x])
        if k < bestk:
            best = x; bestk = k
    return best


def _찾기(interp, args):           # 찾기(목록, 함수) → 함수가 참인 첫 원소, 없으면 없음 (find)
    for x in args[0]:
        if 참인가(interp.apply_func(args[1], [x])):
            return x
    return None


def _있나(interp, args):           # 있나(목록, 함수) → 하나라도 참이면 참 (any)
    return any(참인가(interp.apply_func(args[1], [x])) for x in args[0])


def _모두(interp, args):           # 모두(목록, 함수) → 전부 참이면 참 (all)
    return all(참인가(interp.apply_func(args[1], [x])) for x in args[0])


def _고유(interp, args):           # 고유(목록) → 중복 제거(첫 등장 순서 유지)
    out = []
    for x in args[0]:
        if x not in out:
            out.append(x)
    return out


def _개수(interp, args):           # 개수(목록, 값) → 값이 목록에 나오는 횟수
    return args[0].count(args[1])


def _빈도(interp, args):           # 빈도(목록) → {원소: 개수} 사전 (첫 등장 순서)
    out = {}
    for x in args[0]:
        out[x] = out.get(x, 0) + 1
    return out


def _묶음(interp, args):           # 묶음(목록, 크기) → 크기씩 자른 부분목록들 (마지막은 짧을 수 있음)
    items = args[0]
    n = int(args[1])
    if n <= 0:
        raise HanError('묶음: 크기는 1 이상이어야 합니다')
    return [items[i:i + n] for i in range(0, len(items), n)]


def _평탄화(interp, args):         # 평탄화(목록) → 한 단계 펼침 (내부 목록은 풀고, 그 외는 그대로)
    out = []
    for x in args[0]:
        if isinstance(x, list):
            out.extend(x)
        else:
            out.append(x)
    return out


def _묶기(interp, args):           # 묶기(목록1, 목록2) → [[a,b], ...] (짧은 쪽 길이까지)
    return [[a, b] for a, b in zip(args[0], args[1])]


def _무작위(interp, args):         # 무작위() → 0.0 이상 1.0 미만 실수
    return _random.random()


def _무작위정수(interp, args):     # 무작위정수(시작, 끝) → 시작~끝 정수(양끝 포함)
    return _random.randint(args[0], args[1])


def _무작위선택(interp, args):     # 무작위선택(목록) → 목록에서 무작위 하나
    return _random.choice(args[0])


def _병합(interp, args):           # 병합(사전1, 사전2, ...) → 합친 새 사전(뒤가 우선, 원본 불변)
    out = {}
    for d in args:
        out.update(d)
    return out


def _발생(interp, args):           # 발생(메시지) → 오류 발생 (시도/잡기로 잡힘)
    raise HanError(문자열화(args[0]) if args else '오류')


def _왼쪽채우기(interp, args):     # 왼쪽채우기(값, 너비[, 채움]) → 왼쪽 채워 너비 맞춤(오른쪽 정렬)
    s = 문자열화(args[0]); fill = 문자열화(args[2]) if len(args) > 2 else ' '
    return s.rjust(int(args[1]), fill[0] if fill else ' ')


def _오른쪽채우기(interp, args):   # 오른쪽채우기(값, 너비[, 채움]) → 오른쪽 채움(왼쪽 정렬)
    s = 문자열화(args[0]); fill = 문자열화(args[2]) if len(args) > 2 else ' '
    return s.ljust(int(args[1]), fill[0] if fill else ' ')


def _교집합(interp, args):         # 교집합(가, 나) → 가에 있으면서 나에도 있는 원소(가 순서, 중복 제거)
    나 = args[1]
    out = []
    for x in args[0]:
        if x in 나 and x not in out:
            out.append(x)
    return out


def _합집합(interp, args):         # 합집합(가, 나) → 가와 나의 모든 원소(중복 제거, 가 먼저)
    out = []
    for x in list(args[0]) + list(args[1]):
        if x not in out:
            out.append(x)
    return out


def _차집합(interp, args):         # 차집합(가, 나) → 가에 있고 나엔 없는 원소(가 순서, 중복 제거)
    나 = args[1]
    out = []
    for x in args[0]:
        if x not in 나 and x not in out:
            out.append(x)
    return out


BUILTINS = {
    '출력': _출력,
    '길이': _길이,
    '숫자': _숫자,
    '추가': _추가,
    '합': _합,
    '평균': _평균,
    '중앙값': _중앙값,
    '정렬': _정렬,
    '최대': _최대,
    '최소': _최소,
    '범위': _범위,
    '나누기': _나누기,
    '위치': _위치,
    '합치기': _합치기,
    '거꾸로': _거꾸로,
    '질문': _질문,
    '체계질문': _체계질문,
    '대문자': _대문자,
    '소문자': _소문자,
    '다듬기': _다듬기,
    '바꾸기': _바꾸기,
    '포함': _포함,
    '시작': _시작,
    '끝': _끝,
    '타입': _타입,
    '절댓값': _절댓값,
    '반올림': _반올림,
    '올림': _올림,
    '내림': _내림,
    '제곱근': _제곱근,
    '거듭제곱': _거듭제곱,
    '입력': _입력,
    '키들': _키들,
    '값들': _값들,
    '항목들': _항목들,
    '값얻기': _값얻기,
    '키있나': _키있나,
    '제이슨파싱': _제이슨파싱,
    '제이슨문자열': _제이슨문자열,
    '변환': _변환,
    '거르기': _거르기,
    '접기': _접기,
    '서식': _서식,
    '정렬기준': _정렬기준,
    '최대기준': _최대기준,
    '최소기준': _최소기준,
    '찾기': _찾기,
    '있나': _있나,
    '모두': _모두,
    '고유': _고유,
    '개수': _개수,
    '빈도': _빈도,
    '묶음': _묶음,
    '평탄화': _평탄화,
    '묶기': _묶기,
    '무작위': _무작위,
    '무작위정수': _무작위정수,
    '무작위선택': _무작위선택,
    '병합': _병합,
    '발생': _발생,
    '왼쪽채우기': _왼쪽채우기,
    '오른쪽채우기': _오른쪽채우기,
    '교집합': _교집합,
    '합집합': _합집합,
    '차집합': _차집합,
}


# ============================================================ 진입점
def 실행소스(src, out=None, base_dir='.'):
    interp = Interp(out=out)
    interp.base_dir = base_dir
    interp.run(Parser(lex(src)).parse())
    return interp


def needs_more(src):
    """REPL: 블록이 안 닫혔으면(또는 문자열 미완) True → 다음 줄 계속 입력."""
    try:
        toks = lex(src)
    except HanError:
        return True
    depth = 0
    for t in toks:
        if t.kind == 'OP' and t.val == '{':
            depth += 1
        elif t.kind == 'OP' and t.val == '}':
            depth -= 1
    return depth > 0


def repl_eval(interp, src):
    """REPL용 1입력 처리: 단일 식이면 값 문자열 반환(에코), 아니면 실행 후 None."""
    ast = Parser(lex(src)).parse()
    if (ast[0] == 'block' and len(ast[1]) == 1 and ast[1][0][0] == 'stmt'
            and ast[1][0][2][0] == 'exprstmt'):
        val = interp.eval(ast[1][0][2][1], interp.g)
        return None if val is None else 문자열화(val)
    interp.run(ast)
    return None


def main(argv):
    if len(argv) >= 3 and argv[1] in ('실행', 'run'):
        with open(argv[2], encoding='utf-8') as f:
            src = f.read()
        try:
            실행소스(src, base_dir=os.path.dirname(os.path.abspath(argv[2])) or '.')
        except HanError as e:
            print(f"오류: {e}", file=sys.stderr); sys.exit(1)
    elif len(argv) == 1 or (len(argv) == 2 and argv[1] in ('repl', '대화')):
        print("한(Han) v0 · 대화형. 여러 줄 블록 OK, 식은 값이 바로 나와요. 종료는 Ctrl-D")
        interp = Interp()
        buf = ''
        while True:
            try:
                line = input('... ' if buf else '한> ')
            except EOFError:
                print(); break
            buf = (buf + '\n' + line) if buf else line
            if not buf.strip():
                buf = ''
                continue
            if needs_more(buf):       # 블록 미완 → 계속 입력
                continue
            src = buf; buf = ''
            try:
                echo = repl_eval(interp, src)
                if echo is not None:
                    print(echo)
            except (HanError, Return) as e:
                print(f"오류: {e}")
    else:
        print("사용법: python3 han.py 실행 <파일.han>  |  python3 han.py 대화", file=sys.stderr)
        sys.exit(2)


if __name__ == '__main__':
    main(sys.argv)
