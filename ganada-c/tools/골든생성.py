#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""골든 코퍼스 생성기 — 파이썬 구현(가나다.py)을 정답지로 삼아 기대 출력을 동결한다.

이 스크립트만 파이썬을 쓴다. 만들어진 코퍼스는 tools/골든검사.ㄱㄴㄷ (가나다로 쓴 러너)이
C 바이너리로 대조하므로, 평소 회귀 검증에는 파이썬이 필요 없다.

실행: python3 tools/골든생성.py            # tests/golden/ 재생성
      python3 tools/골든생성.py --확인      # 파일을 쓰지 않고 현재 코퍼스와 비교

코퍼스 구조는 tests/golden/README.md 에 설명이 있다.
"""
import argparse
import os
import re
import shutil
import subprocess
import sys
import tempfile

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import conform  # 케이스 추출 로직은 기존 하니스와 공유(정답지 단일화)

ROOT = conform.ROOT                      # ganada-c/
REPO = conform.REPO                      # han-lang/
EXAMPLES = conform.EXAMPLES
GOLDEN = os.path.join(ROOT, 'tests', 'golden')
CASES = os.path.join(GOLDEN, '케이스')
EXP_EX = os.path.join(GOLDEN, '예제')
SELF = os.path.join(GOLDEN, '자가검증')

# 케이스 소스가 상대경로로 가져오는 파일 — 코퍼스를 자립시키려고 케이스 폴더에 복사한다.
FIXTURES = ['examples/lib.ㄱㄴㄷ']

# 예제에 먹일 표준입력(정규화). 없으면 /dev/null.
EXAMPLE_STDIN = {'input.ㄱㄴㄷ': '홍길동\n30\n'}

# 실행마다 결과가 달라 동결할 수 없는 예제
NONDETERMINISTIC = {'random.ㄱㄴㄷ'}

# 모든 케이스를 이 환경으로 고정한다(러너도 같은 값을 쓴다 — README 참고).
FIXED_ENV = {'HAN_TEST_VAR': '값123', 'HOME': '/ganada-golden-home', 'TZ': 'UTC'}
UNSET_ENV = ['OPENROUTER_API_KEY', 'OPENAI_API_KEY', 'ANTHROPIC_API_KEY']


def 환경():
    env = dict(os.environ)
    for k in UNSET_ENV:
        env.pop(k, None)
    env.update(FIXED_ENV)
    return env


def 파이썬실행(경로, cwd, stdin_data=None, timeout=60):
    kw = dict(capture_output=True, text=True, timeout=timeout, cwd=cwd, env=환경())
    if stdin_data is None:
        kw['stdin'] = subprocess.DEVNULL
    else:
        kw['input'] = stdin_data
    try:
        p = subprocess.run([sys.executable, conform.PY_INTERP, '실행', 경로], **kw)
        return p.stdout, p.stderr, p.returncode
    except subprocess.TimeoutExpired:
        return None, 'TIMEOUT', -1


def 만족(kind, expect, out, err):
    if kind == 'stdout':
        return out == expect
    return expect in (err or '') or expect in (out or '')


# ------------------------------------------------------------------ 쓰기 도우미
class 산출:
    """--확인 모드에서는 쓰지 않고 기존 내용과 비교만 한다."""

    def __init__(self, 확인만):
        self.확인만 = 확인만
        self.불일치 = []

    def 쓰기(self, 경로, 바이트):
        if self.확인만:
            기존 = None
            if os.path.exists(경로):
                with open(경로, 'rb') as f:
                    기존 = f.read()
            if 기존 != 바이트:
                self.불일치.append(os.path.relpath(경로, REPO))
            return
        os.makedirs(os.path.dirname(경로), exist_ok=True)
        with open(경로, 'wb') as f:
            f.write(바이트)

    def 글쓰기(self, 경로, 글):
        self.쓰기(경로, 글.encode('utf-8'))


def 안전이름(s):
    return re.sub(r'[^0-9A-Za-z_가-힣]', '_', s)


# ------------------------------------------------------------------ 본체
def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--확인', action='store_true', help='쓰지 않고 현재 코퍼스와 대조')
    a = ap.parse_args()
    out = 산출(a.확인)

    if not a.확인:
        for d in (CASES, EXP_EX, SELF):
            shutil.rmtree(d, ignore_errors=True)

    목록 = []   # 인덱스 행
    제외 = []   # (구분, 이름, 이유)

    # ---------- 고정 자료(가져오기 대상) 복사: 코퍼스를 자립시킨다
    for rel in FIXTURES:
        with open(os.path.join(REPO, rel), 'rb') as f:
            out.쓰기(os.path.join(CASES, rel), f.read())

    # ---------- tests/test_han.py 유래 케이스
    cases = conform.extract_cases(conform.TEST_FILE)
    for i, (fn, kind, src, expect) in enumerate(cases, 1):
        이름 = f'{i:04d}_{안전이름(fn)}'
        소스경로 = os.path.join(CASES, 이름 + '.ㄱㄴㄷ')
        out.글쓰기(소스경로, src)
        if a.확인 and not os.path.exists(소스경로):
            제외.append(('케이스', 이름, '소스 파일 없음(확인 모드)'))
            continue

        # 정답지로 검증: 표준입력 조건(EOF vs 빈 줄)을 파이썬 쪽에서 결정한다
        stdin_data = None
        o1, e1, rc1 = 파이썬실행(소스경로, CASES)
        if not 만족(kind, expect, o1, e1):
            stdin_data = '\n'
            o1, e1, rc1 = 파이썬실행(소스경로, CASES, stdin_data)
        if not 만족(kind, expect, o1, e1):
            제외.append(('케이스', 이름, f'정답지 불일치 rc={rc1} out={(o1 or "")[:40]!r}'))
            continue
        # 두 번 돌려 같은 결과인지(시각·무작위 누출) 확인
        o2, e2, rc2 = 파이썬실행(소스경로, CASES, stdin_data)
        if (o2, rc2) != (o1, rc1) or not 만족(kind, expect, o2, e2):
            제외.append(('케이스', 이름, '비결정적(두 번 실행 결과 다름)'))
            continue

        기대경로 = os.path.join(CASES, 이름 + ('.기대' if kind == 'stdout' else '.오류'))
        out.글쓰기(기대경로, expect)
        입력경로 = '-'
        if stdin_data is not None:
            p = os.path.join(CASES, 이름 + '.입력')
            out.글쓰기(p, stdin_data)
            입력경로 = os.path.relpath(p, REPO)
        if kind != 'stdout':                      # 참고용 원문(러너는 읽지 않음)
            out.글쓰기(os.path.join(CASES, 이름 + '.오류원문'), e1 or o1 or '')
        목록.append(['케이스', 이름, '출력' if kind == 'stdout' else '오류포함', str(rc1),
                     os.path.relpath(소스경로, REPO), os.path.relpath(기대경로, REPO),
                     입력경로, os.path.relpath(CASES, REPO), fn])

    # ---------- examples/*.ㄱㄴㄷ 유래 케이스
    files = sorted(f for f in os.listdir(EXAMPLES) if f.endswith('.ㄱㄴㄷ'))
    for fn in files:
        경로 = os.path.join(EXAMPLES, fn)
        with open(경로, encoding='utf-8') as f:
            src = f.read()
        이름 = fn[:-len('.ㄱㄴㄷ')]
        if conform.SERVER_BLOCKING.search(src):
            제외.append(('예제', fn, '서버(로 계속 도는 예제 — 스스로 끝나지 않아 동결 불가'))
            continue
        if fn in NONDETERMINISTIC:
            제외.append(('예제', fn, '무작위 — 출력 동결 불가'))
            continue
        stdin_data = EXAMPLE_STDIN.get(fn)
        결과 = []
        for _ in range(2):                        # 서로 다른 임시 작업폴더에서 두 번
            with tempfile.TemporaryDirectory() as td:
                결과.append(파이썬실행(경로, td, stdin_data))
        (o1, e1, rc1), (o2, e2, rc2) = 결과
        if rc1 != 0:
            제외.append(('예제', fn, f'정답지 실패 rc={rc1} err={(e1 or "").strip()[:60]!r}'))
            continue
        if (o1, rc1) != (o2, rc2):
            제외.append(('예제', fn, '비결정적(작업폴더·시각·무작위 의존)'))
            continue
        기대경로 = os.path.join(EXP_EX, 이름 + '.기대')
        out.글쓰기(기대경로, o1)
        입력경로 = '-'
        if stdin_data is not None:
            p = os.path.join(EXP_EX, 이름 + '.입력')
            out.글쓰기(p, stdin_data)
            입력경로 = os.path.relpath(p, REPO)
        목록.append(['예제', 이름, '출력', str(rc1), os.path.relpath(경로, REPO),
                     os.path.relpath(기대경로, REPO), 입력경로, '임시', fn])

    # ---------- 러너 자가검증(캐너리): 통과 1건 + 반드시 실패해야 하는 1건
    통과소스 = os.path.join(SELF, '통과.ㄱㄴㄷ')
    통과기대 = os.path.join(SELF, '통과.기대')
    out.글쓰기(통과소스, '출력("골든 러너 자가검증")\n')
    out.글쓰기(통과기대, '골든 러너 자가검증\n')
    목록.append(['자가통과', '통과', '출력', '0', os.path.relpath(통과소스, REPO),
                 os.path.relpath(통과기대, REPO), '-', os.path.relpath(SELF, REPO),
                 '러너 비교 경로 확인'])
    실패소스 = os.path.join(SELF, '실패.ㄱㄴㄷ')
    실패기대 = os.path.join(SELF, '실패.기대')
    out.글쓰기(실패소스, '출력("실제 출력")\n')
    out.글쓰기(실패기대, '일부러 틀린 기대값\n')
    목록.append(['자가실패', '실패', '출력', '0', os.path.relpath(실패소스, REPO),
                 os.path.relpath(실패기대, REPO), '-', os.path.relpath(SELF, REPO),
                 '러너 실패 감지 확인'])

    # ---------- 인덱스·제외 목록
    케이스수 = sum(1 for r in 목록 if r[0] == '케이스')
    예제수 = sum(1 for r in 목록 if r[0] == '예제')
    # 합계 줄 — 러너가 실제 처리 건수와 대조한다(목록이 잘리면 조용히 줄어드는 걸 막는다)
    목록.insert(0, ['합계', f'케이스={케이스수}', f'예제={예제수}', '0', '-', '-', '-', '-',
                    '생성기가 기록한 총계'])
    머리 = ['구분', '이름', '검사', '코드', '소스', '기대', '입력', '작업', '유래']
    줄들 = ['# 골든 코퍼스 인덱스 — tools/골든생성.py 가 만든다. 탭 구분. # 로 시작하는 줄은 주석.',
            '# ' + '\t'.join(머리)]
    줄들 += ['\t'.join(r) for r in 목록]
    out.글쓰기(os.path.join(GOLDEN, '목록.tsv'), '\n'.join(줄들) + '\n')

    제외줄 = ['# 코퍼스에서 뺀 케이스와 이유 — 러너는 읽지 않는다(사람 리뷰용).',
              '# 구분\t이름\t이유']
    제외줄 += ['\t'.join(r) for r in 제외]
    out.글쓰기(os.path.join(GOLDEN, '제외.tsv'), '\n'.join(제외줄) + '\n')

    print(f'[골든] 케이스 {케이스수}/{len(cases)}건, 예제 {예제수}/{len(files)}건, '
          f'자가검증 2건, 제외 {len(제외)}건')
    for 구분, 이름, 이유 in 제외:
        print(f'  제외 {구분} {이름}: {이유}')
    if a.확인:
        if out.불일치:
            print(f'[골든] 코퍼스가 정답지와 다름 — {len(out.불일치)}개 파일:')
            for p in out.불일치[:20]:
                print('  ' + p)
            sys.exit(1)
        print('[골든] 코퍼스가 정답지와 일치')
    sys.exit(0)


if __name__ == '__main__':
    main()
