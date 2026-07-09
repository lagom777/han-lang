# -*- coding: utf-8 -*-
"""가나다 인터프리터 테스트. 실행: python3 tests/test_han.py"""
import io
import os
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, ROOT)
os.chdir(ROOT)  # 상대경로 가져오기("examples/..") 가 cwd와 무관하게 동작하도록
from 가나다 import 실행소스, Interp, repl_eval, needs_more, repl_command, _웹서버만들기, HanError  # noqa: E402


def run(src):
    out = io.StringIO()
    실행소스(src, out=out)
    return out.getvalue()


def test_hello():
    assert run('출력("안녕")') == "안녕\n"


def test_arith_and_concat():
    assert run('출력(1 + 2 * 3)') == "7\n"
    assert run('출력("값: " + 3)') == "값: 3\n"
    assert run('출력(10 / 4)') == "2.5\n"


def test_for_inclusive():
    assert run('반복 i 를 1 부터 3 까지 { 출력(i) }') == "1\n2\n3\n"


def test_for_step():
    # 'N 씩' 스텝으로 건너뛰며 반복(양끝 포함)
    assert run('반복 i 를 0 부터 10 까지 2 씩 { 출력(i) }') == "0\n2\n4\n6\n8\n10\n"
    # 상한을 정확히 안 밟아도 초과 직전까지
    assert run('반복 i 를 1 부터 6 까지 2 씩 { 출력(i) }') == "1\n3\n5\n"
    # 음수 스텝 → 거꾸로 카운트다운
    assert run('반복 i 를 3 부터 1 까지 -1 씩 { 출력(i) }') == "3\n2\n1\n"
    # 스텝 식(변수)도 허용
    assert run('n = 3\n반복 i 를 0 부터 9 까지 n 씩 { 출력(i) }') == "0\n3\n6\n9\n"
    # 스텝 0은 친절한 오류(무한 루프 방지)
    try:
        run('반복 i 를 0 부터 5 까지 0 씩 { 출력(i) }')
        assert False, "스텝 0 오류가 나야 함"
    except Exception as e:
        assert '스텝' in str(e)
    # 스텝 없는 기존 문법은 그대로(1씩)
    assert run('반복 i 를 1 부터 3 까지 { 출력(i) }') == "1\n2\n3\n"


def test_if_elif_else():
    src = '만약 1 > 2 { 출력("a") } 아니면 만약 2 > 1 { 출력("b") } 아니면 { 출력("c") }'
    assert run(src) == "b\n"


def test_고르기_lazy_conditional():
    # 고르기(조건, 참값, 거짓값) — 삼항 조건식 대용(값 선택)
    assert run('출력(고르기(3 > 2, "큼", "작음"))') == "큼\n"
    assert run('출력(고르기(1 > 2, "큼", "작음"))') == "작음\n"
    # 지연 평가 — 선택 안 된 가지의 오류(0으로 나누기)는 실행되지 않음
    assert run('출력(고르기(5 != 0, 10 / 5, 10 / 0))') == "2\n"
    assert run('출력(고르기(0 != 0, 10 / 0, 999))') == "999\n"
    # 대입·중첩(등급 판정)
    assert run('점수 = 85\n출력(고르기(점수 >= 90, "A", 고르기(점수 >= 80, "B", "C")))') == "B\n"


def test_operator_type_errors():
    # 타입 안 맞는 연산·0 나머지: 파이썬 트레이스백 대신 친절한 HanError
    for src in ['출력("5" - 3)', '나이="20"\n만약 나이 >= 18 {}', '출력(-"x")', '출력(5 % 0)']:
        try:
            run(src)
            assert False, f"오류가 나야 함: {src}"
        except HanError as e:
            assert '연산을 할 수 없습니다' in str(e) or '0으로 나눌 수 없습니다' in str(e)


def test_func_recursion():
    src = '함수 팩(n){ 만약 n < 2 { 반환 1 } 반환 n * 팩(n - 1) } 출력(팩(5))'
    assert run(src) == "120\n"


def test_while_accumulate():
    src = '합 = 0\n수 = 1\n동안 수 <= 5 { 합 = 합 + 수\n수 = 수 + 1 }\n출력(합)'
    assert run(src) == "15\n"


def test_logic_and_bool():
    assert run('출력(참 그리고 거짓)') == "거짓\n"
    assert run('출력(아니다 거짓)') == "참\n"


def test_list_literal_and_index():
    assert run('목록 = [10, 20, 30]\n출력(목록[0] + 목록[2])') == "40\n"
    assert run('출력([1, 2, 3])') == "[1, 2, 3]\n"


def test_list_setindex_and_append():
    src = '목록 = [1, 2]\n목록[0] = 9\n추가(목록, 3)\n출력(목록)\n출력(길이(목록))'
    assert run(src) == "[9, 2, 3]\n3\n"


def test_list_truthiness_and_oob():
    assert run('만약 [] { 출력("a") } 아니면 { 출력("b") }') == "b\n"
    try:
        run('출력([1, 2][5])')
        assert False, "범위 오류가 나야 함"
    except Exception:
        pass


def test_dict_literal_and_index():
    assert run('사람 = {"이름": "홍길동", "나이": 20}\n출력(사람["이름"])\n출력(사람["나이"] + 5)') == "홍길동\n25\n"


def test_dict_setindex_new_key():
    src = '사전 = {}\n사전["가"] = 1\n사전["나"] = 2\n사전["가"] = 9\n출력(사전["가"] + 사전["나"])\n출력(길이(사전))'
    assert run(src) == "11\n2\n"


def test_dict_missing_key_errors():
    try:
        run('출력({"a": 1}["없는키"])')
        assert False, "키 없음 오류가 나야 함"
    except Exception:
        pass


def test_builtins_collection():
    assert run('출력(합([1, 2, 3, 4]))') == "10\n"
    assert run('출력(정렬([3, 1, 2]))') == "[1, 2, 3]\n"
    assert run('출력(정렬([3, 1, 2], 참))') == "[3, 2, 1]\n"  # 내림차순 옵션
    assert run('출력(정렬([3, 1, 2], 거짓))') == "[1, 2, 3]\n"  # 거짓=오름차순
    assert run('출력(상위([3, 1, 4, 1, 5, 9, 2], 3))') == "[9, 5, 4]\n"  # top-N
    assert run('출력(상위([1, 2], 5))') == "[2, 1]\n"                     # n > 길이
    assert run('출력(상위([5, 3, 8], 0))') == "[]\n"                       # n=0
    assert run('출력(최대([5, 9, 2]))') == "9\n"
    assert run('출력(최소([5, 9, 2]))') == "2\n"
    assert run('출력(범위(5))') == "[0, 1, 2, 3, 4]\n"
    assert run('출력(간격(0, 10, 5))') == "[0, 2.5, 5, 7.5, 10]\n"  # linspace 균등 분할
    assert run('출력(간격(0, 100, 6))') == "[0, 20, 40, 60, 80, 100]\n"
    assert run('출력(간격(5, 5, 1))') == "[5]\n"                       # 개수 1
    assert run('출력(범위(2, 5))') == "[2, 3, 4]\n"
    assert run('출력(거꾸로([1, 2, 3]))') == "[3, 2, 1]\n"
    assert run('출력(거꾸로("안녕"))') == "녕안\n"   # 문자열은 뒤집은 문자열(문자 목록 아님)
    assert run('출력(거꾸로("abc"))') == "cba\n"


def test_builtins_string():
    assert run('출력(나누기("가,나,다", ","))') == "[가, 나, 다]\n"
    assert run('출력(합치기(["가", "나", "다"], "-"))') == "가-나-다\n"


def test_줄나누기():
    # 여러 줄 문자열을 줄 단위 목록으로(파일·텍스트 처리용)
    assert run('출력(줄나누기("가\n나\n다"))') == "[가, 나, 다]\n"
    assert run('출력(길이(줄나누기("한 줄만")))') == "1\n"
    assert run('출력(줄나누기(""))') == "[]\n"                 # 빈 문자열 → 빈 목록
    assert run('출력(줄나누기("끝\n"))') == "[끝]\n"            # 끝 개행은 무시(빈 줄 안 생김)
    # 줄 단위 순회
    assert run('합 = 0\n반복 줄 를 줄나누기("1\n2\n3") 에서 { 합 = 합 + 숫자(줄) }\n출력(합)') == "6\n"


def test_foreach_list():
    assert run('합 = 0\n반복 x 를 [10, 20, 30] 에서 { 합 = 합 + x }\n출력(합)') == "60\n"


def test_foreach_string():
    assert run('반복 c 를 "한글" 에서 { 출력(c) }') == "한\n글\n"


def test_foreach_dict_keys():
    src = '점수 = {"가": 1, "나": 2}\n반복 k 를 점수 에서 { 출력(k + "=" + 점수[k]) }'
    assert run(src) == "가=1\n나=2\n"


def test_range_for_still_works():
    assert run('반복 i 를 1 부터 3 까지 { 출력(i) }') == "1\n2\n3\n"


def test_ai_질문_stub_without_key():
    # 키 없을 때: 실제 호출 대신 안내 stub 반환(오프라인 안전·크래시 X)
    os.environ.pop('OPENROUTER_API_KEY', None)
    out = run('출력(질문("안녕"))')
    assert 'AI 키 없음' in out


def test_ai_체계질문_stub_without_key():
    # 시스템+사용자 프롬프트. 키 없으면 마지막(사용자) 메시지를 echo하는 stub
    os.environ.pop('OPENROUTER_API_KEY', None)
    out = run('출력(체계질문("너는 시인이다", "바다를 한 줄로"))')
    assert 'AI 키 없음' in out and '바다를' in out


def test_ai_분류_stub_without_key():
    # 분류: 텍스트+보기를 사용자 프롬프트로 구성해 LLM 호출(키 없으면 stub, 보기 라벨 포함)
    os.environ.pop('OPENROUTER_API_KEY', None)
    out = run('출력(분류("이 영화 최고!", ["긍정", "부정"]))')
    assert 'AI 키 없음' in out and '긍정' in out  # 보기가 프롬프트에 반영됨


