CC ?= clang
CFLAGS ?= -O2 -std=c11 -Wall -Wno-unused-function
SRCS = src/main.c src/value.c src/lexer.c src/parser.c src/interp.c src/builtins.c src/gc.c
HDRS = src/ganada.h src/tables.h

# glibc 는 -std=c11 이면 POSIX 함수를 숨긴다(strdup·strtok_r·getline·localtime_r·
# realpath·clock_gettime·sigaction·_longjmp·PATH_MAX…). macOS 헤더는 그냥 노출하지만
# 리눅스 빌드에는 이 정의가 필요하다. CFLAGS 를 덮어써도 사라지지 않게 따로 둔다.
DEFS = -D_GNU_SOURCE

# 골든 검사용 — 검사할 바이너리(러너를 돌리는 것도 이것)
GANADA_BIN ?= $(CURDIR)/ganada
GANADA_REPO ?= $(CURDIR)/..
GANADA_GOLDEN ?= $(CURDIR)/tests/golden
# 작업폴더는 실행마다 달라야 한다 — 고정 경로를 쓰면 동시에 돈 두 검사가 서로의 출력
# 캡처를 덮어써서 없는 불일치를 수십 건 만들어낸다(실제로 밟았다). := 로 한 번만 펼친다.
GOLDEN_WORK := /tmp/ganada-golden-work-$(shell echo $$$$)
GOLDEN_OK = $(GOLDEN_WORK)/통과표
GOLDEN_ENV = GANADA_REPO=$(GANADA_REPO) GANADA_GOLDEN=$(GANADA_GOLDEN) \
             GANADA_BIN=$(GANADA_BIN) GANADA_GOLDEN_OK=$(GOLDEN_OK) \
             GANADA_GOLDEN_WORK=$(GOLDEN_WORK)

ganada: $(SRCS) $(HDRS)
	$(CC) $(CFLAGS) $(DEFS) -o ganada $(SRCS) -lm

# 기본 회귀 검사 — 파이썬 없이 돈다(러너가 가나다로 쓰였고 C 바이너리가 실행한다).
# 통과표 검사는 러너가 도중에 죽거나 오류 종료코드가 깨진 경우까지 실패로 잡기 위한 것.
test: ganada
	@rm -rf $(GOLDEN_WORK)
	@$(GOLDEN_ENV) $(GANADA_BIN) 실행 tools/골든검사.ㄱㄴㄷ || true
	@test -f $(GOLDEN_OK) || { echo "[golden] 검사 실패 — 통과표 없음 (작업폴더 남김: $(GOLDEN_WORK))"; exit 1; }
	@rm -rf $(GOLDEN_WORK)

# 교차 검증(파이썬 필요) — 가나다.py 를 정답지로 직접 대조
test-conform:
	python3 tools/conform.py all

# 골든 코퍼스 재생성(파이썬 필요). golden-check 는 쓰지 않고 대조만 한다
golden:
	python3 tools/골든생성.py

golden-check:
	python3 tools/골든생성.py --확인

# 설치 — 실행 이름은 `가나다`. ganada 도 같은 바이너리로 함께 깐다(ASCII 로 치고 싶을 때).
PREFIX ?= /usr/local
BINDIR = $(DESTDIR)$(PREFIX)/bin

install: ganada
	mkdir -p $(BINDIR)
	install -m 755 ganada $(BINDIR)/가나다
	install -m 755 ganada $(BINDIR)/ganada
	@echo "설치 완료: $(BINDIR)/가나다  (사용: 가나다 실행 파일.ㄱㄴㄷ)"

uninstall:
	rm -f $(BINDIR)/가나다 $(BINDIR)/ganada

clean:
	rm -f ganada

.PHONY: clean test test-conform golden golden-check install uninstall
