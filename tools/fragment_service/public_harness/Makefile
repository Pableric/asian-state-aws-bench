CC ?= gcc
CFLAGS ?= -O3 -mavx512f -mavx512bw -mavx512vl -mavx512dq -Wall -Wextra

.PHONY: all test test-sde unit clean

all: fragment_harness

fragment_wrappers.o: fragment_wrappers.s candidate_fragment.inc
	$(CC) $(CFLAGS) -c fragment_wrappers.s -o $@

fragment_harness.o: fragment_harness.c
	$(CC) $(CFLAGS) -c fragment_harness.c -o $@

fragment_harness: fragment_wrappers.o fragment_harness.o
	$(CC) $^ -o $@

test: fragment_harness
	./fragment_harness

test-sde: fragment_harness
	/opt/intel-sde/sde64 -skx -- ./fragment_harness

unit:
	python3 -m unittest discover -s tests -p 'test_*.py'

clean:
	rm -f fragment_wrappers.o fragment_harness.o fragment_harness