def test_ai_요약_stub_without_key():
    # 요약: 텍스트를 사용자 프롬프트로 LLM 호출(키 없으면 stub, 입력 텍스트 반영)
    os.environ.pop('OPENROUTER_API_KEY', None)
    out = run('출력(요약("오늘 회의에서 신제품 출시 일정을 정했다.", 2))')
    assert 'AI 키 없음' in out and '오늘' in out  # 텍스트가 프롬프트에 반영됨
    # 문장수 인자 없이도 동작(기본값)
    assert 'AI 키 없음' in run('출력(요약("긴 글 요약 테스트"))')


def test_ai_번역_stub_without_key():
    # 번역: 텍스트를 사용자 프롬프트로 LLM 호출(키 없으면 stub, 입력 텍스트 반영)
    os.environ.pop('OPENROUTER_API_KEY', None)
    out = run('출력(번역("안녕하세요", "영어"))')
    assert 'AI 키 없음' in out and '안녕하세요' in out  # 텍스트가 프롬프트에 반영됨
    # 목표 언어 인자 없이도 동작(기본=영어)
    assert 'AI 키 없음' in run('출력(번역("반갑습니다"))')


def test_ai_추출_stub_without_key():
    # 추출: 텍스트+지시를 사용자 프롬프트로 LLM 호출(키 없으면 stub, 지시 반영)
    os.environ.pop('OPENROUTER_API_KEY', None)
    out = run('출력(추출("연락처: 010-1234-5678 입니다", "전화번호"))')
    assert 'AI 키 없음' in out and '전화번호' in out  # 지시가 프롬프트에 반영됨
    # 지시 인자 없이도 동작(기본=핵심 정보)
    assert 'AI 키 없음' in run('출력(추출("어떤 긴 텍스트"))')


def test_runtime_error_has_line():
    # 런타임 오류에 행 번호가 붙는다(2번째 줄에서 미정의 변수)
    try:
        run('출력(1)\n출력(없는변수)')
        assert False, "오류가 나야 함"
    except Exception as e:
        assert '2행' in str(e)


def test_syntax_error_has_column():
    # 구문 오류에 행+열 위치가 붙는다
    try:
        run('출력(1 +)')  # ')' 자리에 식이 와야 함
        assert False, "오류가 나야 함"
    except Exception as e:
        s = str(e)
        assert '행' in s and '열' in s


def test_lex_error_has_column():
    # 렉서 오류(알 수 없는 문자)도 행+열 — 2행 5열의 '@'
    try:
        run('가 = 1\n나 = @')
        assert False, "오류가 나야 함"
    except Exception as e:
        s = str(e)
        assert '2행' in s and '5열' in s


def test_error_shows_source_line():
    # 런타임 오류에 문제의 소스 줄이 함께 표시된다(Python 트레이스백처럼)
    try:
        run('출력(1)\n출력(없는변수)\n출력(3)')
        assert False, "오류가 나야 함"
    except Exception as e:
        s = str(e)
        assert '2행' in s
        assert '출력(없는변수)' in s   # 문제의 소스 줄이 붙는다
    # 색인 오류도 소스 줄을 보여준다
    try:
        run('목록 = [1, 2]\n출력(목록[9])')
        assert False, "오류가 나야 함"
    except Exception as e:
        s = str(e)
        assert '2행' in s and '목록[9]' in s
    # 구문 오류(파서)에도 소스 줄이 붙는다
    try:
        run('출력(1)\n출력(1 +)')
        assert False, "오류가 나야 함"
    except Exception as e:
        s = str(e)
        assert '2행' in s and '출력(1 +)' in s


def test_error_shows_column_caret():
    # 열 정보가 있는 오류(렉서/파서)엔 그 자리를 가리키는 ^ 캐럿이 붙는다
    try:
        run('가 = 1\n나 = @')  # 2행 5열의 '@' — 렉서 오류
        assert False, "오류가 나야 함"
    except Exception as e:
        s = str(e)
        assert '2행' in s and '5열' in s
        assert '나 = @' in s
        caret = s.splitlines()[-1]
        assert caret.strip() == '^'   # 마지막 줄은 캐럿
        # 한글 폭 보정: prefix "  2 | "(6) + "나 = "(2+1+1+1=5) = 11칸 뒤에 ^
        assert caret == ' ' * 11 + '^'
    # 열 정보 없는 런타임 오류엔 캐럿을 붙이지 않는다(소스 줄만)
    try:
        run('목록 = [1]\n출력(목록[9])')
        assert False, "오류가 나야 함"
    except Exception as e:
        assert '^' not in str(e)


def test_module_import():
    # 다른 .ㄱㄴㄷ 파일의 함수/변수를 가져와 사용
    out = run('가져오기 "examples/lib.ㄱㄴㄷ"\n출력(곱하기(6, 7))\n출력(제곱(5))')
    assert out == "42\n25\n"


def test_string_methods():
    assert run('출력(대문자("han"))') == "HAN\n"
    assert run('출력(소문자("HAN"))') == "han\n"
    assert run('출력(다듬기("  안녕  "))') == "안녕\n"
    assert run('출력(말줄임("안녕하세요반갑습니다", 5))') == "안녕하세요…\n"  # 길면 잘라 …
    assert run('출력(말줄임("짧음", 10))') == "짧음\n"                        # 짧으면 그대로
    assert run('출력(말줄임("정확히다섯", 5))') == "정확히다섯\n"              # 경계=그대로
    assert run('출력(바꾸기("가나가", "가", "다"))') == "다나다\n"
    assert run('출력(포함("한글날", "글"))') == "참\n"
    assert run('출력(포함([1, 2, 3], 2))') == "참\n"
    assert run('출력(시작("한국어", "한"))') == "참\n"
    assert run('출력(끝("프로그램", "램"))') == "참\n"


def test_숫자인가():
    # 숫자 문자열/숫자는 참, 아니면 거짓(입력 검증용)
    assert run('출력(숫자인가("123"))') == "참\n"
    assert run('출력(숫자인가("3.14"))') == "참\n"
    assert run('출력(숫자인가("  -5 "))') == "참\n"      # 부호·공백 허용
    assert run('출력(숫자인가(42))') == "참\n"           # 숫자 자체
    assert run('출력(숫자인가("abc"))') == "거짓\n"
    assert run('출력(숫자인가("12개"))') == "거짓\n"      # 숫자+비숫자 혼합
    assert run('출력(숫자인가(""))') == "거짓\n"
    assert run('출력(숫자인가(참))') == "거짓\n"          # 불리언은 숫자 아님


def test_글자인가():
    # 모두 글자면 참(한글 포함), 숫자·공백·기호 섞이거나 빈 문자열은 거짓
    assert run('출력(글자인가("한글"))') == "참\n"
    assert run('출력(글자인가("abc"))') == "참\n"
    assert run('출력(글자인가("a1"))') == "거짓\n"          # 숫자 혼합
    assert run('출력(글자인가("안녕 하세요"))') == "거짓\n"  # 공백 포함
    assert run('출력(글자인가(""))') == "거짓\n"
    assert run('출력(글자인가(123))') == "거짓\n"           # 숫자는 문자열 아님


def test_type_builtin():
    assert run('출력(타입(3))') == "정수\n"
    assert run('출력(타입(3.5))') == "실수\n"
    assert run('출력(타입("x"))') == "문자열\n"
    assert run('출력(타입([1]))') == "목록\n"
    assert run('출력(타입(참))') == "참거짓\n"
    assert run('출력(타입(없음))') == "없음\n"


def test_repl_echo_and_state():
    it = Interp(out=io.StringIO())
    assert repl_eval(it, '1 + 2') == '3'        # 식은 값 에코
    assert repl_eval(it, 'x = 10') is None       # 대입은 에코 없음
    assert repl_eval(it, 'x * 2') == '20'        # REPL 상태 유지
    assert repl_eval(it, '출력("안녕")') is None  # 부수효과(출력)는 에코 없음
    assert it.out.getvalue() == '안녕\n'


def test_repl_needs_more():
    assert needs_more('함수 f() {') is True
    assert needs_more('함수 f() { 반환 1 }') is False
    assert needs_more('1 + 2') is False


def test_break():
    src = '합 = 0\n반복 i 를 1 부터 100 까지 {\n만약 i > 5 { 멈춤 }\n합 = 합 + i\n}\n출력(합)'
    assert run(src) == "15\n"  # 1+2+3+4+5


def test_continue():
    src = '합 = 0\n반복 i 를 1 부터 10 까지 {\n만약 i % 2 == 0 { 계속 }\n합 = 합 + i\n}\n출력(합)'
    assert run(src) == "25\n"  # 1+3+5+7+9


def test_break_while_and_foreach_continue():
    assert run('수 = 0\n동안 참 {\n수 = 수 + 1\n만약 수 == 3 { 멈춤 }\n}\n출력(수)') == "3\n"
    src = '결과 = 0\n반복 x 를 [1, 2, 3, 4] 에서 {\n만약 x == 2 { 계속 }\n결과 = 결과 + x\n}\n출력(결과)'
    assert run(src) == "8\n"  # 1+3+4


def test_lambda():
    assert run('제곱 = 람다(x) { 반환 x * x }\n출력(제곱(6))') == "36\n"
    assert run('출력((람다(가, 나) { 반환 가 + 나 })(3, 4))') == "7\n"


def test_lambda_higher_order():
    src = '함수 적용(f, 값) { 반환 f(값) }\n출력(적용(람다(x) { 반환 x + 1 }, 10))'
    assert run(src) == "11\n"


def test_math_builtins():
    assert run('출력(절댓값(-5))') == "5\n"
    assert run('출력(반올림(3.7))') == "4\n"
    assert run('출력(반올림(3.14159, 2))') == "3.14\n"
    assert run('출력(반올림(0.5))') == "1\n"
    assert run('출력(반올림(1.5))') == "2\n"
    assert run('출력(반올림(2.5))') == "3\n"
    assert run('출력(반올림(3.5))') == "4\n"
    assert run('출력(반올림(1234.5))') == "1235\n"
    assert run('출력(반올림(-2.5))') == "-3\n"
    assert run('출력(반올림(2.675, 2))') == "2.68\n"
    assert run('출력(올림(2.1))') == "3\n"
    assert run('출력(내림(2.9))') == "2\n"
    assert run('출력(제곱근(9))') == "3\n"
    assert run('출력(거듭제곱(2, 10))') == "1024\n"


