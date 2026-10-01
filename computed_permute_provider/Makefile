# Computed-permute-provider handoff v1. Builds only from files in this directory.

CC ?= gcc
PYTHON ?= python3
OBJDUMP ?= objdump
SDE ?= /opt/intel-sde/sde64
SDE_CPU ?= -spr
JOBS ?= 6

PREFIX := $(abspath .)
PREFIX_MAP := -ffile-prefix-map=$(PREFIX)=.
INCLUDES := -Iinclude -Ibaseline_affine_frozen -Itests
CFLAGS := -O2 -std=c11 -Wall -Wextra -Werror $(INCLUDES) $(PREFIX_MAP)
ASFLAGS := -Wall -Werror -mavx512f -Iasm -Ibaseline_affine_frozen -Ibench $(PREFIX_MAP)
B := build

HAVE_AVX512 := $(shell grep -m1 -o -w avx512f /proc/cpuinfo 2>/dev/null)
ifeq ($(HAVE_AVX512),avx512f)
RUN :=
RUN_MODE := native
else
RUN := $(SDE) $(SDE_CPU) --
RUN_MODE := sde$(SDE_CPU)
endif

PROVIDER_OBJS := $(B)/cpp_provider_prepare.o $(B)/cpp_directions.o $(B)/dim_provider_prepare.o
TEST_OBJS := $(B)/test_correctness.o $(B)/oracle_scalar.o $(B)/sha256.o $(B)/test_wrappers.o
BENCH_OBJS := $(B)/bench_ab.o $(B)/bench_kernels.o $(B)/oracle_scalar.o

BASE_COMMIT := b4b03df4eac1639e8b449a432b7543d66539c181

.PHONY: all test test-extended negative audit bench bench-verify results check clean

all: $(B)/test_correctness $(B)/bench_ab $(B)/audit_instantiate.o

$(B):
	mkdir -p $(B)

$(B)/%.o: src/%.c include/cpp_provider.h include/cpp_directions.h | $(B)
	$(CC) $(CFLAGS) -c $< -o $@

$(B)/dim_provider_prepare.o: baseline_affine_frozen/dim_provider_prepare.c baseline_affine_frozen/dim_provider.h | $(B)
	$(CC) $(CFLAGS) -c $< -o $@

$(B)/%.o: tests/%.c include/cpp_provider.h tests/oracle_scalar.h tests/sha256.h | $(B)
	$(CC) $(CFLAGS) -c $< -o $@

$(B)/test_wrappers.o: tests/test_wrappers.S asm/cpp_provider_fragments.inc baseline_affine_frozen/dim_provider_fragments.inc | $(B)
	$(CC) $(ASFLAGS) -c $< -o $@

$(B)/bench_ab.o: bench/bench_ab.c include/cpp_provider.h bench/bench_ab.h | $(B)
	$(CC) $(CFLAGS) -Ibench -c $< -o $@

$(B)/bench_kernels.o: bench/bench_kernels.S bench/bench_consumers.inc asm/cpp_provider_fragments.inc baseline_affine_frozen/dim_provider_fragments.inc | $(B)
	$(CC) $(ASFLAGS) -c $< -o $@

$(B)/audit_instantiate.o: audit/audit_instantiate.S bench/bench_consumers.inc asm/cpp_provider_fragments.inc baseline_affine_frozen/dim_provider_fragments.inc | $(B)
	$(CC) $(ASFLAGS) -c $< -o $@

$(B)/test_correctness: $(TEST_OBJS) $(PROVIDER_OBJS)
	$(CC) -o $@ $^

$(B)/bench_ab: $(BENCH_OBJS) $(PROVIDER_OBJS)
	$(CC) -o $@ $^

# Canonical scope (D1..D256 x blocks 0..15), far blocks, block-state sweep and
# register contract. Under SDE on hosts without avx512f.
test: $(B)/test_correctness
	$(RUN) ./$(B)/test_correctness --scopes contract,canonical,far,sweep \
		--records-out results/records_d1280.bin --json results/correctness_core.json
	@echo "run_mode=$(RUN_MODE)"

# D1..D1280 x blocks 0..255, sharded over $(JOBS) processes.
test-extended: $(B)/test_correctness
	$(PYTHON) tools/run_extended.py --jobs $(JOBS) --run "$(RUN)" \
		--binary ./$(B)/test_correctness --out results/correctness_extended.json

audit: $(B)/audit_instantiate.o $(B)/bench_kernels.o
	$(PYTHON) audit/audit_disasm.py --objdump $(OBJDUMP) \
		--object $(B)/audit_instantiate.o --bench-object $(B)/bench_kernels.o \
		--listing results/audit_disassembly.txt --out results/audit.json

# A/B timing. Refuses to time unless the host CPU itself reports avx512f.
bench: $(B)/bench_ab
	./$(B)/bench_ab --out-dir results/bench_native

# A/B consumption equality only (no timing); valid under SDE.
bench-verify: $(B)/bench_ab
	$(RUN) ./$(B)/bench_ab --verify-only --out-dir results/bench_verify

# Injected faults; each must make the contract + canonical run FAIL.
negative: $(B)/test_correctness
	$(PYTHON) tools/run_negative_controls.py --run "$(RUN)" \
		--binary ./$(B)/test_correctness --out results/negative_controls.json

results:
	$(PYTHON) tools/collect_results.py --base-commit $(BASE_COMMIT) --sde $(SDE) \
		--out results/handoff_results.json

check: test test-extended negative audit bench-verify bench results

clean:
	rm -rf $(B)
