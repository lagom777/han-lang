#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""C 구현 컨포먼스 하니스.

모드:
  tests    tests/test_han.py 에서 run(src)==expected / 오류 부분문자열 검사를 AST로 추출해 C 바이너리에 적용
  examples examples/*.ㄱㄴㄷ 를 파이썬 구현과 C 구현 양쪽으로 실행해 stdout 비교
  all      둘 다 (기본)

실행: python3 tools/conform.py [tests|examples|all] [-v] [--bin ./ganada]
"""
import argparse
import ast
import io
import os
import re
import subprocess
import sys
import tempfile

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))   # ganada-c/
REPO = os.path.dirname(ROOT)                                          # han-lang/
PY_INTERP = os.path.join(REPO, '가나다.py')
TEST_FILE = os.path.join(REPO, 'tests', 'test_han.py')
EXAMPLES = os.path.join(REPO, 'examples')

# 서버(로 요청을 계속 받는 예제는 스스로 끝나지 않아 stdout 대조가 불가능하다
# (파이썬·C 양쪽 다 timeout). v1 미지원이던 자료열기·저장소(·거래( 는 C 에 구현됐다.
SERVER_BLOCKING = re.compile(r'서버\(')

# 무작위 결과라 stdout 비교가 불가능한 예제
NONDETERMINISTIC = {'random.ㄱㄴㄷ'}


# ------------------------------------------------------------- 추출
def const_str(node):
    return node.value if isinstance(node, ast.Constant) and isinstance(node.value, str) else None


def extract_cases(path):
    """(이름, 종류, 소스, 기대값) 목록. 종류: 'stdout' | 'error_contains'"""
    with open(path, encoding='utf-8') as f:
        tree = ast.parse(f.read())
    cases = []

    def expected_of_run_call(call):
        if isinstance(call.func, ast.Name) and call.func.id == 'run' and len(call.args) == 1:
            return const_str(call.args[0])
        return None

    class V(ast.NodeVisitor):
        def __init__(self):
            self.fn = '?'

        def visit_FunctionDef(self, node):
            if node.name.startswith('test'):
                old = self.fn
                self.fn = node.name
                self.generic_visit(node)
                self.fn = old

        def visit_Compare(self, node):
            # run("src") == "expected"
            if isinstance(node.left, ast.Call) and len(node.ops) == 1 and isinstance(node.ops[0], ast.Eq):
                src = expected_of_run_call(node.left)
                exp = const_str(node.comparators[0]) if node.comparators else None
                if src is not None and exp is not None:
                    cases.append((self.fn, 'stdout', src, exp))
            self.generic_visit(node)

        def visit_Try(self, node):
            # try: run("src"); assert False ...  except ...: assert '부분' in str(e)
            src = None
            for st in node.body:
                if isinstance(st, ast.Expr) and isinstance(st.value, ast.Call):
                    src = expected_of_run_call(st.value) or src
            if src:
                for h in node.handlers:
                    for st in ast.walk(h):
                        if (isinstance(st, ast.Compare) and len(st.ops) == 1 and isinstance(st.ops[0], ast.In)
                                and isinstance(st.left, ast.Constant) and isinstance(st.left.value, str)):
                            cases.append((self.fn, 'error_contains', src, st.left.value))
            self.generic_visit(node)

    V().visit(tree)
    return cases


# ------------------------------------------------------------- 실행
def run_c(binary, src, timeout=15, cwd=None, stdin_data=None):
    # 파이썬 run() 헬퍼와 동일 조건: 기준 디렉터리 상대경로(가져오기/모듈) + HAN_TEST_VAR
    cwd = cwd or REPO
    with tempfile.NamedTemporaryFile('w', suffix='.ㄱㄴㄷ', delete=False, encoding='utf-8',
                                     dir=cwd) as f:
        f.write(src)
        path = f.name
    env = dict(os.environ, HAN_TEST_VAR='값123')
    try:
        kw = dict(capture_output=True, text=True, timeout=timeout, cwd=cwd, env=env)
        if stdin_data is None:
            kw['stdin'] = subprocess.DEVNULL
        else:
            kw['input'] = stdin_data
        p = subprocess.run([binary, '실행', path], **kw)
        return p.stdout, p.stderr, p.returncode
    except subprocess.TimeoutExpired:
        return None, 'TIMEOUT', -1
    finally:
        os.unlink(path)


def run_py(src, timeout=30, cwd=None):
    with tempfile.NamedTemporaryFile('w', suffix='.ㄱㄴㄷ', delete=False, encoding='utf-8',
                                     dir=cwd or REPO) as f:
        f.write(src)
        path = f.name
    try:
        p = subprocess.run([sys.executable, PY_INTERP, '실행', path], capture_output=True, text=True,
                           timeout=timeout, cwd=cwd or REPO, stdin=subprocess.DEVNULL)
        return p.stdout, p.stderr, p.returncode
    except subprocess.TimeoutExpired:
        return None, 'TIMEOUT', -1
    finally:
        os.unlink(path)


# ------------------------------------------------------------- 모드
def mode_tests(binary, verbose):
    cases = extract_cases(TEST_FILE)
    passed = failed = 0
    fails = []
    for name, kind, src, expect in cases:
        out, err, rc = run_c(binary, src)
        if kind == 'stdout':
            ok = out == expect
        else:
            ok = expect in (err or '') or expect in (out or '')
        if not ok and '입력(' in src:
            # 입력() 케이스는 stdin 조건(EOF vs 빈 줄)을 추출할 수 없어 빈 줄로 재시도
            out, err, rc = run_c(binary, src, stdin_data='\n')
            ok = (out == expect) if kind == 'stdout' else (expect in (err or '') or expect in (out or ''))
        if ok:
            passed += 1
        else:
            failed += 1
            fails.append((name, kind, src, expect, out, err))
    print(f'[tests] {passed}/{passed + failed} 통과 (추출 케이스 {len(cases)}개)')
    if verbose or fails:
        for name, kind, src, expect, out, err in fails[:15]:
            print(f'  FAIL {name} [{kind}]')
            print(f'    src: {src[:120]!r}')
            print(f'    기대: {expect[:120]!r}')
            print(f'    stdout: {(out or "")[:120]!r}')
            print(f'    stderr: {(err or "")[:200]!r}')
    return failed


def mode_examples(binary, verbose):
    files = sorted(f for f in os.listdir(EXAMPLES) if f.endswith('.ㄱㄴㄷ'))
    passed = failed = skipped = 0
    fails = []
    for fn in files:
        path = os.path.join(EXAMPLES, fn)
        with open(path, encoding='utf-8') as f:
            src = f.read()
        if SERVER_BLOCKING.search(src) or fn in NONDETERMINISTIC:
            skipped += 1
            continue
        with tempfile.TemporaryDirectory() as td:
            py_out, py_err, py_rc = run_py(src, cwd=td)
            c_out, c_err, c_rc = run_c(binary, src, cwd=td)
        if py_rc != 0:                      # 파이썬 측도 실패하는 예제는 기준 부재
            skipped += 1
            continue
        if c_out == py_out and c_rc == 0:
            passed += 1
        else:
            failed += 1
            fails.append((fn, py_out, py_err, c_out, c_err, c_rc))
    print(f'[examples] {passed} 일치 / {failed} 불일치 / {skipped} 스킵 (서버·무작위·기준 실패)')
    for fn, py_out, py_err, c_out, c_err, c_rc in fails[:15]:
        print(f'  FAIL {fn}')
        print(f'    py stdout: {(py_out or "")[:150]!r}')
        print(f'    C  stdout: {(c_out or "")[:150]!r}  rc={c_rc}')
        print(f'    C  stderr: {(c_err or "")[:200]!r}')
    return failed


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('mode', nargs='?', default='all', choices=['tests', 'examples', 'all'])
    ap.add_argument('--bin', default=os.path.join(ROOT, 'ganada'))
    ap.add_argument('-v', '--verbose', action='store_true')
    a = ap.parse_args()
    if not os.path.exists(a.bin):
        sys.exit(f'바이너리 없음: {a.bin}  (make 먼저)')
    fails = 0
    if a.mode in ('tests', 'all'):
        fails += mode_tests(a.bin, a.verbose)
    if a.mode in ('examples', 'all'):
        fails += mode_examples(a.bin, a.verbose)
    sys.exit(1 if fails else 0)


if __name__ == '__main__':
    main()