def test_input_builtin():
    old = sys.stdin
    sys.stdin = io.StringIO("홍길동\n42\n")
    try:
        out = run('이름 = 입력("이름? ")\n나이 = 숫자(입력())\n출력(이름 + " " + (나이 + 1))')
    finally:
        sys.stdin = old
    assert out == "이름? 홍길동 43\n"


def test_dict_helpers():
    assert run('출력(키들({"가": 1, "나": 2}))') == "[가, 나]\n"
    assert run('출력(값들({"가": 1, "나": 2}))') == "[1, 2]\n"
    assert run('출력(항목들({"가": 1}))') == "[[가, 1]]\n"
    # 사전만들기: 키·값 목록 → 사전 (항목들의 역)
    assert run('출력(사전만들기(["가", "나"], [1, 2]))') == "{가: 1, 나: 2}\n"
    assert run('출력(사전만들기([], []))') == "{}\n"
    assert run('출력(사전만들기(["가", "나", "다"], [1, 2]))') == "{가: 1, 나: 2}\n"  # 짧은 쪽까지
    src = '점수 = {"국": 90, "수": 80}\n합계 = 0\n반복 v 를 값들(점수) 에서 { 합계 = 합계 + v }\n출력(합계)'
    assert run(src) == "170\n"


def test_compound_assign():
    assert run('x = 10\nx += 5\n출력(x)') == "15\n"
    assert run('x = 10\nx -= 3\n출력(x)') == "7\n"
    assert run('x = 4\nx *= 3\n출력(x)') == "12\n"
    assert run('x = 20\nx /= 4\n출력(x)') == "5\n"
    assert run('s = "가"\ns += "나"\n출력(s)') == "가나\n"
    assert run('목록 = [1, 2, 3]\n목록[1] += 10\n출력(목록)') == "[1, 12, 3]\n"


def test_try_catch():
    out = run('시도 {\n출력(10 / 0)\n} 잡기(오류) {\n출력("잡음: " + 오류)\n}')
    assert "잡음:" in out and "0으로" in out


def test_try_no_error():
    assert run('시도 {\n출력("정상")\n} 잡기(e) {\n출력("안탐")\n}') == "정상\n"


def test_try_catch_name_and_index():
    assert run('시도 { 출력(없는것) } 잡기(오류) { 출력("처리됨") }') == "처리됨\n"
    assert run('목록 = [1, 2]\n시도 { 출력(목록[9]) } 잡기(e) { 출력("범위처리") }') == "범위처리\n"


def test_map_filter():
    assert run('출력(변환([1, 2, 3], 람다(x) { 반환 x * 10 }))') == "[10, 20, 30]\n"
    assert run('출력(거르기([1, 2, 3, 4, 5, 6], 람다(x) { 반환 x % 2 == 0 }))') == "[2, 4, 6]\n"
    # 이름 함수 + 조합(거르기→변환)
    src = ('함수 제곱(n) { 반환 n * n }\n'
           '출력(변환(거르기([1, 2, 3, 4], 람다(x) { 반환 x > 2 }), 제곱))')
    assert run(src) == "[9, 16]\n"
    # 분할(partition): [참인 것들, 거짓인 것들]
    assert run('출력(분할([1, 2, 3, 4, 5], 람다(x) { 반환 x % 2 == 0 }))') == "[[2, 4], [1, 3, 5]]\n"
    assert run('출력(분할([], 람다(x) { 반환 참 }))') == "[[], []]\n"
    # 그룹화(groupby): {키: [원소들]}
    assert run('출력(그룹화([1, 2, 3, 4], 람다(x) { 반환 x % 2 }))') == "{1: [1, 3], 0: [2, 4]}\n"
    assert run('출력(그룹화([], 람다(x) { 반환 x }))') == "{}\n"


def test_reduce():
    assert run('출력(접기([1, 2, 3, 4], 0, 람다(누적, x) { 반환 누적 + x }))') == "10\n"
    assert run('출력(접기([1, 2, 3, 4], 1, 람다(a, b) { 반환 a * b }))') == "24\n"
    assert run('출력(접기(["가", "나", "다"], "", 람다(s, c) { 반환 s + c }))') == "가나다\n"
    assert run('출력(접기([], 99, 람다(a, b) { 반환 a + b }))') == "99\n"  # 빈 목록 → 초기값
    # map/filter/reduce 파이프라인
    src = '출력(접기(거르기([1,2,3,4,5,6], 람다(x){ 반환 x % 2 == 0 }), 0, 람다(a,b){ 반환 a + b }))'
    assert run(src) == "12\n"  # (2+4+6)


def test_format():
    assert run('출력(서식("{}님은 {}살", "한", 1))') == "한님은 1살\n"
    assert run('출력(서식("{} + {} = {}", 2, 3, 5))') == "2 + 3 = 5\n"
    assert run('출력(서식("값 부족: {}"))') == "값 부족: {}\n"  # 값 모자라면 {} 유지
    assert run('출력(서식("치환 없음"))') == "치환 없음\n"


def test_sort_by():
    # 길이 기준 오름차순
    assert run('출력(정렬기준(["가나다", "가", "가나"], 람다(s) { 반환 길이(s) }))') == "[가, 가나, 가나다]\n"
    # 사전 필드 기준 정렬
    src = ('사람들 = [{"이름": "B", "나이": 30}, {"이름": "A", "나이": 20}]\n'
           '출력(정렬기준(사람들, 람다(p) { 반환 p["나이"] })[0]["이름"])')
    assert run(src) == "A\n"
    # 내림차순 = 정렬 후 거꾸로
    assert run('출력(거꾸로(정렬기준([3, 1, 2], 람다(x) { 반환 x })))') == "[3, 2, 1]\n"
    # 정렬기준 내림차순 옵션(3번째 인자) — 나이 큰 순
    src2 = ('사람들 = [{"이름": "A", "나이": 20}, {"이름": "B", "나이": 30}]\n'
            '출력(정렬기준(사람들, 람다(p) { 반환 p["나이"] }, 참)[0]["이름"])')
    assert run(src2) == "B\n"
    # 상위기준: 키 큰 순 상위 n (객체 순위표)
    top = ('판매 = [{"이름": "가", "금액": 100}, {"이름": "나", "금액": 300}, {"이름": "다", "금액": 200}]\n'
           '출력(변환(상위기준(판매, 2, 람다(p) { 반환 p["금액"] }), 람다(p) { 반환 p["이름"] }))')
    assert run(top) == "[나, 다]\n"  # 금액 상위 2: 나(300), 다(200)


def test_search_helpers():
    # 찾기(find): 첫 매치 / 없으면 없음
    assert run('출력(찾기([1, 3, 4, 6], 람다(x) { 반환 x % 2 == 0 }))') == "4\n"
    assert run('출력(찾기([1, 3, 5], 람다(x) { 반환 x > 10 }))') == "없음\n"
    # 있나(any)
    assert run('출력(있나([1, 2, 3], 람다(x) { 반환 x > 2 }))') == "참\n"
    assert run('출력(있나([1, 2], 람다(x) { 반환 x > 5 }))') == "거짓\n"
    # 모두(all)
    assert run('출력(모두([2, 4, 6], 람다(x) { 반환 x % 2 == 0 }))') == "참\n"
    assert run('출력(모두([2, 3], 람다(x) { 반환 x % 2 == 0 }))') == "거짓\n"


def test_unique_count():
    assert run('출력(고유([1, 2, 2, 3, 1, 3]))') == "[1, 2, 3]\n"
    assert run('출력(고유(["가", "나", "가"]))') == "[가, 나]\n"
    assert run('출력(개수([1, 2, 2, 3, 2], 2))') == "3\n"
    assert run('출력(개수(["a", "b"], "c"))') == "0\n"


def test_zip():
    assert run('출력(묶기([1, 2, 3], ["가", "나", "다"]))') == "[[1, 가], [2, 나], [3, 다]]\n"
    assert run('출력(묶기([1, 2, 3, 4], ["가", "나"]))') == "[[1, 가], [2, 나]]\n"  # 짧은 쪽까지
    # 묶기 + 변환: 두 목록 원소별 합
    src = '출력(변환(묶기([1, 2, 3], [10, 20, 30]), 람다(쌍) { 반환 쌍[0] + 쌍[1] }))'
    assert run(src) == "[11, 22, 33]\n"


def test_random():
    # 비결정 — 값이 아니라 범위/멤버십을 여러 번 단언
    for _ in range(20):
        v = float(run('출력(무작위())').strip())
        assert 0.0 <= v < 1.0
    for _ in range(30):
        d = int(run('출력(무작위정수(1, 6))').strip())  # 주사위
        assert 1 <= d <= 6
    for _ in range(20):
        c = run('출력(무작위선택(["가", "나", "다"]))').strip()
        assert c in ("가", "나", "다")


def test_merge():
    assert run('출력(병합({"가": 1}, {"나": 2})["가"])') == "1\n"
    assert run('출력(병합({"가": 1, "나": 2}, {"나": 99})["나"])') == "99\n"  # 뒤가 우선
    assert run('출력(길이(병합({"가": 1}, {"나": 2})))') == "2\n"
    # 원본 불변
    assert run('원본 = {"가": 1}\n병합(원본, {"나": 2})\n출력(길이(원본))') == "1\n"


def test_foreach_index():
    # 인덱스 + 값 동시 순회
    assert run('반복 i, 값 를 ["가", "나", "다"] 에서 {\n출력(i + ":" + 값)\n}') == "0:가\n1:나\n2:다\n"
    # 기존 단일 변수 순회는 그대로
    assert run('합 = 0\n반복 x 를 [1, 2, 3] 에서 { 합 = 합 + x }\n출력(합)') == "6\n"
    # 범위 반복에 인덱스 변수는 오류
    try:
        run('반복 i, j 를 1 부터 3 까지 { 출력(i) }')
        assert False, "오류가 나야 함"
    except Exception as e:
        assert "인덱스" in str(e)


def test_default_params():
    fn = '함수 인사(이름, 말="안녕") { 반환 말 + ", " + 이름 }\n'
    assert run(fn + '출력(인사("한"))') == "안녕, 한\n"          # 기본값
    assert run(fn + '출력(인사("한", "반가워"))') == "반가워, 한\n"  # 덮어쓰기
    # 람다도 기본값
    lam = '제곱 = 람다(x, 배수=1) { 반환 x * x * 배수 }\n'
    assert run(lam + '출력(제곱(3))') == "9\n"
    assert run(lam + '출력(제곱(3, 2))') == "18\n"
    # 필수 인자 누락 → 오류
    try:
        run('함수 더(가, 나) { 반환 가 + 나 }\n출력(더(1))')
        assert False, "오류가 나야 함"
    except Exception as e:
        assert "인자" in str(e)
    # 기본값 뒤 필수 매개변수 → 구문 오류
    try:
        run('함수 나쁨(가=1, 나) { 반환 가 }\n나쁨(1)')
        assert False, "오류가 나야 함"
    except Exception as e:
        assert "기본값" in str(e)


