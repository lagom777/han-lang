CC ?= clang
CFLAGS ?= -O2 -std=c11 -Wall -Wno-unused-function
# Interpreter + LLVM codegen. na_rt.c is NOT linked into ganada — only into
# user native binaries via `가나다 llvm` (compile_llvm in main.c passes it to clang).
SRCS = src/main.c src/value.c src/lexer.c src/parser.c src/interp.c src/builtins.c src/gc.c src/codegen_llvm.c
HDRS = src/ganada.h src/tables.h src/na_rt.h
NA_RT = src/na_rt.c
# na_rt 소스는 바이트 배열로 실행 파일 안에 들어간다 — 설치하거나 옮긴 바이너리도 `가나다 빌드` 가
# 되게(예전엔 바이너리 옆 src/na_rt.c 를 찾아서, 설치하면 늘 실패했다). 자동 생성물이라 커밋하지 않는다.
EMBED = src/na_rt_embed.h

# glibc 는 -std=c11 이면 POSIX 함수를 숨긴다(strdup·strtok_r·getline·localtime_r·
# realpath·clock_gettime·sigaction·_longjmp·PATH_MAX…). macOS 헤더는 그냥 노출하지만
# 리눅스 빌드에는 이 정의가 필요하다. CFLAGS 를 덮어써도 사라지지 않게 따로 둔다.
DEFS = -D_GNU_SOURCE
# 인터프리터는 큰 스택의 스레드에서 돈다(main.c). 옛 glibc 는 -pthread 가 있어야 링크된다.
THREADS = -pthread

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

# 보안 회귀 그물 — 한때 메모리를 밟았던 입력들. 코퍼스와 따로 도는 이유는
# 골든 코퍼스가 tools/골든생성.py 로 다시 만들어지는 자동 생성물이기 때문이다.
SEC_WORK := /tmp/ganada-sec-work-$(shell echo $$$$)
SEC_OK = $(SEC_WORK)/통과표

# 네이티브 회귀 그물 — `실행` 의 네이티브 길이 인터프리터와 같은 결과를 내는지 두 길로 돌려 비교한다.
NATIVE_WORK := /tmp/ganada-native-work-$(shell echo $$$$)
NATIVE_OK = $(NATIVE_WORK)/통과표

ganada: $(SRCS) $(HDRS) $(EMBED)
	$(CC) $(CFLAGS) $(DEFS) $(THREADS) -o ganada $(SRCS) -lm

# od · sed 만 쓴다(POSIX — 리눅스·macOS·도커 빌드 단계 모두).
$(EMBED): src/na_rt.h $(NA_RT)
	@{ echo '/* 자동 생성 — make 가 src/na_rt.h · src/na_rt.c 로 만든다. 고치지 말 것 */'; \
	  for f in na_rt_h:src/na_rt.h na_rt_c:$(NA_RT); do \
	    echo "static const unsigned char EMBED_$${f%%:*}[] = {"; \
	    od -A n -v -t x1 "$${f#*:}" | sed -e 's/\([0-9a-f][0-9a-f]\)/0x\1,/g'; \
	    echo '0x00 };'; \
	  done; } > $@.tmp && mv $@.tmp $@

# 기본 회귀 검사 — 파이썬 없이 돈다(러너가 가나다로 쓰였고 C 바이너리가 실행한다).
# 통과표 검사는 러너가 도중에 죽거나 오류 종료코드가 깨진 경우까지 실패로 잡기 위한 것.
test: ganada
	@rm -rf $(GOLDEN_WORK)
	@$(GOLDEN_ENV) $(GANADA_BIN) 실행 tools/골든검사.ㄱㄴㄷ || true
	@test -f $(GOLDEN_OK) || { echo "[golden] 검사 실패 — 통과표 없음 (작업폴더 남김: $(GOLDEN_WORK))"; exit 1; }
	@rm -rf $(GOLDEN_WORK)
	@rm -rf $(SEC_WORK)
	@GANADA_SEC_WORK=$(SEC_WORK) GANADA_SEC_OK=$(SEC_OK) $(GANADA_BIN) 실행 tests/보안회귀.ㄱㄴㄷ || true
	@test -f $(SEC_OK) || { echo "[보안회귀] 검사 실패 — 통과표 없음 (작업폴더 남김: $(SEC_WORK))"; exit 1; }
	@rm -rf $(SEC_WORK)
	@rm -rf $(NATIVE_WORK)
	@GANADA_BIN=$(GANADA_BIN) GANADA_NATIVE_WORK=$(NATIVE_WORK) GANADA_NATIVE_OK=$(NATIVE_OK) $(GANADA_BIN) 실행 tests/네이티브회귀.ㄱㄴㄷ || true
	@test -f $(NATIVE_OK) || { echo "[네이티브회귀] 검사 실패 — 통과표 없음 (작업폴더 남김: $(NATIVE_WORK))"; exit 1; }
	@rm -rf $(NATIVE_WORK)

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

# Smoke-link na_rt against a dummy to catch API breakages (optional).
check-na-rt: $(NA_RT) src/na_rt.h
	$(CC) $(CFLAGS) -c -o /tmp/na_rt.o $(NA_RT)

clean:
	rm -f ganada $(EMBED)

.PHONY: clean test test-conform golden golden-check install uninstall check-na-rt
