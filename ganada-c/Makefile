CC ?= clang
CFLAGS ?= -O2 -std=c11 -Wall -Wno-unused-function
SRCS = src/main.c src/value.c src/lexer.c src/parser.c src/interp.c src/builtins.c src/gc.c
HDRS = src/ganada.h src/tables.h

# 골든 검사용 — 검사할 바이너리(러너를 돌리는 것도 이것)
GANADA_BIN ?= $(CURDIR)/ganada
GANADA_REPO ?= $(CURDIR)/..
GANADA_GOLDEN ?= $(CURDIR)/tests/golden
GOLDEN_OK = /tmp/ganada-golden-work/통과표
GOLDEN_ENV = GANADA_REPO=$(GANADA_REPO) GANADA_GOLDEN=$(GANADA_GOLDEN) \
             GANADA_BIN=$(GANADA_BIN) GANADA_GOLDEN_OK=$(GOLDEN_OK)

ganada: $(SRCS) $(HDRS)
	$(CC) $(CFLAGS) -o ganada $(SRCS) -lm

# 기본 회귀 검사 — 파이썬 없이 돈다(러너가 가나다로 쓰였고 C 바이너리가 실행한다).
# 통과표 검사는 러너가 도중에 죽거나 오류 종료코드가 깨진 경우까지 실패로 잡기 위한 것.
test: ganada
	@rm -f $(GOLDEN_OK)
	@$(GOLDEN_ENV) $(GANADA_BIN) 실행 tools/골든검사.ㄱㄴㄷ || true
	@test -f $(GOLDEN_OK) || { echo "[golden] 검사 실패 — 통과표 없음"; exit 1; }

# 교차 검증(파이썬 필요) — 가나다.py 를 정답지로 직접 대조
test-conform:
	python3 tools/conform.py all

# 골든 코퍼스 재생성(파이썬 필요). golden-check 는 쓰지 않고 대조만 한다
golden:
	python3 tools/골든생성.py

golden-check:
	python3 tools/골든생성.py --확인

clean:
	rm -f ganada

.PHONY: clean test test-conform golden golden-check