def test_destructure():
    assert run('가, 나 = [1, 2]\n출력(가 + 나)') == "3\n"
    assert run('가, 나, 다 = [10, 20, 30]\n출력(다)') == "30\n"
    # 항목(키-값 쌍) 분해
    assert run('이름, 점수 = ["민지", 95]\n출력(이름 + ": " + 점수)') == "민지: 95\n"
    # 개수 불일치 → 오류
    try:
        run('가, 나 = [1, 2, 3]')
        assert False, "오류가 나야 함"
    except Exception as e:
        assert "구조 분해" in str(e)


def test_range_step():
    assert run('출력(범위(0, 10, 2))') == "[0, 2, 4, 6, 8]\n"
    assert run('출력(범위(10, 0, -2))') == "[10, 8, 6, 4, 2]\n"  # 음수 간격(내림차순)
    # 기존 1·2인자 하위호환
    assert run('출력(범위(3))') == "[0, 1, 2]\n"
    assert run('출력(범위(2, 5))') == "[2, 3, 4]\n"


def test_raise():
    # 발생 → 시도/잡기로 잡힘
    src = '시도 {\n발생("잘못된 입력")\n} 잡기(오류) {\n출력("잡음: " + 오류)\n}'
    assert run(src) == "잡음: 잘못된 입력\n"
    # 안 잡으면 전파(메시지+행 정보)
    try:
        run('출력(1)\n발생("치명적")')
        assert False, "오류가 나야 함"
    except Exception as e:
        assert "치명적" in str(e)


def test_env():
    import os
    os.environ["HAN_TEST_VAR"] = "값123"
    try:
        assert run('출력(환경변수("HAN_TEST_VAR"))') == "값123\n"
        assert run('출력(환경변수("HAN_없는_XYZ", "기본"))') == "기본\n"  # 기본값
        assert run('출력(환경변수("HAN_없는_XYZ"))') == "없음\n"           # 기본 미지정
    finally:
        del os.environ["HAN_TEST_VAR"]


def test_now():
    assert run('출력(타입(지금()))') == "실수\n"  # 유닉스 초(소수)
    assert run('출력(지금() > 1600000000)') == "참\n"  # 2020년 이후
    assert run('시작 = 지금()\n끝 = 지금()\n출력(끝 >= 시작)') == "참\n"  # 경과 측정


def test_normalize():
    assert run('출력(정규화([0, 5, 10]))') == "[0, 0.5, 1]\n"
    assert run('출력(정규화([2, 2, 2]))') == "[0, 0, 0]\n"  # 동일값 → 0들
    assert run('출력(정규화([]))') == "[]\n"                 # 빈


def test_transpose():
    assert run('출력(전치([[1, 2, 3], [4, 5, 6]]))') == "[[1, 4], [2, 5], [3, 6]]\n"
    assert run('출력(전치([]))') == "[]\n"                       # 빈
    assert run('출력(전치([[1], [2], [3]]))') == "[[1, 2, 3]]\n"  # 열벡터 → 행벡터


def test_mode():
    assert run('출력(최빈값([1, 2, 2, 3, 3, 3]))') == "3\n"
    assert run('출력(최빈값(["가", "나", "가"]))') == "가\n"
    assert run('출력(최빈값([1, 1, 2, 2]))') == "1\n"  # 동률 → 먼저 등장


def test_stdev():
    # [2,4,4,4,5,5,7,9] 평균 5, 모분산 4 → 표준편차 2
    assert run('출력(표준편차([2, 4, 4, 4, 5, 5, 7, 9]))') == "2\n"
    assert run('출력(표준편차([5, 5, 5]))') == "0\n"  # 편차 없음
    assert run('출력(표준편차([]))') == "0\n"          # 빈 목록


def test_cumsum():
    assert run('출력(누적합([1, 2, 3, 4]))') == "[1, 3, 6, 10]\n"
    assert run('출력(누적합([]))') == "[]\n"          # 빈 목록
    assert run('출력(누적합([5]))') == "[5]\n"          # 단일
    assert run('출력(누적합([10, -3, 5]))') == "[10, 7, 12]\n"  # 음수


def test_누적곱():
    assert run('출력(누적곱([1, 2, 3, 4]))') == "[1, 2, 6, 24]\n"
    assert run('출력(누적곱([5]))') == "[5]\n"           # 단일
    assert run('출력(누적곱([]))') == "[]\n"             # 빈 목록


def test_회전():
    # 회전: 왼쪽으로 n칸(음수면 오른쪽), 길이 초과는 나머지 처리
    assert run('출력(회전([1, 2, 3, 4, 5], 2))') == "[3, 4, 5, 1, 2]\n"   # 왼쪽 2칸
    assert run('출력(회전([1, 2, 3, 4, 5], -1))') == "[5, 1, 2, 3, 4]\n"  # 음수=오른쪽
    assert run('출력(회전([1, 2, 3], 3))') == "[1, 2, 3]\n"               # 한 바퀴=원상복귀
    assert run('출력(회전([1, 2, 3], 5))') == "[3, 1, 2]\n"               # 길이 초과=나머지
    assert run('출력(회전([1, 2, 3], 0))') == "[1, 2, 3]\n"               # 0칸=그대로
    assert run('출력(회전([], 3))') == "[]\n"                             # 빈 목록


def test_listdir():
    import tempfile, os, shutil
    d = os.path.join(tempfile.gettempdir(), "han_listdir_test")
    if os.path.exists(d):
        shutil.rmtree(d)
    os.makedirs(d)
    open(os.path.join(d, "b.txt"), "w").close()
    open(os.path.join(d, "a.txt"), "w").close()
    assert run(f'출력(파일목록("{d}"))') == "[a.txt, b.txt]\n"  # 정렬됨
    shutil.rmtree(d)


def test_file_exists():
    import tempfile, os
    p = os.path.join(tempfile.gettempdir(), "han_exists_test.txt")
    if os.path.exists(p):
        os.remove(p)
    assert run(f'출력(파일존재("{p}"))') == "거짓\n"  # 없음
    run(f'파일쓰기("{p}", "x")')
    assert run(f'출력(파일존재("{p}"))') == "참\n"      # 생성 후 있음
    os.remove(p)


def test_append():
    import tempfile, os
    p = os.path.join(tempfile.gettempdir(), "han_append.txt")
    if os.path.exists(p):
        os.remove(p)
    assert run(f'출력(이어쓰기("{p}", "가"))') == "1\n"  # 없으면 생성, 1글자
    run(f'이어쓰기("{p}", "나다")')                       # 덧붙임
    assert run(f'출력(파일읽기("{p}"))') == "가나다\n"     # 덮어쓰지 않고 이어짐
    os.remove(p)


def test_file_io():
    import tempfile, os
    p = os.path.join(tempfile.gettempdir(), "han_io_test.txt")
    # 쓰기는 글자 수 반환
    assert run(f'출력(파일쓰기("{p}", "가나다"))') == "3\n"
    # 쓴 뒤 읽으면 내용 일치
    assert run(f'파일쓰기("{p}", "안녕 한")\n출력(파일읽기("{p}"))') == "안녕 한\n"
    os.remove(p)
    # 없는 파일은 오류(시도/잡기로 잡힘)
    assert run('시도 { 파일읽기("/없는경로/xyz.txt") } 잡기(오류) { 출력("못 읽음") }') == "못 읽음\n"


def test_gcd_lcm():
    assert run('출력(최대공약수(12, 18))') == "6\n"
    assert run('출력(최소공배수(4, 6))') == "12\n"
    assert run('출력(최대공약수(17, 5))') == "1\n"   # 서로소
    assert run('출력(최소공배수(0, 5))') == "0\n"     # 0 포함


def test_generate():
    assert run('출력(생성(5, 람다(i){ 반환 i * i }))') == "[0, 1, 4, 9, 16]\n"
    assert run('출력(생성(3, 람다(i){ 반환 (i + 1) * 10 }))') == "[10, 20, 30]\n"
    assert run('출력(생성(0, 람다(i){ 반환 i }))') == "[]\n"  # 빈 생성


def test_clamp():
    assert run('출력(사이값(5, 0, 10))') == "5\n"      # 범위 안 → 그대로
    assert run('출력(사이값(-3, 0, 10))') == "0\n"      # 하한
    assert run('출력(사이값(15, 0, 10))') == "10\n"     # 상한
    assert run('출력(사이값(7.5, 0, 5))') == "5\n"      # 소수도


def test_model_select():
    # 기본 모델 조회
    assert "gpt" in run('출력(모델())')
    # 설정 후 조회(같은 프로그램 내 유지)
    assert run('모델("anthropic/claude-3.5")\n출력(모델())') == "anthropic/claude-3.5\n"


def test_repl_command():
    assert repl_command("출력(1)") is None          # 일반 코드는 명령 아님
    assert repl_command(":종료") == ("quit", None)
    assert repl_command(":끝") == ("quit", None)
    action, text = repl_command(":도움")
    assert action == "print" and "내장함수" in text and "출력" in text
    assert ":변수" in text                            # 도움말이 :변수 안내
    assert repl_command(":없는명령")[0] == "print"   # 알 수 없는 명령도 안내


def test_repl_command_변수():
    # :변수 는 REPL 세션에 정의된 변수를 나열한다(interp 전달 시)
    it = Interp()
    assert repl_command(":변수", it) == ("print", "정의된 변수가 없어요")
    repl_eval(it, "이름 = \"한\"")
    repl_eval(it, "나이 = 20")
    action, text = repl_command(":변수", it)
    assert action == "print"
    assert "이름 = 한" in text and "나이 = 20" in text
    # interp 없이 호출해도 안전(하위호환)
    assert repl_command(":변수")[0] == "print"


