# -*- coding: utf-8 -*-
"""한(Han) 인터프리터 테스트. 실행: python3 tests/test_han.py"""
import io
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
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
