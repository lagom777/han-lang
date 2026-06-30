# -*- coding: utf-8 -*-
"""한(Han) 인터프리터 테스트. 실행: python3 tests/test_han.py"""
import io
import os
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, ROOT)
os.chdir(ROOT)  # 상대경로 가져오기("examples/..") 가 cwd와 무관하게 동작하도록
from han import 실행소스, Interp, repl_eval, needs_more  # noqa: E402


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


def test_if_elif_else():
    src = '만약 1 > 2 { 출력("a") } 아니면 만약 2 > 1 { 출력("b") } 아니면 { 출력("c") }'
    assert run(src) == "b\n"


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
    assert run('출력(최대([5, 9, 2]))') == "9\n"
    assert run('출력(최소([5, 9, 2]))') == "2\n"
    assert run('출력(범위(5))') == "[0, 1, 2, 3, 4]\n"
    assert run('출력(범위(2, 5))') == "[2, 3, 4]\n"
    assert run('출력(거꾸로([1, 2, 3]))') == "[3, 2, 1]\n"


def test_builtins_string():
    assert run('출력(나누기("가,나,다", ","))') == "[가, 나, 다]\n"
    assert run('출력(합치기(["가", "나", "다"], "-"))') == "가-나-다\n"


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


def test_module_import():
    # 다른 .han 파일의 함수/변수를 가져와 사용
    out = run('가져오기 "examples/lib.han"\n출력(곱하기(6, 7))\n출력(제곱(5))')
    assert out == "42\n25\n"


def test_string_methods():
    assert run('출력(대문자("han"))') == "HAN\n"
    assert run('출력(소문자("HAN"))') == "han\n"
    assert run('출력(다듬기("  안녕  "))') == "안녕\n"
    assert run('출력(바꾸기("가나가", "가", "다"))') == "다나다\n"
    assert run('출력(포함("한글날", "글"))') == "참\n"
    assert run('출력(포함([1, 2, 3], 2))') == "참\n"
    assert run('출력(시작("한국어", "한"))') == "참\n"
    assert run('출력(끝("프로그램", "램"))') == "참\n"


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


def test_pad():
    assert run('출력(왼쪽채우기("5", 3, "0"))') == "005\n"        # 오른쪽 정렬, 0으로 채움
    assert run('출력(오른쪽채우기("가", 4, "."))') == "가...\n"    # 왼쪽 정렬, .으로 채움
    assert run('출력(왼쪽채우기(12, 5))') == "   12\n"            # 기본 공백, 숫자도 문자열화
    assert run('출력(오른쪽채우기("길다", 2))') == "길다\n"       # 너비보다 길면 그대로


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


def test_all_examples_run():
    # 모든 examples/*.han 이 오류 없이 실행되는지(회귀 방지)
    import glob
    os.environ.pop('OPENROUTER_API_KEY', None)   # ai.han → 안내 stub 경로
    exdir = os.path.join(ROOT, 'examples')
    paths = sorted(glob.glob(os.path.join(exdir, '*.han')))
    assert len(paths) >= 10
    old = sys.stdin
    try:
        for path in paths:
            sys.stdin = io.StringIO("홍길동\n30\n40\n50\n")  # input.han 용
            with open(path, encoding='utf-8') as f:
                src = f.read()
            try:
                실행소스(src, out=io.StringIO(), base_dir=exdir)
            except Exception as e:
                raise AssertionError(os.path.basename(path) + " 실행 실패: " + str(e))
    finally:
        sys.stdin = old


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