def test_repl_command_비우기():
    # :비우기 는 세션 변수를 지운다(내장함수는 유지)
    it = Interp()
    repl_eval(it, "가 = 1")
    repl_eval(it, "나 = 2")
    action, text = repl_command(":비우기", it)
    assert action == "print" and "2개" in text        # 지운 개수 안내
    assert repl_command(":변수", it) == ("print", "정의된 변수가 없어요")
    # 초기화 후에도 내장함수는 정상 동작
    assert repl_eval(it, "합([1, 2, 3])") == "6"
    # :도움에 :비우기 안내
    assert ":비우기" in repl_command(":도움")[1]
    assert run('출력(천단위(1234567))') == "1,234,567\n"
    assert run('출력(천단위(1000))') == "1,000\n"
    assert run('출력(천단위(999))') == "999\n"
    assert run('출력(천단위(-12345))') == "-12,345\n"


def test_dict_key_suggestion():
    # 키 오타 → 가까운 키 제안
    try:
        run('사전 = {"점수목록": [1]}\n사전["점수몰록"]')
        assert False, "키 없음 나야 함"
    except Exception as e:
        assert "키 없음" in str(e) and "혹시 '점수목록'?" in str(e)
    # 가까운 게 없으면 있는 키 나열
    try:
        run('사전 = {"이름": "철수", "나이": 30}\n사전["직업"]')
        assert False
    except Exception as e:
        assert "있는 키" in str(e) and "이름" in str(e) and "나이" in str(e)


def test_name_suggestion():
    # 오타 시 가까운 이름 제안
    try:
        run('점수목록 = [1,2,3]\n출력(점수몰록)')  # 점수몰록 ~ 점수목록
        assert False, "이름 오류 나야 함"
    except Exception as e:
        assert "정의되지 않았습니다" in str(e)
        assert "혹시 '점수목록'?" in str(e)
    # 가까운 게 전혀 없으면 제안 없음(기존 메시지 유지)
    try:
        run('출력(존재하지않는아주긴변수이름)')
        assert False
    except Exception as e:
        assert "정의되지 않았습니다" in str(e) and "혹시" not in str(e)


def test_flatten():
    assert run('출력(평탄화([[1,2],[3,4],[5]]))') == "[1, 2, 3, 4, 5]\n"
    assert run('출력(평탄화([[1],[2,3],[]]))') == "[1, 2, 3]\n"  # 빈 내부 목록
    assert run('출력(평탄화([1,[2,3],4]))') == "[1, 2, 3, 4]\n"  # 혼합(비목록 유지)
    assert run('출력(평탄화([]))') == "[]\n"


def test_chunk():
    assert run('출력(묶음([1,2,3,4,5], 2))') == "[[1, 2], [3, 4], [5]]\n"  # 마지막 짧음
    assert run('출력(묶음([1,2,3,4], 2))') == "[[1, 2], [3, 4]]\n"
    assert run('출력(묶음([], 3))') == "[]\n"
    try:
        run('묶음([1,2], 0)')  # 크기 0은 오류
        assert False, "크기 0 오류 나야 함"
    except Exception:
        pass


def test_frequency():
    assert run('출력(빈도(["가","나","가","다","가"]))') == "{가: 3, 나: 1, 다: 1}\n"
    assert run('출력(빈도([1,1,2,3,3,3]))') == "{1: 2, 2: 1, 3: 3}\n"
    assert run('출력(빈도([]))') == "{}\n"


def test_string_find():
    assert run('출력(위치("안녕하세요", "하세"))') == "2\n"   # 0부터
    assert run('출력(위치("안녕", "없음"))') == "-1\n"        # 없으면 -1
    assert run('출력(위치("가나다라", "가"))') == "0\n"


def test_mean_median():
    assert run('출력(평균([1, 2, 3, 4]))') == "2.5\n"
    assert run('출력(평균([2, 4, 6]))') == "4\n"            # 정수형 결과는 정수로
    assert run('출력(중앙값([3, 1, 2]))') == "2\n"          # 정렬 후 가운데(홀수)
    assert run('출력(중앙값([1, 2, 3, 4]))') == "2.5\n"     # 짝수면 가운데 둘 평균
    # 빈 목록은 오류
    try:
        run('평균([])')
        assert False, "빈 목록 오류 나야 함"
    except Exception:
        pass


def test_dict_get():
    assert run('출력(값얻기({"가":1,"나":2}, "가"))') == "1\n"
    assert run('출력(값얻기({"가":1}, "없는키", 0))') == "0\n"      # 기본값
    assert run('출력(값얻기({"가":1}, "없는키"))') == "없음\n"       # 기본 미지정 → 없음
    assert run('출력(키있나({"가":1}, "가"))') == "참\n"
    assert run('출력(키있나({"가":1}, "나"))') == "거짓\n"


def test_argmax():
    src = ('사람들 = [{"이름":"가","점수":80},{"이름":"나","점수":95},{"이름":"다","점수":70}]\n'
           '출력(최대기준(사람들, 람다(p){ 반환 p["점수"] })["이름"])\n'
           '출력(최소기준(사람들, 람다(p){ 반환 p["점수"] })["이름"])')
    assert run(src) == "나\n다\n"
    # 빈 목록은 오류
    try:
        run('최대기준([], 람다(x){ 반환 x })')
        assert False, "빈 목록 오류 나야 함"
    except Exception:
        pass


def test_set_ops():
    assert run('출력(교집합([1,2,3,4], [2,4,6]))') == "[2, 4]\n"
    assert run('출력(합집합([1,2,3], [3,4,5]))') == "[1, 2, 3, 4, 5]\n"
    assert run('출력(차집합([1,2,3,4], [2,4]))') == "[1, 3]\n"
    assert run('출력(교집합([1,1,2,2,3], [1,2]))') == "[1, 2]\n"  # 중복 제거


def test_pad():
    assert run('출력(왼쪽채우기("5", 3, "0"))') == "005\n"        # 오른쪽 정렬, 0으로 채움
    assert run('출력(오른쪽채우기("가", 4, "."))') == "가...\n"    # 왼쪽 정렬, .으로 채움
    assert run('출력(왼쪽채우기(12, 5))') == "   12\n"            # 기본 공백, 숫자도 문자열화
    assert run('출력(오른쪽채우기("길다", 2))') == "길다\n"       # 너비보다 길면 그대로


def test_가운데채우기():
    assert run('출력(가운데채우기("가", 5))') == "  가  \n"        # 기본 공백, 양쪽 균등
    assert run('출력(가운데채우기("가", 4, "*"))') == "*가**\n"    # 홀수 여백은 오른쪽에
    assert run('출력(가운데채우기("길다", 2))') == "길다\n"       # 너비보다 길면 그대로


def test_json():
    # 파싱: JSON 문자열 → 값
    assert run('자료 = 제이슨파싱("{\\"이름\\": \\"한\\", \\"나이\\": 1}")\n출력(자료["이름"])') == "한\n"
    assert run('출력(합(제이슨파싱("[1, 2, 3]")))') == "6\n"
    # 생성: 값 → JSON 문자열
    assert run('출력(제이슨문자열([1, 2, 3]))') == "[1, 2, 3]\n"
    assert run('출력(제이슨문자열({"키": "값"}))') == '{"키": "값"}\n'
    # 왕복(round-trip)
    assert run('글 = 제이슨문자열({"수": 42})\n출력(제이슨파싱(글)["수"] + 1)') == "43\n"


def test_slice():
    assert run('출력([1, 2, 3, 4, 5][1:3])') == "[2, 3]\n"
    assert run('출력("한국어"[0:2])') == "한국\n"
    assert run('출력([1, 2, 3, 4][2:])') == "[3, 4]\n"
    assert run('출력([1, 2, 3, 4][:2])') == "[1, 2]\n"
    assert run('출력("프로그램"[-2:])') == "그램\n"


def test_flagship_example():
    # 종합 예제 — 전 toolkit 결합(판매 분석)이 기대 출력을 낸다
    with open(os.path.join(ROOT, "examples", "flagship.ㄱㄴㄷ"), encoding="utf-8") as f:
        src = f.read()
    out = io.StringIO()
    실행소스(src, out=out, base_dir=os.path.join(ROOT, "examples"))
    s = out.getvalue()
    assert "총매출: 8530000원" in s
    assert "베스트셀러: 노트북" in s
    assert "1. 노트북" in s  # 정렬·인덱스·채우기
    assert '"분류"' in s and '"전자"' in s  # JSON 요약


def test_flagship_pipeline_example():
    # 실전 파이프라인 — 파일 I/O + JSON + toolkit 결합(쓰기→읽기→분석→쓰기→읽기)
    with open(os.path.join(ROOT, "examples", "flagship_pipeline.ㄱㄴㄷ"), encoding="utf-8") as f:
        src = f.read()
    out = io.StringIO()
    실행소스(src, out=out, base_dir=os.path.join(ROOT, "examples"))
    s = out.getvalue()
    assert "총매출: 1,535,000원" in s  # 접기 + 천단위
    assert "베스트셀러: 노트북" in s    # 최대기준(argmax)
    assert '"전자"' in s               # 빈도 → JSON 요약


def test_all_examples_run():
    # 모든 examples/*.ㄱㄴㄷ 가 오류 없이 실행되는지(회귀 방지)
    import glob
    os.environ.pop('OPENROUTER_API_KEY', None)   # ai.ㄱㄴㄷ → 안내 stub 경로
    exdir = os.path.join(ROOT, 'examples')
    # 웹.ㄱㄴㄷ·정적웹.ㄱㄴㄷ·한몸.ㄱㄴㄷ 는 서버(블로킹)로 계속 돌기 때문에 제외 — test_서버/test_정적서버/test_한몸_통합 이 따로 검증
    paths = [p for p in sorted(glob.glob(os.path.join(exdir, '*.ㄱㄴㄷ')))
             if os.path.basename(p) not in ('웹.ㄱㄴㄷ', '정적웹.ㄱㄴㄷ', '한몸.ㄱㄴㄷ')]
    assert len(paths) >= 10
    old = sys.stdin
    try:
        for path in paths:
            sys.stdin = io.StringIO("홍길동\n30\n40\n50\n")  # input.ㄱㄴㄷ 용
            with open(path, encoding='utf-8') as f:
                src = f.read()
            try:
                실행소스(src, out=io.StringIO(), base_dir=exdir)
            except Exception as e:
                raise AssertionError(os.path.basename(path) + " 실행 실패: " + str(e))
    finally:
        sys.stdin = old


