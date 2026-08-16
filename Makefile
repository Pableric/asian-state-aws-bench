CC ?= gcc
CFLAGS ?= -O3 -march=native -fPIC -Wall -Wextra -I. -Iprivate
LDFLAGS ?= -lm

.PHONY: all clean check bench test test-dynamic-sde test-reduced-fma-sde test-ordered-d1-sde

all: libeuropean_pricer.so bench_european bench_european_points

private/sobol.o: private/sobol.c private/sobol.h
	$(CC) -std=c23 $(CFLAGS) -c $< -o $@

private/sobol_gaussian_avx512.o: private/sobol_gaussian_avx512.s
	$(CC) $(CFLAGS) -c $< -o $@

sobol_european_avx512.o: sobol_european_avx512.s
	$(CC) $(CFLAGS) -c $< -o $@

sobol_european_reduced_fma_avx512.o: sobol_european_reduced_fma_avx512.s
	$(CC) $(CFLAGS) -c $< -o $@

sobol_european_ordered_d1_avx512.o: sobol_european_ordered_d1_avx512.s private/european_ordered_d1_tail.inc
	$(CC) $(CFLAGS) -c $< -o $@

sobol_european_center_shared_avx512.o: sobol_european_center_shared_avx512.s
	$(CC) $(CFLAGS) -c $< -o $@

sobol_european_dynamic_ranges_avx512.o: sobol_european_dynamic_ranges_avx512.s
	$(CC) $(CFLAGS) -c $< -o $@

sobol_european_direct_avx512.o: sobol_european_direct_avx512.s
	$(CC) $(CFLAGS) -c $< -o $@

sobol_hybrid.o: sobol_hybrid.s
	$(CC) $(CFLAGS) -c $< -o $@

european_pricer.o: european_pricer.c european_pricer.h private/sobol.h private/european_ordered_setup.h european_exp_table_64.h european_reduced_exp_slots.h european_direct_center_4096.h private/gaussian_split_tail_2048.h private/gaussian_center_shared_coeff_values_2048.h private/gaussian_dynamic_range_coeff_values.h
	$(CC) $(CFLAGS) -c $< -o $@

european_reduced_setup.o: european_reduced_setup.c private/european_reduced_setup.h european_exp_table_64.h european_reduced_exp_slots.h private/gaussian_dynamic_range_coeff_values.h
	$(CC) $(CFLAGS) -c $< -o $@

european_ordered_setup.o: european_ordered_setup.c private/european_ordered_setup.h private/european_ordered_d1_coeffs.h
	$(CC) $(CFLAGS) -c $< -o $@

libeuropean_pricer.so: european_pricer.o european_reduced_setup.o european_ordered_setup.o private/sobol.o private/sobol_gaussian_avx512.o sobol_european_avx512.o sobol_european_reduced_fma_avx512.o sobol_european_ordered_d1_avx512.o sobol_european_center_shared_avx512.o sobol_european_dynamic_ranges_avx512.o sobol_european_direct_avx512.o sobol_hybrid.o
	$(CC) -shared -o $@ $^ $(LDFLAGS)

bench_european: benchmarks/bench_european.c european_pricer.h libeuropean_pricer.so
	$(CC) -O3 -march=native -Wall -Wextra -I. benchmarks/bench_european.c -L. -leuropean_pricer -Wl,-rpath,'$$ORIGIN' -lm -o $@

bench_european_points: benchmarks/bench_european_points.c european_pricer.h libeuropean_pricer.so
	$(CC) -O3 -march=native -Wall -Wextra -I. benchmarks/bench_european_points.c -L. -leuropean_pricer -Wl,-rpath,'$$ORIGIN' -lm -o $@

bench_reduced_setup: benchmarks/bench_reduced_setup.c european_reduced_setup.c private/european_reduced_setup.h european_exp_table_64.h european_reduced_exp_slots.h private/gaussian_dynamic_range_coeff_values.h
	$(CC) $(CFLAGS) -DEUROPEAN_SETUP_TEST_REFERENCE benchmarks/bench_reduced_setup.c european_reduced_setup.c -lm -o $@

test_reduced_setup: tests/test_reduced_setup.c european_reduced_setup.c private/european_reduced_setup.h european_exp_table_64.h european_reduced_exp_slots.h private/gaussian_dynamic_range_coeff_values.h
	$(CC) $(CFLAGS) -DEUROPEAN_SETUP_TEST_REFERENCE tests/test_reduced_setup.c european_reduced_setup.c -lm -o $@

test_ordered_d1_kernel: tests/test_ordered_d1_kernel.c private/european_ordered_setup.h sobol_european_ordered_d1_avx512.o
	$(CC) -O3 -march=native -ffp-contract=off -Wall -Wextra -I. tests/test_ordered_d1_kernel.c sobol_european_ordered_d1_avx512.o -lm -o $@

bench: bench_european
	./bench_european 16 call
	./bench_european 16 put

