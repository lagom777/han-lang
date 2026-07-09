#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""가나다 — 한글 프로그래밍 언어 · 독립 인터프리터 (v0)

진짜 새 언어다: 자체 문법 + 자체 렉서·파서·트리워킹 인터프리터로 직접 실행한다.
파이썬으로 트랜스파일하지 않는다(구현 호스트가 파이썬일 뿐, 언어는 독립적이다).

사용법:
  python3 가나다.py 실행 프로그램.ㄱㄴㄷ      # 파일 실행
  python3 가나다.py            # 대화형(REPL)
"""
import sys
import os
import math
import random as _random
import json as _json
import urllib.request
import unicodedata
from decimal import Decimal, ROUND_HALF_UP


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
        step = None
        if not self.at('OP', '{'):          # 까지 뒤가 블록이 아니면 스텝: <식> 씩
            step = self.expr()
            self.eat('ID', '씩')            # '씩' 필수 — 반복 i 를 a 부터 b 까지 N 씩 { }
        return ('for', var, first, end, self.block(), step)

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
        raise HanError(f"이름 오류: '{name}' 가 정의되지 않았습니다{self._제안(name)}")

    def _제안(self, name):                                # 가까운 이름(변수/내장) "혹시 X?" 힌트
        import difflib
        후보 = set(BUILTINS.keys())
        e = self
        while e:
            후보.update(e.vars.keys())
            e = e.parent
        가까운 = difflib.get_close_matches(name, list(후보), n=1, cutoff=0.6)
        return f" (혹시 '{가까운[0]}'?)" if 가까운 else ""

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
        self.modules = {}               # 모듈() 캐시 — 경로별 네임스페이스 사전(인터프리터당 1회 실행)
        self._거래연결 = set()          # 거래() 진행 중인 DB 연결 id — 안에서는 문장별 자동 커밋 보류

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
            step = self.eval(node[5], env) if node[5] is not None else 1
            if not isinstance(step, (int, float)):
                raise HanError("범위 반복 스텝은 숫자여야 합니다")
            if step == 0:
                raise HanError("범위 반복 스텝은 0일 수 없습니다")
            i = a
            while (i <= b) if step > 0 else (i >= b):   # 부터..까지 양끝 포함, 스텝 방향 따라
                loop = Env(env); loop.vars[node[1]] = i
                try:
                    self.exec_block(node[4], loop)
                except BreakSignal:
                    break
                except ContinueSignal:
                    pass
                i += step
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
                raise HanError(f"키 없음: {문자열화(i)}{self._키제안(obj, i)}")
            return obj[i]
        raise HanError("색인할 수 없는 값입니다")

    def _키제안(self, obj, i):                            # 사전 키 오타 "혹시 X?" 또는 있는 키 나열
        keys = [k for k in obj.keys() if isinstance(k, str)]
        if not isinstance(i, str) or not keys:
            return ""
        import difflib
        가까운 = difflib.get_close_matches(i, keys, n=1, cutoff=0.6)
        if 가까운:
            return f" (혹시 '{가까운[0]}'?)"
        보임 = keys[:6]
        return f" (있는 키: {', '.join(보임)}{'…' if len(keys) > 6 else ''})"

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
        # 고르기(조건, 참값, 거짓값) — 지연 평가 특수형(선택된 가지만 평가). 삼항 조건식 대용.
        if callee[0] == 'var' and callee[1] == '고르기' and len(node[2]) == 3:
            cond = self.eval(node[2][0], env)
            return self.eval(node[2][1] if 참인가(cond) else node[2][2], env)
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


def _표준편차(interp, args):       # 표준편차(목록) → 모표준편차(분산의 제곱근). 빈 목록은 0
    items = args[0]
    if not items:
        return 0
    m = sum(items) / len(items)
    var = sum((x - m) ** 2 for x in items) / len(items)
    return math.sqrt(var)


def _최빈값(interp, args):         # 최빈값(목록) → 가장 자주 나온 값(동률이면 먼저 등장한 것). 빈 목록 오류
    items = args[0]
    if not items:
        raise HanError('최빈값: 빈 목록')
    counts = {}
    for x in items:
        counts[x] = counts.get(x, 0) + 1
    return max(counts, key=lambda k: counts[k])  # 동률 시 삽입(첫 등장) 순 우선


def _정규화(interp, args):         # 정규화(목록) → 최소~최대를 0~1로 선형 변환(min-max). 모두 같으면 0들, 빈 목록 []
    xs = args[0]
    if not xs:
        return []
    lo, hi = min(xs), max(xs)
    if hi == lo:
        return [0 for _ in xs]
    return [(x - lo) / (hi - lo) for x in xs]


def _정렬(interp, args):           # 정렬(목록[, 내림차순]) → 정렬된 새 목록(기본 오름차순, 2번째 인자 참이면 내림차순)
    return sorted(args[0], reverse=참인가(args[1]) if len(args) > 1 else False)


def _상위(interp, args):           # 상위(목록, 개수) → 큰 순 상위 n개(내림차순 정렬 후 잘라냄). 순위표용
    n = int(args[1]) if len(args) > 1 else 1
    return sorted(args[0], reverse=True)[:max(0, n)]


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


def _간격(interp, args):           # 간격(시작, 끝, 개수) → 시작~끝 균등 분할 개수개 값(양끝 포함, linspace)
    a, b = args[0], args[1]
    n = int(args[2]) if len(args) > 2 else 0
    if n <= 1:
        return [a] if n == 1 else []
    step = (b - a) / (n - 1)
    return [a + step * i for i in range(n)]


def _나누기(interp, args):         # 문자열 나누기 → 목록
    return args[0].split(args[1]) if len(args) > 1 else args[0].split()


def _줄나누기(interp, args):       # 줄나누기(문자열) → 줄 단위 목록(\n·\r\n 처리, 끝 개행 무시). 파일·여러 줄 처리용
    return 문자열화(args[0] if args else '').splitlines()


def _위치(interp, args):           # 위치(문자열, 부분) → 부분의 첫 위치(0부터), 없으면 -1
    return 문자열화(args[0]).find(문자열화(args[1]))


def _합치기(interp, args):         # 목록 → 문자열 (구분자)
    sep = args[1] if len(args) > 1 else ''
    return sep.join(문자열화(x) for x in args[0])


def _거꾸로(interp, args):         # 거꾸로(목록|문자열) → 뒤집은 새 목록/문자열(입력 타입 보존)
    x = args[0]
    if isinstance(x, str):
        return x[::-1]
    return list(reversed(x))


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


DEFAULT_AI_MODEL = 'openai/gpt-4o-mini'


def _모델(interp, args):           # 모델([이름]) → 이름 주면 이후 질문 기본 모델 설정, 항상 현재 모델 반환
    if args:
        interp.ai_model = 문자열화(args[0])
    return getattr(interp, 'ai_model', DEFAULT_AI_MODEL)


def _질문(interp, args):           # AI에게 묻기 — 질문(프롬프트[, 모델]). OpenRouter 경유.
    prompt = 문자열화(args[0]) if args else ''
    model = args[1] if len(args) > 1 else getattr(interp, 'ai_model', DEFAULT_AI_MODEL)
    return _ai_call([{'role': 'user', 'content': prompt}], model)


def _체계질문(interp, args):       # 체계질문(시스템, 사용자[, 모델]) — 시스템 프롬프트로 AI 행동 제어
    system = 문자열화(args[0]) if args else ''
    user = 문자열화(args[1]) if len(args) > 1 else ''
    model = args[2] if len(args) > 2 else getattr(interp, 'ai_model', DEFAULT_AI_MODEL)
    return _ai_call([
        {'role': 'system', 'content': system},
        {'role': 'user', 'content': user},
    ], model)


def _분류(interp, args):           # 분류(텍스트, 보기목록[, 모델]) → 보기 중 하나로 분류(LLM). 감정·의도·카테고리
    text = 문자열화(args[0]) if args else ''
    choices = args[1] if len(args) > 1 else []
    model = args[2] if len(args) > 2 else getattr(interp, 'ai_model', DEFAULT_AI_MODEL)
    labels = ', '.join(문자열화(c) for c in choices)
    system = '다음 텍스트를 주어진 보기 중 정확히 하나로 분류하고, 그 보기 라벨만 출력하세요.'
    user = '텍스트: ' + text + '\n보기: ' + labels
    return _ai_call([
        {'role': 'system', 'content': system},
        {'role': 'user', 'content': user},
    ], model)


def _요약(interp, args):           # 요약(텍스트[, 문장수][, 모델]) → LLM으로 N문장 요약. 대표 AI 작업.
    text = 문자열화(args[0]) if args else ''
    n = args[1] if len(args) > 1 else 3
    model = args[2] if len(args) > 2 else getattr(interp, 'ai_model', DEFAULT_AI_MODEL)
    system = '다음 텍스트를 핵심만 담아 ' + 문자열화(n) + '문장 이내로 간결히 요약하세요. 요약문만 출력하세요.'
    return _ai_call([
        {'role': 'system', 'content': system},
        {'role': 'user', 'content': text},
    ], model)


def _번역(interp, args):           # 번역(텍스트, 목표언어[, 모델]) → LLM으로 번역. 대표 AI 작업.
    text = 문자열화(args[0]) if args else ''
    lang = 문자열화(args[1]) if len(args) > 1 else '영어'
    model = args[2] if len(args) > 2 else getattr(interp, 'ai_model', DEFAULT_AI_MODEL)
    system = '다음 텍스트를 ' + lang + '(으)로 자연스럽게 번역하세요. 번역문만 출력하세요.'
    return _ai_call([
        {'role': 'system', 'content': system},
        {'role': 'user', 'content': text},
    ], model)


def _추출(interp, args):           # 추출(텍스트, 지시[, 모델]) → 텍스트에서 지시대로 정보만 뽑기(LLM). 구조화 대표 작업.
    text = 문자열화(args[0]) if args else ''
    instruction = 문자열화(args[1]) if len(args) > 1 else '핵심 정보'
    model = args[2] if len(args) > 2 else getattr(interp, 'ai_model', DEFAULT_AI_MODEL)
    system = '다음 텍스트에서 요청한 정보만 뽑아 값만 출력하세요. 설명·문장 없이 값만.'
    user = '요청: ' + instruction + '\n텍스트: ' + text
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


def _말줄임(interp, args):          # 말줄임(문자열, 최대길이) → 길면 잘라 "…" 붙임(UI 표시·프롬프트 트림)
    s = 문자열화(args[0])
    n = int(args[1]) if len(args) > 1 else 0
    return s if len(s) <= n else s[:max(0, n)] + "…"


def _바꾸기(interp, args):          # 바꾸기(문자열, 옛, 새)
    return args[0].replace(args[1], args[2])


def _포함(interp, args):           # 포함(컨테이너, 값) → 참/거짓 (문자열·목록·사전)
    return args[1] in args[0]


def _시작(interp, args):
    return args[0].startswith(args[1])


def _끝(interp, args):
    return args[0].endswith(args[1])


def _숫자인가(interp, args):        # 숫자인가(값) → 숫자로 볼 수 있으면 참(입력 검증용). 숫자 자체 or 숫자 문자열
    v = args[0] if args else None
    if isinstance(v, bool):
        return False                # 참/거짓은 숫자가 아님
    if isinstance(v, (int, float)):
        return True
    if isinstance(v, str):
        s = v.strip()
        if not s:
            return False
        try:
            float(s)
            return True
        except ValueError:
            return False
    return False


def _글자인가(interp, args):        # 글자인가(문자열) → 모두 글자면 참(숫자·공백·기호 섞이면 거짓, 빈 문자열 거짓). 한글 포함
    s = args[0] if args else None
    return isinstance(s, str) and len(s) > 0 and s.isalpha()


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


def _반올림(interp, args):          # 반올림(수[, 소수자리]) — 한국식 사사오입(5는 올림, 0에서 멀어지는 방향)
    자리 = int(args[1]) if len(args) > 1 else 0
    결과 = Decimal(str(args[0])).quantize(Decimal(1).scaleb(-자리), rounding=ROUND_HALF_UP)
    return float(결과) if len(args) > 1 else int(결과)


def _천단위(interp, args):          # 천단위(숫자) → 천 단위 콤마 문자열 (예: 1234567 → "1,234,567")
    return f"{args[0]:,}"


def _사이값(interp, args):          # 사이값(값, 최소, 최대) → 값을 [최소,최대]로 제한 (clamp)
    v, lo, hi = args[0], args[1], args[2]
    return lo if v < lo else hi if v > hi else v


def _최대공약수(interp, args):      # 최대공약수(가, 나) → GCD
    return math.gcd(int(args[0]), int(args[1]))


def _최소공배수(interp, args):      # 최소공배수(가, 나) → LCM (둘 중 0이면 0)
    a, b = int(args[0]), int(args[1])
    return abs(a * b) // math.gcd(a, b) if a and b else 0


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


def _파일읽기(interp, args):       # 파일읽기(경로) → 파일 내용(문자열). 없거나 못 읽으면 오류
    with open(문자열화(args[0]), encoding='utf-8') as f:
        return f.read()


def _파일쓰기(interp, args):       # 파일쓰기(경로, 내용) → 내용을 파일에 씀(덮어씀), 쓴 글자 수 반환
    s = 문자열화(args[1]) if len(args) > 1 else ''
    with open(문자열화(args[0]), 'w', encoding='utf-8') as f:
        f.write(s)
    return len(s)


def _이어쓰기(interp, args):       # 이어쓰기(경로, 내용) → 파일 끝에 덧붙임(없으면 생성), 쓴 글자 수 (로그 누적)
    s = 문자열화(args[1]) if len(args) > 1 else ''
    with open(문자열화(args[0]), 'a', encoding='utf-8') as f:
        f.write(s)
    return len(s)


def _파일존재(interp, args):       # 파일존재(경로) → 참/거짓 (파일·디렉터리 존재 여부)
    return os.path.exists(문자열화(args[0]))


def _파일목록(interp, args):       # 파일목록(경로) → 디렉터리 안 항목 이름 목록(정렬). 폴더 일괄 처리용
    return sorted(os.listdir(문자열화(args[0])))


def _환경변수(interp, args):       # 환경변수(이름[, 기본값]) → 값, 없으면 기본값(미지정 시 없음)
    name = 문자열화(args[0]) if args else ''
    return os.environ.get(name, args[1] if len(args) > 1 else None)


def _모듈(interp, args):           # 모듈(경로) → 사전{이름:값}. 파일을 격리 환경에서 실행해 최상위 정의(함수·변수)를
    # 네임스페이스 사전으로 반환(호출측 이름 오염 없음 — 플랫 가져오기와 대비). 경로는 가져오기처럼 현재 파일 기준.
    # 같은 경로는 인터프리터당 1회만 실행하고 같은 사전을 돌려준다(캐시 → 상태 공유).
    path = 문자열화(args[0])
    full = os.path.normpath(os.path.join(interp.base_dir, path))
    if full in interp.modules:
        return interp.modules[full]
    try:
        with open(full, encoding='utf-8') as f:
            src = f.read()
    except OSError:
        raise HanError(f"모듈을 불러올 수 없습니다: {path}")
    env = Env()                                     # 격리 환경 — 전역과 부모 사슬 없음
    prev_dir, prev_line = interp.base_dir, interp.cur_line
    interp.base_dir = os.path.dirname(full) or '.'  # 모듈 안 상대경로는 모듈 기준
    try:
        interp.exec_block(Parser(lex(src)).parse(), env)
    except HanError as e:
        raise HanError(f"모듈 '{path}' 오류 — {e}") from None
    finally:
        interp.base_dir, interp.cur_line = prev_dir, prev_line
    interp.modules[full] = env.vars                 # env.vars 그대로 = 모듈 함수의 클로저 환경(살아있는 상태)
    return env.vars


def _지금(interp, args):           # 지금() → 현재 유닉스 시각(초, 소수). 타임스탬프·경과 측정용
    import time
    return time.time()


def _키들(interp, args):           # 사전의 키 목록
    return list(args[0].keys())


def _값들(interp, args):           # 사전의 값 목록
    return list(args[0].values())


def _항목들(interp, args):         # 사전의 [키, 값] 목록
    return [[k, v] for k, v in args[0].items()]


def _사전만들기(interp, args):     # 사전만들기(키목록, 값목록) → {키:값} 사전(짧은 쪽까지, 항목들의 역)
    return {k: v for k, v in zip(args[0], args[1])}


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


def _생성(interp, args):           # 생성(개수, 함수) → [함수(0), 함수(1), ..., 함수(개수-1)] (Array.from)
    n = int(args[0])
    return [interp.apply_func(args[1], [i]) for i in range(n)]


def _거르기(interp, args):         # 거르기(목록, 함수) → 함수가 참인 원소만 (filter)
    return [x for x in args[0] if 참인가(interp.apply_func(args[1], [x]))]


def _분할(interp, args):           # 분할(목록, 함수) → [함수 참인 것들, 거짓인 것들] (partition)
    yes, no = [], []
    for x in args[0]:
        (yes if 참인가(interp.apply_func(args[1], [x])) else no).append(x)
    return [yes, no]


def _그룹화(interp, args):         # 그룹화(목록, 키함수) → {키: [원소들]} (groupby, 첫 등장 순)
    out = {}
    for x in args[0]:
        k = interp.apply_func(args[1], [x])
        out.setdefault(k, []).append(x)
    return out


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


def _정렬기준(interp, args):       # 정렬기준(목록, 키함수[, 내림차순]) → 키함수 기준 정렬(기본 오름차순, 3번째 참이면 내림차순)
    return sorted(args[0], key=lambda x: interp.apply_func(args[1], [x]), reverse=참인가(args[2]) if len(args) > 2 else False)


def _상위기준(interp, args):       # 상위기준(목록, 개수, 키함수) → 키함수 값 큰 순 상위 n개(원소 유지). 객체 순위표
    n = int(args[1]) if len(args) > 1 else 1
    return sorted(args[0], key=lambda x: interp.apply_func(args[2], [x]), reverse=True)[:max(0, n)]


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
    if args and isinstance(args[0], 저장소핸들):   # 찾기(저장소, 조건사전) → 조건 키=값 전부 일치하는 기록 목록
        조건 = args[1] if len(args) > 1 else {}
        if not isinstance(조건, dict):
            raise HanError('찾기: 저장소 검색 조건은 사전이어야 합니다 — 찾기(저장소, {"키": 값})')
        return [d for d in _저장소_전부(args[0])
                if all(k in d and d[k] == v for k, v in 조건.items())]
    for x in args[0]:
        if 참인가(interp.apply_func(args[1], [x])):
            return x
    return None


def _있나(interp, args):           # 있나(목록, 함수) → 하나라도 참이면 참 (any)
    return any(참인가(interp.apply_func(args[1], [x])) for x in args[0])


def _모두(interp, args):           # 모두(목록, 함수) → 전부 참이면 참 (all)
    if args and isinstance(args[0], 저장소핸들):   # 모두(저장소) → 모든 기록 목록(각 사전에 "번호")
        return _저장소_전부(args[0])
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


def _누적합(interp, args):         # 누적합(목록) → 누적 합 목록 [a, a+b, a+b+c, ...] (running total)
    out = []
    s = 0
    for x in args[0]:
        s += x
        out.append(s)
    return out


def _누적곱(interp, args):         # 누적곱(목록) → 누적 곱 목록 [a, a*b, a*b*c, ...] (running product)
    out = []
    p = 1
    for x in args[0]:
        p *= x
        out.append(p)
    return out


def _회전(interp, args):           # 회전(목록, 칸수) → 왼쪽으로 칸수만큼 회전한 새 목록(음수면 오른쪽, 길이 초과는 나머지 처리)
    xs = list(args[0])
    n = len(xs)
    if n == 0:
        return []
    k = int(args[1]) % n if len(args) > 1 else 0
    return xs[k:] + xs[:k]


def _전치(interp, args):           # 전치(2차원목록) → 행과 열을 바꾼 목록 (transpose). 짧은 행 기준
    rows = args[0]
    if not rows:
        return []
    return [list(col) for col in zip(*rows)]


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


def _가운데채우기(interp, args):   # 가운데채우기(값, 너비[, 채움]) → 양쪽 채움(가운데 정렬, 홀수는 오른쪽에)
    s = 문자열화(args[0]); fill = 문자열화(args[2]) if len(args) > 2 else ' '
    return s.center(int(args[1]), fill[0] if fill else ' ')


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


def _웹서버만들기(interp, 포트, 라우트, 정적폴더=None):
    """포트·라우트(경로 문자열 → 가나다 함수)로 HTTPServer 를 구성해 돌려준다(아직 serve 안 함).
    핸들러는 요청 사전 {메서드,경로,질의,본문,쿠키} 하나를 받아, 문자열(→200 text/html) 또는
    사전 {상태,헤더,본문[,쿠키설정]} 을 반환한다. 없는 경로는 404. 테스트·서버 빌트인이 공유.
    쿠키설정 {이름:값} 은 각각 Set-Cookie(Path=/; HttpOnly, 퍼센트 인코딩)로 나간다.
    정적폴더를 주면 라우트에 없는 경로는 그 폴더의 파일로 서빙(라우트가 우선)."""
    import http.server
    import urllib.parse

    기본헤더 = {'Content-Type': 'text/html; charset=utf-8'}
    타입표 = {'.html': 'text/html; charset=utf-8', '.css': 'text/css; charset=utf-8',
              '.js': 'text/javascript; charset=utf-8', '.png': 'image/png',
              '.jpg': 'image/jpeg', '.jpeg': 'image/jpeg', '.svg': 'image/svg+xml',
              '.ico': 'image/x-icon', '.json': 'application/json; charset=utf-8',
              '.txt': 'text/plain; charset=utf-8'}

    class _핸들러(http.server.BaseHTTPRequestHandler):
        def _응답(self, 상태, 헤더, 본문, 쿠키설정=None):
            데이터 = 본문 if isinstance(본문, bytes) else 문자열화(본문).encode('utf-8')
            self.send_response(int(상태))
            for k, v in 헤더.items():
                self.send_header(문자열화(k), 문자열화(v))
            if isinstance(쿠키설정, dict):
                for k, v in 쿠키설정.items():        # 이름·값 퍼센트 인코딩(한글 안전)
                    self.send_header('Set-Cookie', urllib.parse.quote(문자열화(k), safe='')
                                     + '=' + urllib.parse.quote(문자열화(v), safe='')
                                     + '; Path=/; HttpOnly')
            self.send_header('Content-Length', str(len(데이터)))
            self.end_headers()
            self.wfile.write(데이터)

        def _처리(self):
            parts = urllib.parse.urlsplit(self.path)
            경로 = urllib.parse.unquote(parts.path)
            질의 = {k: v[0] for k, v in urllib.parse.parse_qs(parts.query).items()}
            길이 = int(self.headers.get('Content-Length') or 0)
            본문 = self.rfile.read(길이).decode('utf-8') if 길이 else ''
            if self.command == 'POST' and (self.headers.get('Content-Type') or '').startswith('application/x-www-form-urlencoded'):
                질의.update({k: v[0] for k, v in urllib.parse.parse_qs(본문).items()})   # POST 폼 필드도 질의로(본문 값 우선), 본문은 그대로 둔다
            쿠키 = {}
            for 쌍 in (self.headers.get('Cookie') or '').split(';'):
                이름, _, 값 = 쌍.strip().partition('=')
                if 이름:
                    쿠키[urllib.parse.unquote(이름)] = urllib.parse.unquote(값)
            요청 = {'메서드': self.command, '경로': 경로, '질의': 질의, '본문': 본문, '쿠키': 쿠키}
            fn = 라우트.get(경로)
            if fn is None:
                if 정적폴더 is not None:
                    self._정적(경로)
                else:
                    self._응답(404, 기본헤더, '404 없는 경로: ' + 경로)
                return
            try:
                결과 = interp.apply_func(fn, [요청])
            except HanError as e:
                self._응답(500, 기본헤더, '500 서버 오류: ' + str(e))
                return
            if isinstance(결과, dict):
                self._응답(결과.get('상태', 200), 결과.get('헤더') or 기본헤더, 결과.get('본문', ''), 결과.get('쿠키설정'))
            else:
                self._응답(200, 기본헤더, 결과)

        def _정적(self, 경로):        # 정적폴더 안의 파일 서빙. 경로 이탈(../)은 realpath 검증으로 차단 → 404
            if 경로.endswith('/'):
                경로 += 'index.html'
            뿌리 = os.path.realpath(정적폴더)
            실경로 = os.path.realpath(os.path.join(뿌리, 경로.lstrip('/')))
            if not (실경로 == 뿌리 or 실경로.startswith(뿌리 + os.sep)) or not os.path.isfile(실경로):
                self._응답(404, 기본헤더, '404 없는 경로: ' + 경로)
                return
            with open(실경로, 'rb') as f:
                데이터 = f.read()
            확장자 = os.path.splitext(실경로)[1].lower()
            self._응답(200, {'Content-Type': 타입표.get(확장자, 'application/octet-stream')}, 데이터)

        do_GET = _처리
        do_POST = _처리

        def log_message(self, *a):      # 기본 요청 로그 억제(시작 줄만 남긴다)
            pass

    return http.server.HTTPServer(('', int(포트)), _핸들러)


def _서버(interp, args):           # 서버(포트, 라우트[, 정적폴더]) — 라우트{경로:함수}로 HTTP 서비스(블로킹). 포트 0이면 임의 포트. 정적폴더를 주면 라우트 밖 경로는 폴더 파일 서빙.
    포트 = int(args[0]) if args else 8000
    라우트 = args[1] if len(args) > 1 else {}
    if not isinstance(라우트, dict):
        raise HanError('서버: 라우트는 {경로:함수} 사전이어야 합니다')
    정적폴더 = 문자열화(args[2]) if len(args) > 2 else None
    httpd = _웹서버만들기(interp, 포트, 라우트, 정적폴더)
    interp.out.write('가나다 서버: http://localhost:' + str(httpd.server_address[1]) + '\n')
    interp.out.flush()
    try:
        httpd.serve_forever()
    except KeyboardInterrupt:
        pass
    finally:
        httpd.server_close()
    return None


def _자료핸들(이름, v):            # 실행·질의·자료닫기 공용 — 자료열기() 핸들인지 검증
    import sqlite3
    if not isinstance(v, sqlite3.Connection):
        raise HanError(f"{이름}: 첫 인자는 자료열기() 가 돌려준 핸들이어야 합니다")
    return v


def _자료인자(이름, args):         # 실행·질의 공용 — 선택적 인자목록(? 자리에 순서대로) 검증
    인자 = args[2] if len(args) > 2 else []
    if not isinstance(인자, list):
        raise HanError(f"{이름}: 인자목록은 목록이어야 합니다 — SQL 의 ? 자리에 순서대로 들어갑니다")
    return 인자


def _자료열기(interp, args):       # 자료열기(경로) → SQLite 핸들(실행·질의에 넘김, 자료닫기로 닫음). ":memory:" 는 메모리 전용
    import sqlite3
    경로 = 문자열화(args[0])
    try:
        conn = sqlite3.connect(경로, check_same_thread=False)   # 서버 라우트(다른 스레드)에서도 쓸 수 있게
    except sqlite3.Error as e:
        raise HanError(f"자료열기 오류: '{경로}' — {e}")
    conn.row_factory = sqlite3.Row
    return conn


def _커밋(interp, conn):           # 거래() 안이면 커밋 보류(끝에 한꺼번에), 아니면 문장마다 자동 커밋
    if id(conn) not in interp._거래연결:
        conn.commit()


def _실행(interp, args):           # 실행(핸들, SQL[, 인자목록]) → 변경 행 수(CREATE 등은 0). 문장마다 자동 커밋
    import sqlite3
    conn = _자료핸들('실행', args[0])
    인자 = _자료인자('실행', args)
    try:
        cur = conn.execute(문자열화(args[1]), 인자)
        _커밋(interp, conn)
    except sqlite3.Error as e:
        raise HanError(f"실행 오류: {e} (값 끼워넣기는 문자열 잇기 대신 ? 와 인자목록을 쓰세요)")
    return max(cur.rowcount, 0)


def _질의(interp, args):           # 질의(핸들, SQL[, 인자목록]) → 행 목록(각 행은 {컬럼명: 값} 사전)
    import sqlite3
    conn = _자료핸들('질의', args[0])
    인자 = _자료인자('질의', args)
    try:
        rows = conn.execute(문자열화(args[1]), 인자).fetchall()
    except sqlite3.Error as e:
        raise HanError(f"질의 오류: {e} (값 끼워넣기는 문자열 잇기 대신 ? 와 인자목록을 쓰세요)")
    return [dict(r) for r in rows]


def _자료닫기(interp, args):       # 자료닫기(핸들) → 없음. 다 쓰면 닫아 파일 잠금 해제
    _자료핸들('자료닫기', args[0]).close()
    return None


# ------------------------------------------------------------ 한 몸 풀스택 ① 저장소 (SQL 숨김)
class 저장소핸들:
    """저장소() 가 돌려주는 값 — SQLite 연결 + 표 이름. SQL 없이 넣기/모두/찾기/고치기/빼기로 쓴다."""
    __slots__ = ('연결', '이름')

    def __init__(self, 연결, 이름):
        self.연결, self.이름 = 연결, 이름

    def __repr__(self):
        return f"<저장소 {self.이름}>"


def _저장소검증(이름, v):
    if not isinstance(v, 저장소핸들):
        raise HanError(f"{이름}: 첫 인자는 저장소() 가 돌려준 저장소여야 합니다")
    return v


def _저장소(interp, args):         # 저장소(경로, 이름) → 저장소. 파일(":memory:" 는 메모리 전용)에 이름 표를 만들어(없으면) 연다
    if len(args) < 2:
        raise HanError('저장소: 저장소(경로, 이름) — 파일 경로와 저장소 이름 둘 다 필요합니다')
    이름 = 문자열화(args[1])
    if not 이름.isidentifier():
        raise HanError(f"저장소: 이름은 한글·영문·숫자·_ 로 된 한 단어여야 합니다 — '{이름}'")
    연결 = _자료열기(interp, [args[0]])   # 자료열기 내부 재사용(오류 안내·다른 스레드 허용 포함)
    연결.execute(f'CREATE TABLE IF NOT EXISTS "{이름}" (id INTEGER PRIMARY KEY AUTOINCREMENT, 자료 TEXT)')
    연결.commit()
    return 저장소핸들(연결, 이름)


def _넣기(interp, args):           # 넣기(저장소, 사전) → 번호. 사전을 통째로 저장(한글 키·값 그대로)
    저장 = _저장소검증('넣기', args[0])
    사전 = args[1] if len(args) > 1 else None
    if not isinstance(사전, dict):
        raise HanError('넣기: 두 번째 인자는 사전이어야 합니다 — 넣기(저장소, {"키": 값})')
    기록 = {k: v for k, v in 사전.items() if k != '번호'}   # 번호는 저장소가 매긴다
    cur = 저장.연결.execute(f'INSERT INTO "{저장.이름}" (자료) VALUES (?)',
                          [_json.dumps(기록, ensure_ascii=False)])
    _커밋(interp, 저장.연결)
    return cur.lastrowid


def _저장소_전부(저장):            # 내부 공용 — 모든 기록을 [사전] 으로 (각 사전에 "번호" 부여, 번호 순)
    rows = 저장.연결.execute(f'SELECT id, 자료 FROM "{저장.이름}" ORDER BY id').fetchall()
    out = []
    for r in rows:
        d = _json.loads(r['자료'])
        d['번호'] = r['id']
        out.append(d)
    return out


def _고치기(interp, args):         # 고치기(저장소, 번호, 사전) → 성공여부. 기록에 사전의 키들만 덮어씀(부분 수정)
    저장 = _저장소검증('고치기', args[0])
    if len(args) < 3 or not isinstance(args[2], dict):
        raise HanError('고치기: 고치기(저장소, 번호, 사전) — 번호와 고칠 내용 사전이 필요합니다')
    번호 = int(args[1])
    row = 저장.연결.execute(f'SELECT 자료 FROM "{저장.이름}" WHERE id = ?', [번호]).fetchone()
    if row is None:
        return False
    d = _json.loads(row['자료'])
    d.update(args[2])
    d.pop('번호', None)             # 번호는 저장 안 함(id 가 원본)
    저장.연결.execute(f'UPDATE "{저장.이름}" SET 자료 = ? WHERE id = ?',
                    [_json.dumps(d, ensure_ascii=False), 번호])
    _커밋(interp, 저장.연결)
    return True


def _빼기(interp, args):           # 빼기(저장소, 번호) → 성공여부. 그 번호 기록 삭제
    저장 = _저장소검증('빼기', args[0])
    if len(args) < 2:
        raise HanError('빼기: 빼기(저장소, 번호) — 지울 기록의 번호가 필요합니다')
    cur = 저장.연결.execute(f'DELETE FROM "{저장.이름}" WHERE id = ?', [int(args[1])])
    _커밋(interp, 저장.연결)
    return cur.rowcount > 0


def _거래(interp, args):           # 거래(핸들, 함수) → 함수 안 DB 작업을 하나로 묶어 성공 시 커밋, 오류 시 전부 롤백. 핸들은 자료열기()·저장소() 둘 다 OK. (v1: 중첩 미지원)
    import sqlite3
    핸들 = args[0] if args else None
    함수 = args[1] if len(args) > 1 else None
    conn = 핸들.연결 if isinstance(핸들, 저장소핸들) else 핸들
    if not isinstance(conn, sqlite3.Connection):
        raise HanError('거래: 첫 인자는 자료열기() 핸들이나 저장소() 여야 합니다')
    interp._거래연결.add(id(conn))
    try:
        interp.apply_func(함수, [])
        conn.commit()
    except HanError:
        conn.rollback()
        raise
    finally:
        interp._거래연결.discard(id(conn))
    return None


# ------------------------------------------------------------ 한 몸 풀스택 ② HTML 도우미 (HTML 숨김)
class HTML조각(str):
    """문서·글·목록·연결·입력폼이 돌려주는 완성된 HTML — 문서에 넣을 때 이스케이프를 통과한다.
    일반 문자열은 항상 이스케이프되므로 사용자 입력을 그대로 넣어도 안전(XSS 차단)."""
    __slots__ = ()


def _HTML이스케이프(v):            # 일반 값 → HTML 안전 문자열, HTML조각은 그대로 통과
    if isinstance(v, HTML조각):
        return str(v)
    s = 문자열화(v)
    return (s.replace('&', '&amp;').replace('<', '&lt;').replace('>', '&gt;')
             .replace('"', '&quot;').replace("'", '&#39;'))


_문서스타일 = ('body{font-family:sans-serif;max-width:40em;margin:2em auto;padding:0 1em;line-height:1.6}'
              'input,button{font:inherit;padding:.3em .6em;margin:.2em 0}'
              'button{cursor:pointer}ul{padding-left:1.2em}')


def _문서(interp, args):           # 문서(제목, ...본문조각) → 완성된 HTML 페이지(UTF-8·기본 스타일). 조각은 글/목록/연결/입력폼 결과나 문자열
    제목 = _HTML이스케이프(args[0] if args else '')
    본문 = '\n'.join(_HTML이스케이프(a) for a in args[1:])
    return HTML조각('<!DOCTYPE html><html lang="ko"><head><meta charset="utf-8">'
                   '<meta name="viewport" content="width=device-width, initial-scale=1">'
                   f'<title>{제목}</title><style>{_문서스타일}</style></head>'
                   f'<body>\n{본문}\n</body></html>')


def _글(interp, args):             # 글(텍스트[, 크기]) → 문단(<p>). 크기 1~3 이면 제목(<h1>~<h3>)
    텍스트 = _HTML이스케이프(args[0] if args else '')
    크기 = int(args[1]) if len(args) > 1 else 0
    태그 = f'h{크기}' if 크기 in (1, 2, 3) else 'p'
    return HTML조각(f'<{태그}>{텍스트}</{태그}>')


def _목록태그(interp, args):       # 목록(문자열목록) → 점 목록(<ul>). 각 항목 이스케이프(조각은 그대로)
    항목들 = args[0] if args else []
    if not isinstance(항목들, list):
        raise HanError('목록: 인자는 목록이어야 합니다 — 목록(["가", "나"])')
    return HTML조각('<ul>' + ''.join('<li>' + _HTML이스케이프(x) + '</li>' for x in 항목들) + '</ul>')


def _연결(interp, args):           # 연결(주소, 텍스트) → 링크(<a>)
    주소 = _HTML이스케이프(args[0] if args else '')
    텍스트 = _HTML이스케이프(args[1] if len(args) > 1 else (args[0] if args else ''))
    return HTML조각(f'<a href="{주소}">{텍스트}</a>')


def _입력폼(interp, args):         # 입력폼(주소, 필드이름목록[, 버튼텍스트]) → 입력 폼. 보내면 주소로 GET → 요청["질의"] 에 {필드:값}
    주소 = _HTML이스케이프(args[0] if args else '')
    필드들 = args[1] if len(args) > 1 else []
    if not isinstance(필드들, list):
        raise HanError('입력폼: 필드이름목록은 목록이어야 합니다 — 입력폼("/등록", ["이름", "내용"])')
    버튼 = _HTML이스케이프(args[2] if len(args) > 2 else '보내기')
    칸들 = ''.join(f'<label>{_HTML이스케이프(f)} <input name="{_HTML이스케이프(f)}"></label><br>'
                  for f in 필드들)
    return HTML조각(f'<form action="{주소}" method="get">{칸들}<button>{버튼}</button></form>')


BUILTINS = {
    '출력': _출력,
    '길이': _길이,
    '숫자': _숫자,
    '추가': _추가,
    '합': _합,
    '평균': _평균,
    '중앙값': _중앙값,
    '표준편차': _표준편차,
    '최빈값': _최빈값,
    '정규화': _정규화,
    '정렬': _정렬,
    '상위': _상위,
    '최대': _최대,
    '최소': _최소,
    '범위': _범위,
    '간격': _간격,
    '나누기': _나누기,
    '줄나누기': _줄나누기,
    '위치': _위치,
    '합치기': _합치기,
    '거꾸로': _거꾸로,
    '모델': _모델,
    '질문': _질문,
    '체계질문': _체계질문,
    '분류': _분류,
    '요약': _요약,
    '번역': _번역,
    '추출': _추출,
    '대문자': _대문자,
    '소문자': _소문자,
    '다듬기': _다듬기,
    '말줄임': _말줄임,
    '바꾸기': _바꾸기,
    '포함': _포함,
    '시작': _시작,
    '끝': _끝,
    '숫자인가': _숫자인가,
    '글자인가': _글자인가,
    '타입': _타입,
    '절댓값': _절댓값,
    '반올림': _반올림,
    '천단위': _천단위,
    '사이값': _사이값,
    '최대공약수': _최대공약수,
    '최소공배수': _최소공배수,
    '올림': _올림,
    '내림': _내림,
    '제곱근': _제곱근,
    '거듭제곱': _거듭제곱,
    '입력': _입력,
    '파일읽기': _파일읽기,
    '파일쓰기': _파일쓰기,
    '이어쓰기': _이어쓰기,
    '파일존재': _파일존재,
    '파일목록': _파일목록,
    '환경변수': _환경변수,
    '모듈': _모듈,
    '지금': _지금,
    '키들': _키들,
    '값들': _값들,
    '항목들': _항목들,
    '값얻기': _값얻기,
    '키있나': _키있나,
    '제이슨파싱': _제이슨파싱,
    '제이슨문자열': _제이슨문자열,
    '변환': _변환,
    '생성': _생성,
    '거르기': _거르기,
    '분할': _분할,
    '그룹화': _그룹화,
    '접기': _접기,
    '서식': _서식,
    '정렬기준': _정렬기준,
    '상위기준': _상위기준,
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
    '누적합': _누적합,
    '누적곱': _누적곱,
    '회전': _회전,
    '전치': _전치,
    '무작위': _무작위,
    '무작위정수': _무작위정수,
    '무작위선택': _무작위선택,
    '병합': _병합,
    '사전만들기': _사전만들기,
    '발생': _발생,
    '왼쪽채우기': _왼쪽채우기,
    '오른쪽채우기': _오른쪽채우기,
    '가운데채우기': _가운데채우기,
    '교집합': _교집합,
    '합집합': _합집합,
    '차집합': _차집합,
    '서버': _서버,
    '자료열기': _자료열기,
    '실행': _실행,
    '질의': _질의,
    '자료닫기': _자료닫기,
    '저장소': _저장소,
    '넣기': _넣기,
    '고치기': _고치기,
    '빼기': _빼기,
    '거래': _거래,
    '문서': _문서,
    '글': _글,
    '목록': _목록태그,
    '연결': _연결,
    '입력폼': _입력폼,
}


# ============================================================ 진입점
def _표시폭(s):
    """터미널 표시 폭 — 한글 등 East-Asian Wide/Fullwidth는 2칸. 캐럿 정렬용."""
    return sum(2 if unicodedata.east_asian_width(c) in ('W', 'F') else 1 for c in s)


def _소스줄_붙이기(msg, src):
    """오류 메시지 앞의 [N행 ...] / [N행 M열 ...] 에서 위치를 뽑아 그 소스 줄을
    아래에 덧붙인다. 열 정보가 있으면 그 자리를 가리키는 ^ 캐럿도 추가(한글 폭 보정).
    행 정보가 없거나(범위 밖·빈 줄) 이미 붙어 있으면 원문 그대로."""
    if not msg.startswith('[') or '\n' in msg:
        return msg
    j = 1
    while j < len(msg) and msg[j].isdigit():
        j += 1
    if j == 1 or not msg[j:].startswith('행'):
        return msg
    n = int(msg[1:j])
    # 선택적 열 번호 파싱: "행 M열"
    col = None
    k = j + len('행')
    while k < len(msg) and msg[k] == ' ':
        k += 1
    d = k
    while d < len(msg) and msg[d].isdigit():
        d += 1
    if d > k and msg[d:].startswith('열'):
        col = int(msg[k:d])
    lines = src.split('\n')
    if not (1 <= n <= len(lines)):
        return msg
    코드 = lines[n - 1].rstrip()          # 앞 들여쓰기는 유지(캐럿 정렬)
    if not 코드.strip():
        return msg
    prefix = f"  {n} | "
    out = f"{msg}\n{prefix}{코드}"
    if col is not None and 1 <= col <= len(코드) + 1:
        out += "\n" + " " * (len(prefix) + _표시폭(코드[:col - 1])) + "^"
    return out


def 실행소스(src, out=None, base_dir='.'):
    interp = Interp(out=out)
    interp.base_dir = base_dir
    try:
        interp.run(Parser(lex(src)).parse())
    except HanError as e:                       # 렉서·파서·런타임 오류에 문제의 소스 줄 표시
        raise HanError(_소스줄_붙이기(str(e), src)) from None
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


def repl_command(line, interp=None):
    """REPL 메타 명령(:으로 시작). 일반 코드면 None, 명령이면 (동작, 출력문자열)."""
    cmd = line.strip()
    if not cmd.startswith(':'):
        return None
    if cmd in (':종료', ':끝'):
        return ('quit', None)
    if cmd == ':도움':
        return ('print', "명령: :도움(도움말) :변수(정의된 변수) :비우기(변수 초기화) :종료(끝내기)\n내장함수: " + ', '.join(sorted(BUILTINS.keys())))
    if cmd in (':비우기', ':초기화'):
        if interp is None:
            return ('print', "(초기화할 수 없어요)")
        n = len(interp.g.vars)
        interp.g.vars.clear()          # 사용자 정의 변수·함수만 삭제(내장함수는 BUILTINS라 유지)
        return ('print', f"변수 {n}개를 지웠어요.")
    if cmd in (':변수', ':환경'):
        if interp is None:
            return ('print', "(변수 정보를 볼 수 없어요)")
        vs = interp.g.vars
        if not vs:
            return ('print', "정의된 변수가 없어요")
        def _repr(v):
            s = 문자열화(v)
            return s if len(s) <= 50 else s[:50] + '…'
        return ('print', '\n'.join(f"{k} = {_repr(v)}" for k, v in vs.items()))
    return ('print', f"알 수 없는 명령: {cmd} (:도움 으로 목록)")


def main(argv):
    if len(argv) >= 3 and argv[1] in ('실행', 'run'):
        with open(argv[2], encoding='utf-8') as f:
            src = f.read()
        try:
            실행소스(src, base_dir=os.path.dirname(os.path.abspath(argv[2])) or '.')
        except HanError as e:
            print(f"오류: {e}", file=sys.stderr); sys.exit(1)
    elif len(argv) == 1 or (len(argv) == 2 and argv[1] in ('repl', '대화')):
        print("가나다 v0 · 대화형. 여러 줄 블록 OK, 식은 값이 바로 나와요. :도움 으로 명령, 종료는 :종료/Ctrl-D")
        interp = Interp()
        buf = ''
        while True:
            try:
                line = input('... ' if buf else '가나다> ')
            except EOFError:
                print(); break
            if not buf:                   # 블록 중이 아니면 메타 명령 처리(:도움 :변수 :종료)
                c = repl_command(line, interp)
                if c:
                    if c[0] == 'quit':
                        break
                    print(c[1]); continue
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
        print("사용법: python3 가나다.py 실행 <파일.ㄱㄴㄷ>  |  python3 가나다.py 대화", file=sys.stderr)
        sys.exit(2)


if __name__ == '__main__':
    main(sys.argv)