def test_서버():
    # 서버(포트, 라우트) — 임의 포트(0)에 바인딩, 데몬 스레드로 서비스, 실제 HTTP 요청 검증
    import threading
    import urllib.request
    import urllib.parse
    import urllib.error

    src = (
        '함수 홈(요청) { 반환 "<h1>안녕 가나다</h1>" }\n'
        '함수 인사(요청) {\n'
        '    이름 = 값얻기(요청["질의"], "이름", "손님")\n'
        '    반환 {"상태": 200, "헤더": {"Content-Type": "text/plain; charset=utf-8"}, "본문": "이름=" + 이름}\n'
        '}\n'
        '라우트 = {"/": 홈, "/안녕": 인사}\n'
    )
    interp = 실행소스(src, out=io.StringIO())
    httpd = _웹서버만들기(interp, 0, interp.g.vars['라우트'])
    포트 = httpd.server_address[1]
    t = threading.Thread(target=httpd.serve_forever, daemon=True)
    t.start()
    try:
        base = f"http://127.0.0.1:{포트}"
        with urllib.request.urlopen(base + "/") as r:
            assert r.status == 200
            assert "안녕 가나다" in r.read().decode("utf-8")
        # 질의 라우트 — 한글 경로·값은 퍼센트 인코딩(브라우저·urllib 자동)
        url = base + urllib.parse.quote("/안녕") + "?" + urllib.parse.urlencode({"이름": "철수"})
        with urllib.request.urlopen(url) as r:
            assert r.status == 200
            assert r.read().decode("utf-8") == "이름=철수"
        # 없는 경로 → 404
        try:
            urllib.request.urlopen(base + "/nope")
            assert False, "404 가 나야 함"
        except urllib.error.HTTPError as e:
            assert e.code == 404
    finally:
        httpd.shutdown()
        httpd.server_close()


def test_정적서버():
    # 서버(포트, 라우트, 정적폴더) — 라우트 밖 경로는 폴더 파일 서빙, 라우트 우선, ../ 이탈 차단
    import threading
    import shutil
    import tempfile
    import http.client
    import urllib.request
    import urllib.parse
    import urllib.error

    d = os.path.join(tempfile.gettempdir(), "han_static_test")
    if os.path.exists(d):
        shutil.rmtree(d)
    os.makedirs(os.path.join(d, "public"))
    with open(os.path.join(d, "public", "index.html"), "w", encoding="utf-8") as f:
        f.write("<h1>정적 홈</h1>")
    with open(os.path.join(d, "public", "style.css"), "w", encoding="utf-8") as f:
        f.write("body { color: red }")
    with open(os.path.join(d, "비밀.txt"), "w", encoding="utf-8") as f:      # 폴더 밖 — 접근되면 안 됨
        f.write("극비자료")

    interp = 실행소스('함수 홈(요청) { 반환 "라우트 홈" }\n라우트 = {"/": 홈}\n', out=io.StringIO())
    httpd = _웹서버만들기(interp, 0, interp.g.vars['라우트'], os.path.join(d, "public"))
    포트 = httpd.server_address[1]
    t = threading.Thread(target=httpd.serve_forever, daemon=True)
    t.start()
    try:
        base = f"http://127.0.0.1:{포트}"
        # 정적 파일 + 올바른 Content-Type
        with urllib.request.urlopen(base + "/index.html") as r:
            assert r.status == 200
            assert r.headers["Content-Type"] == "text/html; charset=utf-8"
            assert "정적 홈" in r.read().decode("utf-8")
        with urllib.request.urlopen(base + "/style.css") as r:
            assert r.headers["Content-Type"] == "text/css; charset=utf-8"
            assert "color: red" in r.read().decode("utf-8")
        # 라우트가 정적 파일보다 우선 ("/" 는 라우트 → index.html 아님)
        with urllib.request.urlopen(base + "/") as r:
            assert r.read().decode("utf-8") == "라우트 홈"
        # 경로 이탈(../) → 404 (http.client 로 원본 경로 그대로 전송)
        conn = http.client.HTTPConnection("127.0.0.1", 포트)
        conn.request("GET", "/../" + urllib.parse.quote("비밀.txt"))   # ../ 는 그대로, 파일명만 인코딩
        resp = conn.getresponse()
        assert resp.status == 404
        assert "극비자료" not in resp.read().decode("utf-8")   # 파일 내용 유출 없음 (404 본문은 경로만 되울림)
        conn.close()
        # 없는 파일 → 404
        try:
            urllib.request.urlopen(base + urllib.parse.quote("/없는파일.html"))
            assert False, "404 가 나야 함"
        except urllib.error.HTTPError as e:
            assert e.code == 404
    finally:
        httpd.shutdown()
        httpd.server_close()
        shutil.rmtree(d)


def test_쿠키서버():
    # 쿠키 — 요청["쿠키"] 파싱(퍼센트 디코딩), 응답 "쿠키설정" → Set-Cookie(Path=/; HttpOnly, 인코딩) 왕복
    import threading
    import http.client
    import urllib.parse

    src = (
        '함수 방문(요청) {\n'
        '    횟수 = 숫자(값얻기(요청["쿠키"], "방문", "0")) + 1\n'
        '    반환 {"본문": "방문=" + 횟수, "쿠키설정": {"방문": "" + 횟수, "이름": "철수"}}\n'
        '}\n'
        '함수 보기(요청) { 반환 "이름=" + 값얻기(요청["쿠키"], "이름", "(없음)") }\n'
        '라우트 = {"/방문": 방문, "/보기": 보기}\n'
    )
    interp = 실행소스(src, out=io.StringIO())
    httpd = _웹서버만들기(interp, 0, interp.g.vars['라우트'])
    포트 = httpd.server_address[1]
    t = threading.Thread(target=httpd.serve_forever, daemon=True)
    t.start()
    try:
        방문경로 = urllib.parse.quote("/방문")
        # 1차 요청: 쿠키 없음 → 방문=1, Set-Cookie 발행(한글 이름·값은 퍼센트 인코딩, Path=/; HttpOnly)
        conn = http.client.HTTPConnection("127.0.0.1", 포트)
        conn.request("GET", 방문경로)
        resp = conn.getresponse()
        assert resp.status == 200
        assert resp.read().decode("utf-8") == "방문=1"
        쿠키들 = [v for k, v in resp.getheaders() if k.lower() == "set-cookie"]
        conn.close()
        assert len(쿠키들) == 2
        방문쿠키 = [c for c in 쿠키들 if c.startswith(urllib.parse.quote("방문", safe='') + "=")][0]
        assert 방문쿠키 == urllib.parse.quote("방문", safe='') + "=1; Path=/; HttpOnly"
        이름쿠키 = [c for c in 쿠키들 if c.startswith(urllib.parse.quote("이름", safe='') + "=")][0]
        assert urllib.parse.quote("철수", safe='') in 이름쿠키       # 값도 퍼센트 인코딩
        # 2차 요청: 받은 쿠키를 되돌려보냄 → 방문=2 (왕복)
        보낼쿠키 = "; ".join(c.split(";")[0] for c in 쿠키들)
        conn = http.client.HTTPConnection("127.0.0.1", 포트)
        conn.request("GET", 방문경로, headers={"Cookie": 보낼쿠키})
        resp = conn.getresponse()
        assert resp.read().decode("utf-8") == "방문=2"
        conn.close()
        # 한글 값 쿠키가 디코딩되어 요청["쿠키"] 로 들어오는지
        conn = http.client.HTTPConnection("127.0.0.1", 포트)
        conn.request("GET", urllib.parse.quote("/보기"), headers={"Cookie": 보낼쿠키})
        resp = conn.getresponse()
        assert resp.read().decode("utf-8") == "이름=철수"
        conn.close()
        # 문자열 반환 라우트는 Set-Cookie 없이 그대로 (기존 동작 불변)
        conn = http.client.HTTPConnection("127.0.0.1", 포트)
        conn.request("GET", urllib.parse.quote("/보기"))
        resp = conn.getresponse()
        assert resp.read().decode("utf-8") == "이름=(없음)"
        assert not [v for k, v in resp.getheaders() if k.lower() == "set-cookie"]
        conn.close()
    finally:
        httpd.shutdown()
        httpd.server_close()


def test_쿠키_만료():
    # 쿠키설정 값이 {값, 만료} 사전이면 Set-Cookie 에 Max-Age 부여(만료=0 이면 삭제=로그아웃). 문자열 값은 기존대로
    import threading
    import http.client
    import urllib.parse

    src = (
        '함수 로그인(요청) { 반환 {"본문": "ok", "쿠키설정": {"세션": {"값": "abc", "만료": 3600}}} }\n'
        '함수 로그아웃(요청) { 반환 {"본문": "bye", "쿠키설정": {"세션": {"값": "", "만료": 0}}} }\n'
        '함수 그냥(요청) { 반환 {"본문": "x", "쿠키설정": {"세션": "abc"}} }\n'
        '라우트 = {"/로그인": 로그인, "/로그아웃": 로그아웃, "/그냥": 그냥}\n'
    )
    interp = 실행소스(src, out=io.StringIO())
    httpd = _웹서버만들기(interp, 0, interp.g.vars['라우트'])
    포트 = httpd.server_address[1]
    t = threading.Thread(target=httpd.serve_forever, daemon=True)
    t.start()
    try:
        def 쿠키(경로):
            conn = http.client.HTTPConnection("127.0.0.1", 포트)
            conn.request("GET", urllib.parse.quote(경로))
            resp = conn.getresponse()
            resp.read()
            헤더 = [v for k, v in resp.getheaders() if k.lower() == "set-cookie"][0]
            conn.close()
            return 헤더
        assert "Max-Age=3600" in 쿠키("/로그인")       # 지속 세션
        assert "Max-Age=0" in 쿠키("/로그아웃")         # 삭제(로그아웃)
        그냥헤더 = 쿠키("/그냥")                          # 문자열 값 → 기존 동작(Max-Age 없음)
        assert "Max-Age" not in 그냥헤더 and 그냥헤더.endswith("; Path=/; HttpOnly")
    finally:
        httpd.shutdown()
        httpd.server_close()