test: all
	python3 tests/compare_european_reference.py --blocks 1 --type call
	python3 tests/compare_european_reference.py --blocks 1 --type put
	python3 tests/compare_european_reference.py --blocks 16 --type call
	python3 tests/compare_european_reference.py --blocks 16 --type put
	python3 tests/compare_european_reference.py --blocks 1 --type call --mode hybrid
	python3 tests/compare_european_reference.py --blocks 1 --type put --mode hybrid
	python3 tests/compare_european_reference.py --blocks 16 --type call --mode hybrid
	python3 tests/compare_european_reference.py --blocks 16 --type put --mode hybrid
	python3 tests/compare_european_reference.py --blocks 1 --type call --mode gaussian-center-shared
	python3 tests/compare_european_reference.py --blocks 1 --type put --mode gaussian-center-shared
	python3 tests/compare_european_reference.py --blocks 16 --type call --mode gaussian-center-shared
	python3 tests/compare_european_reference.py --blocks 16 --type put --mode gaussian-center-shared
	python3 tests/compare_european_reference.py --blocks 1 --type call --mode gaussian-dynamic-ranges
	python3 tests/compare_european_reference.py --blocks 1 --type put --mode gaussian-dynamic-ranges
	python3 tests/compare_european_reference.py --blocks 16 --type call --mode gaussian-dynamic-ranges
	python3 tests/compare_european_reference.py --blocks 16 --type put --mode gaussian-dynamic-ranges
	python3 tests/compare_european_reference.py --blocks 1 --type call --mode gaussian-split-tail
	python3 tests/compare_european_reference.py --blocks 1 --type put --mode gaussian-split-tail
	python3 tests/compare_european_reference.py --blocks 16 --type call --mode gaussian-split-tail
	python3 tests/compare_european_reference.py --blocks 16 --type put --mode gaussian-split-tail

test-dynamic-sde: all
	python3 generate_dynamic_range_coeffs.py --check
	python3 -m unittest tests/test_dynamic_range_coeffs.py
	/opt/intel-sde/sde64 -skx -- python3 tests/compare_dynamic_ranges.py
	/opt/intel-sde/sde64 -skx -omix /tmp/european-baseline.mix -- ./bench_european 1 call gaussian-exp
	/opt/intel-sde/sde64 -skx -omix /tmp/european-dynamic.mix -- ./bench_european 1 call gaussian-dynamic-ranges
	python3 tests/check_dynamic_mix.py /tmp/european-baseline.mix /tmp/european-dynamic.mix

test-reduced-fma-sde: all test_reduced_setup
	python3 generate_reduced_exp_schedule.py --check
	python3 -m unittest tests.test_reduced_exp_schedule
	/opt/intel-sde/sde64 -skx -- ./test_reduced_setup
	/opt/intel-sde/sde64 -skx -omix /tmp/european-reduced-setup.mix -- ./test_reduced_setup
	python3 tests/check_reduced_setup_mix.py /tmp/european-reduced-setup.mix
	/opt/intel-sde/sde64 -skx -- python3 tests/compare_reduced_fma.py
	/opt/intel-sde/sde64 -skx -omix /tmp/european-reduced-baseline.mix -- ./bench_european 1 call gaussian-exp
	/opt/intel-sde/sde64 -skx -omix /tmp/european-reduced-fma.mix -- ./bench_european 1 call gaussian-exp-reduced-fma
	python3 tests/check_reduced_fma_mix.py /tmp/european-reduced-baseline.mix /tmp/european-reduced-fma.mix

test-ordered-d1-sde: all test_ordered_d1_kernel
	python3 generate_ordered_d1_coeffs.py --check
	python3 -m unittest tests.test_ordered_d1_layout
	/opt/intel-sde/sde64 -skx -- ./test_ordered_d1_kernel
	/opt/intel-sde/sde64 -skx -- python3 tests/compare_ordered_d1.py
	/opt/intel-sde/sde64 -skx -omix /tmp/european-ordered-baseline.mix -- ./bench_european 1 call gaussian-exp-reduced-fma
	/opt/intel-sde/sde64 -skx -omix /tmp/european-ordered-d1.mix -- ./bench_european_points 8192 call
	/opt/intel-sde/sde64 -skx -omix /tmp/european-ordered-d1-131072.mix -- ./bench_european_points 131072 call
	python3 tests/check_ordered_d1_mix.py /tmp/european-ordered-baseline.mix /tmp/european-ordered-d1.mix ./sobol_european_ordered_d1_avx512.o /tmp/european-ordered-d1-131072.mix

check:
	@test -f private/sobol_gaussian_avx512.s
	@test -f private/gaussian_linear_coeff_values_2048.h
	@test -f private/gaussian_center_shared_coeff_values_2048.h
	@test -f private/gaussian_dynamic_range_coeff_values.h
	@test -f european_reduced_exp_slots.h
	@test -f private/european_ordered_d1_coeffs.h
	@test -f private/european_ordered_d1_tail.inc
	@test -f private/gaussian_split_tail_2048.h

clean:
	rm -f *.o private/*.o libeuropean_pricer.so bench_european bench_european_points \
		bench_reduced_setup test_reduced_setup test_ordered_d1_kernel
