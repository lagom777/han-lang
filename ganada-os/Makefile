# 가나다 OS / 언어 — C 소스 없음. 호스트 검증용 aarch64 어셈블리 VM.
# 제품 목표: 베어메탈 OS (리눅스 사용자공간 아님). 지금은 macOS 호스트에서 VM만 검증.

ARCH ?= arm64
AS ?= as
LD ?= ld
SDK ?= $(shell xcrun --show-sdk-path 2>/dev/null)
PYTHON ?= python3

GVM = gvm
ASM = kernel/vm_host.s
OBJ = build/vm_host.o

TESTS_SRC = tests/hello.ㄱㄴㄷ tests/arith.ㄱㄴㄷ tests/vars.ㄱㄴㄷ tests/if.ㄱㄴㄷ
TESTS_GBC = $(TESTS_SRC:tests/%.ㄱㄴㄷ=build/%.gbc)

.PHONY: all test clean dirs

all: dirs $(GVM)

dirs:
	@mkdir -p build

$(OBJ): $(ASM) | dirs
	$(AS) -arch $(ARCH) -o $@ $(ASM)

$(GVM): $(OBJ)
	$(LD) -arch $(ARCH) -e _main -o $@ $(OBJ) -lSystem -syslibroot $(SDK)

build/%.gbc: tests/%.ㄱㄴㄷ bootstrap/gbcpack.py | dirs
	$(PYTHON) bootstrap/gbcpack.py $< $@

test: $(GVM) $(TESTS_GBC)
	@set -e; for t in hello arith vars if; do \
	  echo "=== $$t ==="; \
	  ./$(GVM) build/$$t.gbc | tee build/$$t.out; \
	  diff -u tests/$$t.golden build/$$t.out; \
	done
	@echo "ALL P0 TESTS PASSED (C-free host VM)"

clean:
	rm -rf build $(GVM)