def test_POST_폼본문():
    # POST 폼(application/x-www-form-urlencoded) 본문 → 요청["질의"] 로 파싱(본문 문자열은 그대로 유지)
    import threading
    import http.client
    import urllib.parse

    src = (
        '함수 등록(요청) {\n'
        '    반환 "이름=" + 값얻기(요청["질의"], "이름", "없음") + " 본문=" + 요청["본문"]\n'
        '}\n'
        '라우트 = {"/등록": 등록}\n'
    )
    interp = 실행소스(src, out=io.StringIO())
    httpd = _웹서버만들기(interp, 0, interp.g.vars['라우트'])
    포트 = httpd.server_address[1]
    t = threading.Thread(target=httpd.serve_forever, daemon=True)
    t.start()
    try:
        본문 = urllib.parse.urlencode({"이름": "철수", "내용": "폼 전송"})
        conn = http.client.HTTPConnection("127.0.0.1", 포트)
        conn.request("POST", urllib.parse.quote("/등록"), body=본문,
                     headers={"Content-Type": "application/x-www-form-urlencoded"})
        resp = conn.getresponse()
        assert resp.status == 200
        받은 = resp.read().decode("utf-8")
        assert "이름=철수" in 받은          # 폼 필드가 질의로 들어옴
        assert "본문=" + 본문 in 받은        # 원본 본문은 그대로 유지
        conn.close()
    finally:
        httpd.shutdown()
        httpd.server_close()


def test_자료_왕복():
    # SQLite — 메모리 DB에 생성→삽입→질의 왕복 (한글 테이블·컬럼·값 그대로)
    src = (
        '자료 = 자료열기(":memory:")\n'
        '실행(자료, "CREATE TABLE 방명록(이름 TEXT, 말 TEXT)")\n'
        '실행(자료, "INSERT INTO 방명록 VALUES (?, ?)", ["철수", "안녕하세요"])\n'
        '행들 = 질의(자료, "SELECT * FROM 방명록")\n'
        '출력(길이(행들))\n'
        '출력(행들[0]["이름"] + ": " + 행들[0]["말"])\n'
        '자료닫기(자료)\n'
    )
    assert run(src) == "1\n철수: 안녕하세요\n"


def test_자료_파일저장():
    # 파일 DB — 실행마다 자동 커밋이라 닫았다 다시 열어도 데이터 유지
    import tempfile
    import shutil
    d = tempfile.mkdtemp()
    경로 = os.path.join(d, "자료.db")
    try:
        run(
            '자료 = 자료열기("' + 경로 + '")\n'
            '실행(자료, "CREATE TABLE t(x TEXT)")\n'
            '실행(자료, "INSERT INTO t VALUES (?)", ["한글값"])\n'
            '자료닫기(자료)\n'
        )
        out = run(
            '자료 = 자료열기("' + 경로 + '")\n'
            '행들 = 질의(자료, "SELECT x FROM t")\n'
            '출력(행들[0]["x"])\n'
            '자료닫기(자료)\n'
        )
        assert out == "한글값\n"
    finally:
        shutil.rmtree(d)


def test_자료_변경행수_바인딩():
    # 변경 행 수 — CREATE 0, INSERT 1, UPDATE/DELETE 는 걸린 행 수. ? 바인딩은 특수문자도 값 그대로
    src = (
        '자료 = 자료열기(":memory:")\n'
        '출력(실행(자료, "CREATE TABLE 점수(이름 TEXT, 점 INTEGER)"))\n'
        '출력(실행(자료, "INSERT INTO 점수 VALUES (?, ?)", ["가", 10]))\n'
        '실행(자료, "INSERT INTO 점수 VALUES (?, ?)", ["나", 20])\n'
        '실행(자료, "INSERT INTO 점수 VALUES (?, ?)", ["다\'; DROP TABLE 점수; --", 30])\n'
        '출력(실행(자료, "UPDATE 점수 SET 점 = 점 + 1 WHERE 점 >= ?", [10]))\n'
        '행들 = 질의(자료, "SELECT 이름 FROM 점수 WHERE 점 = ?", [31])\n'
        '출력(행들[0]["이름"])\n'
        '출력(실행(자료, "DELETE FROM 점수 WHERE 점 > ?", [0]))\n'
        '자료닫기(자료)\n'
    )
    assert run(src) == "0\n1\n3\n다'; DROP TABLE 점수; --\n3\n"


def test_자료_없는경로_가나다오류():
    # 없는 디렉터리의 DB 파일 → 가나다 오류(시도/잡기로 잡힘)
    src = (
        '시도 {\n'
        '    자료열기("/없는_폴더_가나다_테스트/x.db")\n'
        '    출력("여기 오면 안 됨")\n'
        '} 잡기(오류) {\n'
        '    출력("잡음")\n'
        '    출력(포함(오류, "자료열기"))\n'
        '}\n'
    )
    assert run(src) == "잡음\n참\n"


def test_자료_핸들검증():
    # 핸들 아닌 값을 넘기면 안내 오류 (실행·질의·자료닫기 공통)
    src = (
        '시도 { 실행(123, "SELECT 1") } 잡기(오류) { 출력(포함(오류, "핸들")) }\n'
        '시도 { 질의("x", "SELECT 1") } 잡기(오류) { 출력(포함(오류, "핸들")) }\n'
        '시도 { 자료닫기([1]) } 잡기(오류) { 출력(포함(오류, "핸들")) }\n'
    )
    assert run(src) == "참\n참\n참\n"


def test_저장소_왕복():
    # 저장소(경로, 이름) — SQL 없이 넣기→모두 왕복(한글 키·값 그대로), 다시 열어도 유지(파일·자동 커밋)
    import tempfile
    import shutil
    d = tempfile.mkdtemp()
    경로 = os.path.join(d, "한몸.db")
    try:
        out = run(
            '방명록 = 저장소("' + 경로 + '", "글들")\n'
            '번호 = 넣기(방명록, {"이름": "철수", "내용": "안녕하세요"})\n'
            '출력(번호)\n'
            '출력(넣기(방명록, {"이름": "영희", "내용": "반가워요"}))\n'
            '글들 = 모두(방명록)\n'
            '출력(길이(글들))\n'
            '출력(글들[0]["이름"] + ": " + 글들[0]["내용"])\n'
            '출력(글들[1]["번호"])\n'                               # 모두() 가 기록마다 "번호" 부여
        )
        assert out == "1\n2\n2\n철수: 안녕하세요\n2\n"
        # 새로 열어도 데이터 유지
        assert run('출력(길이(모두(저장소("' + 경로 + '", "글들"))))') == "2\n"
    finally:
        shutil.rmtree(d)


def test_저장소_찾기():
    # 찾기(저장소, 조건사전) — 키=값 전부 일치(여러 키는 AND), 없으면 빈 목록. 기존 목록 찾기(find)는 그대로
    src = (
        '창고 = 저장소(":memory:", "물건")\n'
        '넣기(창고, {"이름": "사과", "색": "빨강", "개수": 3})\n'
        '넣기(창고, {"이름": "바나나", "색": "노랑", "개수": 5})\n'
        '넣기(창고, {"이름": "체리", "색": "빨강", "개수": 3})\n'
        '빨강들 = 찾기(창고, {"색": "빨강"})\n'
        '출력(길이(빨강들))\n'
        '출력(빨강들[1]["이름"])\n'
        '출력(길이(찾기(창고, {"색": "빨강", "개수": 3})))\n'      # 여러 키 AND
        '출력(찾기(창고, {"색": "파랑"}))\n'                        # 매치 없음 → 빈 목록
        '출력(찾기([1, 3, 4], 람다(x) { 반환 x % 2 == 0 }))\n'      # 목록 찾기(find) 하위호환
    )
    assert run(src) == "2\n체리\n2\n[]\n4\n"


def test_저장소_고치기_빼기():
    # 고치기 — 그 번호 기록에 사전의 키들만 덮어씀(부분 수정). 빼기 — 삭제. 둘 다 성공여부 반환
    src = (
        '방명록 = 저장소(":memory:", "글들")\n'
        '번호 = 넣기(방명록, {"이름": "철수", "내용": "처음"})\n'
        '출력(고치기(방명록, 번호, {"내용": "고침"}))\n'
        '출력(모두(방명록)[0]["이름"] + ": " + 모두(방명록)[0]["내용"])\n'   # 안 고친 키(이름)는 유지
        '출력(고치기(방명록, 999, {"내용": "x"}))\n'                          # 없는 번호 → 거짓
        '출력(빼기(방명록, 번호))\n'
        '출력(길이(모두(방명록)))\n'
        '출력(빼기(방명록, 번호))\n'                                          # 이미 없음 → 거짓
    )
    assert run(src) == "참\n철수: 고침\n거짓\n참\n0\n거짓\n"


def test_거래_롤백_커밋():
    # 거래(핸들, 함수) — 함수 안 오류 나면 전부 롤백, 성공하면 커밋(원자적). 저장소·자료열기 둘 다 OK
    # 실패 거래: 넣기 후 발생(오류) → 롤백 → 0건
    src = (
        '창고 = 저장소(":memory:", "물건")\n'
        '시도 {\n'
        '    거래(창고, 람다() {\n'
        '        넣기(창고, {"이름": "가"})\n'
        '        발생("중단")\n'
        '    })\n'
        '} 잡기(오류) { 출력("취소: " + 오류) }\n'
        '출력(길이(모두(창고)))\n'          # 롤백되어 0건
        '거래(창고, 람다() {\n'
        '    넣기(창고, {"이름": "나"})\n'
        '    넣기(창고, {"이름": "다"})\n'
        '})\n'
        '출력(길이(모두(창고)))\n'          # 성공 → 2건 지속
    )
    assert run(src) == "취소: 중단\n0\n2\n"


def test_저장소_검증오류():
    # 저장소 아닌 값·이상한 이름·사전 아닌 기록은 친절한 오류(시도/잡기로 잡힘)
    src = (
        '시도 { 넣기(123, {"가": 1}) } 잡기(오류) { 출력(포함(오류, "저장소")) }\n'
        '시도 { 저장소(":memory:", "이상한 이름!") } 잡기(오류) { 출력(포함(오류, "이름")) }\n'
        '창고 = 저장소(":memory:", "물건")\n'
        '시도 { 넣기(창고, [1, 2]) } 잡기(오류) { 출력(포함(오류, "사전")) }\n'
    )
    assert run(src) == "참\n참\n참\n"


