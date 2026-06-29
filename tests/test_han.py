# -*- coding: utf-8 -*-
"""한(Han) 인터프리터 테스트. 실행: python3 tests/test_han.py"""
import io
import os
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, ROOT)
os.chdir(ROOT)  # 상대경로 가져오기("examples/..") 가 cwd와 무관하게 동작하도록
from han import 실행소스  # noqa: E402


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


def test_runtime_error_has_line():
    # 런타임 오류에 행 번호가 붙는다(2번째 줄에서 미정의 변수)
    try:
        run('출력(1)\n출력(없는변수)')
        assert False, "오류가 나야 함"
    except Exception as e:
        assert '2행' in str(e)


def test_module_import():
    # 다른 .han 파일의 함수/변수를 가져와 사용
    out = run('가져오기 "examples/lib.han"\n출력(곱하기(6, 7))\n출력(제곱(5))')
    assert out == "42\n25\n"


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
