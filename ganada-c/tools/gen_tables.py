#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""tables.h 생성기 — 가나다.py 에서 한글 문자열을 바이트 그대로 추출해 C 헤더로 낸다.

C 소스에 한글 리터럴을 직접 타이핑하면 글리프 손상 위험이 있으므로,
모든 한글 문자열은 이 스크립트가 \\xXX 이스케이프로 생성한다.
사람이 직접 타이핑한 한글(EXTRA_*)은 반드시 가나다.py 원문에 존재하는지 검증한다.

실행: python3 tools/gen_tables.py   (ganada-c/ 기준)
"""
import ast
import os
import re
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))       # ganada-c/
SRC = os.path.join(ROOT, '..', '가나다.py')
OUT = os.path.join(ROOT, 'src', 'tables.h')

with open(SRC, encoding='utf-8') as f:
    src_text = f.read()
tree = ast.parse(src_text)

# ---------------------------------------------------------- 한글 로마자 표기 (고유 enum 이름용)
CHO = ['g', 'kk', 'n', 'd', 'tt', 'r', 'm', 'b', 'pp', 's', 'ss', '', 'j', 'jj', 'ch', 'k', 't', 'p', 'h']
JUNG = ['a', 'ae', 'ya', 'yae', 'eo', 'e', 'yeo', 'ye', 'o', 'wa', 'wae', 'oe', 'yo', 'u', 'wo', 'we', 'wi', 'yu', 'eu', 'ui', 'i']
JONG = ['', 'g', 'kk', 'gs', 'n', 'nj', 'nh', 'd', 'l', 'lg', 'lm', 'lb', 'ls', 'lt', 'lp', 'lh', 'm', 'b', 'bs', 's', 'ss', 'ng', 'j', 'ch', 'k', 't', 'p', 'h']


def romanize(word):
    out = []
    for ch in word:
        code = ord(ch)
        if 0xAC00 <= code <= 0xD7A3:
            s = code - 0xAC00
            out.append(CHO[s // 588] + JUNG[(s // 28) % 21] + JONG[s % 28])
        elif ch.isascii() and ch.isalnum():
            out.append(ch)
        else:
            raise ValueError(f'로마자화 불가 문자: {ch!r} ({word!r})')
    return ''.join(out).upper()


def c_escape(s):
    """문자열 전체를 \\xXX 이스케이프로(비 ASCII) — 바이트 정확성 보장"""
    parts = []
    for b in s.encode('utf-8'):
        if 0x20 <= b < 0x7F and b not in (0x22, 0x5C):
            parts.append(chr(b))
        else:
            parts.append(f'\\x{b:02X}')
    return ''.join(parts)


def cstr(s):
    return '"' + c_escape(s) + '"'


# ---------------------------------------------------------- 가나다.py 에서 추출
def extract_set(name):
    for node in tree.body:
        if isinstance(node, ast.Assign) and any(isinstance(t, ast.Name) and t.id == name for t in node.targets):
            return [e.value for e in node.value.elts]
    raise RuntimeError(f'{name} 못 찾음')


def extract_dict_keys(name):
    for node in tree.body:
        if isinstance(node, ast.Assign) and any(isinstance(t, ast.Name) and t.id == name for t in node.targets):
            return [k.value for k in node.value.keys]
    raise RuntimeError(f'{name} 못 찾음')


KEYWORDS = extract_set('KEYWORDS')
BUILTINS = extract_dict_keys('BUILTINS')

# ---------------------------------------------------------- 사람이 타이핑하는 한글 — 원문 존재 검증
def _in_src(frag):
    """원문에 그대로 또는 파이썬 문자열 이스케이프(\") 형태로 존재하는지"""
    return frag in src_text or frag.replace('"', '\\"') in src_text


def check_in_src(s, label):
    if not _in_src(s):
        raise SystemExit(f'오타 의심: {label} {s!r} 이(가) 가나다.py 원문에 없음')


def check_msg(fmt, label):
    """printf 플레이스홀더 기준으로 쪼갠 한글 조각이 모두 원문에 있는지 검증"""
    for frag in re.split(r'%(?:\d+\$)?(?:lld|lld|zu|zd|d|s|g|f|x)', fmt):
        frag = frag.replace('%%', '')
        if re.search(r'[가-힣…·]', frag) and not _in_src(frag):
            raise SystemExit(f'오타 의심: {label} 메시지 조각 {frag!r} 이(가) 원문에 없음  (전체: {fmt!r})')


EXTRA = {  # 상수명 → 문자열 (참/거짓/없음/함수 등 키워드와 겹치는 건 KW_* 재사용)
    'STR_TYPE_BOOL': '참거짓',
    'STR_TYPE_INT': '정수',
    'STR_TYPE_FLOAT': '실수',
    'STR_TYPE_STR': '문자열',
    'STR_TYPE_LIST': '목록',
    'STR_TYPE_DICT': '사전',
    'STR_TYPE_UNKNOWN': '알수없음',
    'STR_SSIK': '씩',
    'STR_WA': ' 와(과) ',
    'STR_ELLIPSIS': '…',
    'STR_LAMBDA_NAME': '<람다>',
    'STR_DEFAULT_MODEL': 'openai/gpt-4o-mini',
    'STR_COLOR_RED': '빨강',
    'STR_COLOR_GREEN': '초록',
    'STR_COLOR_YELLOW': '노랑',
    'STR_COLOR_BLUE': '파랑',
    'STR_COLOR_MAGENTA': '자주',
    'STR_COLOR_CYAN': '청록',
    'STR_COLOR_GRAY': '회색',
    'STR_COLOR_BOLD': '굵게',
    'STR_GOREUGI': '고르기',
    'STR_OORYU': '오류',
    'STR_BONAEGI': '보내기',
    'STR_TEKST': '텍스트: ',
    'STR_BOGI': '보기: ',
    'STR_YOCHEONG': '요청: ',
    'STR_JONGNYU': '종류',
    'STR_KEUGI': '크기',
    'STR_SUJEONGSIGAG': '수정시각',
    'STR_PAIL': '파일',
    'STR_POLDEEO': '폴더',
    'STR_CMD_QUIT': ':종료',      # 대화형 종료 명령 (가나다.py repl_command 와 같은 두 이름)
    'STR_CMD_END': ':끝',
}
for name, s in EXTRA.items():
    check_in_src(s, name)

MESSAGES = {  # 상수명 → printf 형식 오류 메시지
    'ERR_UNTERMINATED_STR': '[%d행 %d열] 문자열이 닫히지 않았습니다',
    'ERR_UNKNOWN_CHAR': "[%d행 %d열] 알 수 없는 문자: '%s'",
    'ERR_SYNTAX_EXPECT': "[%d행 %d열] 구문 오류: '%s' 자리에 '%s'",
    'ERR_BLOCK_UNCLOSED': "'}' 가 필요합니다 (블록이 닫히지 않음)",
    'ERR_IMPORT_NEEDS_STR': '[%d행 %d열] 가져오기 뒤에는 "경로" 문자열이 필요합니다',
    'ERR_BAD_ASSIGN': '대입할 수 없는 대상입니다',
    'ERR_BAD_COMPOUND': '복합 대입 대상이 올바르지 않습니다',
    'ERR_DEFAULT_PARAM': '[%d행 %d열] 기본값 있는 매개변수 뒤에는 기본값 없는 매개변수를 둘 수 없습니다',
    'ERR_RANGE_INDEX': '범위 반복(부터..까지)에는 인덱스 변수를 쓸 수 없습니다 (순회 전용)',
    'ERR_SYNTAX_UNEXPECTED': "[%d행 %d열] 구문 오류: 예기치 않은 '%s'",
    'ERR_NAME_UNDEFINED': "이름 오류: '%s' 가 정의되지 않았습니다",
    'ERR_DESTRUCTURE': '구조 분해 오류: 값 %d개가 필요합니다 (목록/문자열만)',
    'ERR_STEP_TYPE': '범위 반복 스텝은 숫자여야 합니다',
    'ERR_STEP_ZERO': '범위 반복 스텝은 0일 수 없습니다',
    'ERR_NOT_ITERABLE': '반복할 수 없는 값입니다 (목록·문자열·사전만)',
    'ERR_INDEX_INT': '색인은 정수여야 합니다',
    'ERR_INDEX_RANGE': '색인 범위 오류: %s',
    'ERR_KEY_MISSING': '키 없음: %s',
    'ERR_NOT_INDEXABLE': '색인할 수 없는 값입니다',
    'ERR_NOT_SETTABLE': '색인 대입할 수 없는 값입니다',
    'ERR_BINOP_TYPES': "'%s' 연산을 할 수 없습니다: %s",
    'ERR_DIV_ZERO': '0으로 나눌 수 없습니다',
    'ERR_NOT_CALLABLE': '호출 오류: 함수가 아닙니다',
    'ERR_ARG_COUNT': "호출 오류: '%s' 는 인자 %s개가 필요(받음 %d개)",
    'ERR_RECURSION': '재귀가 너무 깊습니다 (무한 재귀가 아닌지 확인하세요)',
    'ERR_RECURSION_ALT': '재귀가 너무 깊습니다 (순환 참조나 무한 재귀가 아닌지 확인하세요)',
    'ERR_BREAK_OUTSIDE': "'멈춤'/'계속'은 반복문 안에서만 쓸 수 있습니다",
    'ERR_IMPORT_FAIL': '가져올 수 없습니다: %s',
    'ERR_MODULE_FAIL': '모듈을 불러올 수 없습니다: %s',
    'ERR_MODULE_INNER': "모듈 '%s' 오류 — %s",
    'ERR_NOT_SLICEABLE': '자를 수 없는 값입니다 (목록·문자열만)',
    'ERR_AVG_EMPTY': '평균: 빈 목록',
    'ERR_MEDIAN_EMPTY': '중앙값: 빈 목록',
    'ERR_MODE_EMPTY': '최빈값: 빈 목록',
    'ERR_ARGMAX_EMPTY': '최대기준: 빈 목록',
    'ERR_ARGMIN_EMPTY': '최소기준: 빈 목록',
    'ERR_CHUNK_SIZE': '묶음: 크기는 1 이상이어야 합니다',
    'ERR_NOT_IMPLEMENTED': '이 C 구현에서는 아직 지원하지 않습니다: %s',
    'ERR_FILE_READ': '파일을 읽을 수 없습니다: %s',
    'ERR_FILE_WRITE': '파일에 쓸 수 없습니다: %s',
    'ERR_JSON_PARSE': '제이슨파싱 오류: %s',
    'STR_AI_NO_KEY': '[AI 키 없음] OPENROUTER_API_KEY를 설정하면 실제 답을 받아요. (물음: %s)',
    'FMT_FUNC_REPR': '<함수 %s>',
    'ERR_STORE_NAME': '저장소: 이름은 한글·영문·숫자·_ 로 된 한 단어여야 합니다',
    'ERR_CD_NOFOLDER': "위치변경: '%s' 폴더가 없습니다",
    'ERR_DEL_NOTEMPTY': "파일삭제: '%s' 폴더가 비어있지 않습니다 (안의 파일부터 지우세요)",
    'ERR_DEL_MISSING': "파일삭제: '%s' 가 없습니다",
    'ERR_COPY_MISSING': "파일복사: '%s' 파일이 없습니다",
    'ERR_REN_MISSING': "이름바꾸기: '%s' 가 없습니다",
    'ERR_INFO_MISSING': "파일정보: '%s' 가 없습니다",
    'ERR_BAD_COLOR': "색칠: '%s' 은 모르는 색입니다 (가능: %s)",
    'ERR_LISTARG': '목록: 인자는 목록이어야 합니다 — 목록(["가", "나"])',
    'ERR_FORMARG': '입력폼: 필드이름목록은 목록이어야 합니다 — 입력폼("/등록", ["이름", "내용"])',
    'FMT_CALL_ERR': "'%s' 호출 오류: %s",
    'FMT_SUGGEST': " (혹시 '%s'?)",
    'FMT_KEYS_LIST': " (있는 키: %s)",
    # CLI 안내는 C 구현 고유다 — 실행 이름이 `가나다`(또는 ganada)이고 파이썬을 거치지 않는다.
    'STR_USAGE': '사용법: 가나다 실행 <파일.ㄱㄴㄷ>  |  가나다 대화',
    # 배너도 고유 — C 대화형은 아직 식 값 자동 표시와 : 명령이 없다(과장하지 않는다).
    'STR_BANNER': '가나다 v0 · 대화형. 여러 줄 블록 OK. 종료는 :종료/Ctrl-D',
    'STR_PROMPT': '가나다> ',
}
# 원문에 없는 C 구현 고유 메시지 — 검증 면제
ALLOW_NEW = {'ERR_NOT_IMPLEMENTED', 'ERR_FILE_READ', 'ERR_FILE_WRITE', 'ERR_JSON_PARSE',
             'STR_USAGE', 'STR_BANNER'}

for name, fmt in MESSAGES.items():
    if name not in ALLOW_NEW:
        check_msg(fmt, name)

# ---------------------------------------------------------- 헤더 생성
lines = []
w = lines.append
w('/* 자동 생성 파일 — tools/gen_tables.py 가 만든다. 직접 수정 금지. */')
w('/* 모든 한글 문자열은 \\xXX 이스케이프 (UTF-8 바이트 정확). */')
w('#ifndef GANADA_TABLES_H')
w('#define GANADA_TABLES_H')
w('')

# 키워드: 토큰 종류별 상수 + 렉서 테이블
w('/* ---- 키워드 ---- */')
kw_names = {}
for kw in KEYWORDS:
    rn = romanize(kw)
    if rn in kw_names:
        rn += '_2'
    kw_names[rn] = kw
    w(f'#define KW_{rn} {cstr(kw)} /* {kw} */')
w(f'#define NUM_KEYWORDS {len(KEYWORDS)}')
w('static const char *const KEYWORD_TABLE[NUM_KEYWORDS] = {')
for rn, kw in kw_names.items():
    w(f'    KW_{rn},')
w('};')
w('')

# 내장함수: enum + 이름 테이블
w('/* ---- 내장 함수 ---- */')
w('typedef enum {')
bn = []
seen = {}
for b in BUILTINS:
    rn = romanize(b)
    if rn in seen:
        seen[rn] += 1
        rn += f'_{seen[rn]}'
    else:
        seen[rn] = 1
    bn.append((rn, b))
    w(f'    B_{rn}, /* {b} */')
w('    B_COUNT')
w('} BuiltinId;')
w('static const char *const BUILTIN_NAMES[B_COUNT] = {')
for rn, b in bn:
    w(f'    {cstr(b)}, /* B_{rn} */')
w('};')
w('')

# 기타 문자열
w('/* ---- 기타 문자열 ---- */')
for name, s in EXTRA.items():
    w(f'#define {name} {cstr(s)} /* {s} */')
w('')

# 오류 메시지
w('/* ---- 오류 메시지 (printf 형식) ---- */')
for name, fmt in MESSAGES.items():
    w(f'#define {name} {cstr(fmt)}')
w('')

w('#endif /* GANADA_TABLES_H */')

os.makedirs(os.path.dirname(OUT), exist_ok=True)
with open(OUT, 'w', encoding='utf-8') as f:
    f.write('\n'.join(lines) + '\n')

print(f'생성 완료: {OUT}')
print(f'  키워드 {len(KEYWORDS)}개, 내장함수 {len(BUILTINS)}개, 추가 문자열 {len(EXTRA)}개, 메시지 {len(MESSAGES)}개')
