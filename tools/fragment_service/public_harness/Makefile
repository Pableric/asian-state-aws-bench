CC ?= gcc
CFLAGS ?= -O3 -mavx512f -mavx512bw -mavx512vl -mavx512dq -Wall -Wextra

.PHONY: all test test-sde unit clean

all: block_harness

candidate_block_builder.o: candidate_block_builder.s
	$(CC) $(CFLAGS) -c $< -o $@

block_harness.o: block_harness.c
	$(CC) $(CFLAGS) -c $< -o $@

block_harness: candidate_block_builder.o block_harness.o
	$(CC) $^ -o $@

test: block_harness
	./block_harness

test-sde: block_harness
	/opt/intel-sde/sde64 -skx -- ./block_harness

unit:
	python3 -m unittest discover -s tests -p 'test_*.py'

clean:
	rm -f candidate_block_builder.o block_harness.o block_harness