def test_HTML_이스케이프():
    # 글·목록·문서 — 사용자 텍스트의 태그는 이스케이프(XSS 차단), 완성 조각(글 결과 등)은 그대로 통과
    assert run('출력(글("<script>alert(1)</script>"))') == "<p>&lt;script&gt;alert(1)&lt;/script&gt;</p>\n"
    assert run('출력(글("큰제목", 1))') == "<h1>큰제목</h1>\n"
    assert run('출력(글("소제목", 2))') == "<h2>소제목</h2>\n"
    assert run('출력(목록(["<b>안녕</b>", "가"]))') == "<ul><li>&lt;b&gt;안녕&lt;/b&gt;</li><li>가</li></ul>\n"
    out = run('출력(문서("제목<x>", 글("본문"), "생<i>글자"))')
    assert out.startswith("<!DOCTYPE html>")
    assert 'charset="utf-8"' in out
    assert "<title>제목&lt;x&gt;</title>" in out       # 제목도 이스케이프
    assert "<p>본문</p>" in out                        # 글() 조각은 태그 그대로 통과
    assert "생&lt;i&gt;글자" in out                    # 일반 문자열은 이스케이프
    assert "<i>" not in out


def test_연결_입력폼():
    # 연결 — <a>, 입력폼 — GET <form>. 주소·라벨·버튼 모두 이스케이프
    assert run('출력(연결("/목록", "돌아가기"))') == '<a href="/목록">돌아가기</a>\n'
    assert run('출력(연결("/a?x=1&y=2", "<보기>"))') == '<a href="/a?x=1&amp;y=2">&lt;보기&gt;</a>\n'
    out = run('출력(입력폼("/등록", ["이름", "내용"], "남기기"))')
    assert out == ('<form action="/등록" method="get">'
                   '<label>이름 <input name="이름"></label><br>'
                   '<label>내용 <input name="내용"></label><br>'
                   '<button>남기기</button></form>\n')
    assert '<button>보내기</button>' in run('출력(입력폼("/등록", ["이름"]))')   # 기본 버튼 텍스트


def test_한몸_통합():
    # 한 몸 풀스택(examples/한몸.ㄱㄴㄷ 스타일) — 임시 저장소 + 서버 기동, 등록→목록 HTTP 왕복
    import threading
    import tempfile
    import shutil
    import urllib.request
    import urllib.parse

    d = tempfile.mkdtemp()
    경로 = os.path.join(d, "방명록.db")
    src = (
        '방명록 = 저장소("' + 경로 + '", "글들")\n'
        '함수 첫화면(요청) {\n'
        '    글들 = 모두(방명록)\n'
        '    줄들 = []\n'
        '    반복 항목 를 글들 에서 { 추가(줄들, 항목["이름"] + ": " + 항목["내용"]) }\n'
        '    반환 문서("방명록", 글("방명록", 1), 목록(줄들), 입력폼("/등록", ["이름", "내용"], "남기기"))\n'
        '}\n'
        '함수 등록(요청) {\n'
        '    넣기(방명록, {"이름": 값얻기(요청["질의"], "이름", "익명"), "내용": 값얻기(요청["질의"], "내용", "")})\n'
        '    반환 문서("등록 완료", 글("등록 완료!"), 연결("/", "돌아가기"))\n'
        '}\n'
        '라우트 = {"/": 첫화면, "/등록": 등록}\n'
    )
    interp = 실행소스(src, out=io.StringIO())
    httpd = _웹서버만들기(interp, 0, interp.g.vars['라우트'])
    포트 = httpd.server_address[1]
    t = threading.Thread(target=httpd.serve_forever, daemon=True)
    t.start()
    try:
        base = f"http://127.0.0.1:{포트}"
        # 빈 방명록 — 완성된 HTML 페이지 + 입력 폼
        with urllib.request.urlopen(base + "/") as r:
            처음 = r.read().decode("utf-8")
        assert 처음.startswith("<!DOCTYPE html>")
        assert "<h1>방명록</h1>" in 처음 and '<form action="/등록" method="get"' in 처음
        assert "철수" not in 처음
        # 등록(브라우저 폼 GET 과 동일) → 안내 페이지
        url = (base + urllib.parse.quote("/등록") + "?"
               + urllib.parse.urlencode({"이름": "철수", "내용": "<b>안녕</b> 한몸!"}))
        with urllib.request.urlopen(url) as r:
            assert "등록 완료" in r.read().decode("utf-8")
        # 목록 화면에 반영 + 사용자 입력 이스케이프(XSS 차단)
        with urllib.request.urlopen(base + "/") as r:
            화면 = r.read().decode("utf-8")
        assert "<li>철수: &lt;b&gt;안녕&lt;/b&gt; 한몸!</li>" in 화면
        assert "<b>안녕</b>" not in 화면
    finally:
        httpd.shutdown()
        httpd.server_close()
        shutil.rmtree(d)


def test_모듈_사전반환():
    # 모듈(경로) → 네임스페이스 사전 — 함수 호출·변수 접근 모두 ["키"] 로
    out = run('수학 = 모듈("examples/lib.ㄱㄴㄷ")\n'
              '출력(수학["제곱"](5))\n'
              '출력(수학["곱하기"](6, 7))\n'
              '출력(수학["원주율"])')
    assert out == "25\n42\n3.14159\n"


def test_모듈_격리_vs_가져오기():
    # 모듈() 은 격리 — 모듈 안 이름이 호출측 env 를 오염하지 않음(플랫 가져오기와 대비)
    try:
        run('모듈("examples/lib.ㄱㄴㄷ")\n출력(원주율)')
        assert False, "이름 오류가 나야 함"
    except Exception as e:
        assert "'원주율'" in str(e) and "정의되지 않았습니다" in str(e)
    # 같은 파일을 가져오기(플랫)로 부르면 이름이 그대로 들어옴 — 대비 확인
    assert run('가져오기 "examples/lib.ㄱㄴㄷ"\n출력(원주율)') == "3.14159\n"


def test_모듈_캐시_상태공유():
    # 같은 경로 2회 모듈() → 같은 사전(캐시) — 모듈 상태(카운터)가 공유됨
    import tempfile
    import shutil
    d = tempfile.mkdtemp()
    p = os.path.join(d, "세기.ㄱㄴㄷ")
    with open(p, "w", encoding="utf-8") as f:
        f.write('카운터 = 0\n함수 증가() { 카운터 += 1\n반환 카운터 }\n')
    try:
        out = run('가 = 모듈("' + p + '")\n'
                  '나 = 모듈("' + p + '")\n'
                  '가["증가"]()\n'
                  '출력(나["증가"]())\n'      # 같은 사전 → 카운터 이어짐
                  '출력(가["카운터"])')       # 사전 값도 살아있는 모듈 상태
        assert out == "2\n2\n"
    finally:
        shutil.rmtree(d)


def test_모듈_오류_경로표면화():
    # 모듈 안 오류 → 경로가 담긴 가나다 오류로 표면화 / 없는 파일도 친절한 오류
    import tempfile
    import shutil
    d = tempfile.mkdtemp()
    p = os.path.join(d, "고장.ㄱㄴㄷ")
    with open(p, "w", encoding="utf-8") as f:
        f.write('출력(1)\n발생("펑")\n')
    try:
        try:
            run('모듈("' + p + '")')
            assert False, "모듈 오류가 나야 함"
        except Exception as e:
            assert "모듈" in str(e) and "고장.ㄱㄴㄷ" in str(e) and "펑" in str(e)
        out = run('시도 { 모듈("없는폴더/없는모듈.ㄱㄴㄷ") } 잡기(오류) { 출력(오류) }')
        assert "모듈을 불러올 수 없습니다" in out and "없는모듈.ㄱㄴㄷ" in out
    finally:
        shutil.rmtree(d)


def test_방명록앱_멀티파일_통합():
    # 멀티파일 앱(examples/방명록앱/) — 진짜 앱.ㄱㄴㄷ 부팅(환경변수로 포트 0·임시 자료경로 주입) 후
    # HTTP 등록→목록 왕복. 자료/화면/앱 세 파일이 모듈() 로 엮여 실제로 동작하는지 검증
    import threading
    import tempfile
    import shutil
    import time
    import re
    import urllib.request
    import urllib.parse

    d = tempfile.mkdtemp()
    os.environ['포트'] = '0'                       # 임의 포트(hermetic)
    os.environ['자료경로'] = os.path.join(d, '방명록.db')
    앱폴더 = os.path.join(ROOT, 'examples', '방명록앱')
    with open(os.path.join(앱폴더, '앱.ㄱㄴㄷ'), encoding='utf-8') as f:
        src = f.read()
    buf = io.StringIO()
    t = threading.Thread(target=실행소스, args=(src,),
                         kwargs={'out': buf, 'base_dir': 앱폴더}, daemon=True)
    t.start()
    try:
        포트 = None
        for _ in range(200):                        # 서버()가 출력한 실제 포트 대기
            m = re.search(r'localhost:(\d+)', buf.getvalue())
            if m:
                포트 = int(m.group(1))
                break
            time.sleep(0.05)
        assert 포트, "서버가 포트를 출력하지 않음: " + buf.getvalue()
        base = f"http://127.0.0.1:{포트}"
        with urllib.request.urlopen(base + "/") as r:
            처음 = r.read().decode("utf-8")
        assert "<h1>방명록</h1>" in 처음 and "철수" not in 처음
        url = (base + urllib.parse.quote("/등록") + "?"
               + urllib.parse.urlencode({"이름": "철수", "내용": "여러 파일!"}))
        with urllib.request.urlopen(url) as r:
            assert "등록 완료" in r.read().decode("utf-8")
        with urllib.request.urlopen(base + "/") as r:
            화면 = r.read().decode("utf-8")
        assert "철수: 여러 파일!" in 화면
    finally:
        os.environ.pop('포트', None)
        os.environ.pop('자료경로', None)
        shutil.rmtree(d)


if __name__ == '__main__':
    fns = [v for k, v in sorted(globals().items()) if k.startswith('test_') and callable(v)]
    failed = 0
    for fn in fns:
        try:
            fn()
            print(f"  ✓ {fn.__name__}")
        except Exception as e:
            failed += 1
            print(f"  ✗ {fn.__name__}: {e}")
    print(f"\n{len(fns) - failed}/{len(fns)} 통과")
    sys.exit(1 if failed else 0)
