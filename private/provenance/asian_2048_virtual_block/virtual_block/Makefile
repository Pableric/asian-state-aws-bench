CC ?= gcc
CFLAGS ?= -O2 -std=c11 -Wall -Wextra -Werror
PREFIX := $(abspath .)
ROOT := $(abspath ..)

.PHONY: all test report clean

all: test_virtual_block virtual_block_tool

virtual_block.o: virtual_block.c virtual_block.h
	$(CC) $(CFLAGS) -c $< -o $@

test_virtual_block: test_virtual_block.c virtual_block.o virtual_block.h
	$(CC) $(CFLAGS) -o $@ test_virtual_block.c virtual_block.o

virtual_block_tool: virtual_block_cli.c virtual_block.o virtual_block.h
	$(CC) $(CFLAGS) -o $@ virtual_block_cli.c virtual_block.o

test: test_virtual_block
	./test_virtual_block

report: virtual_block_tool
	./virtual_block_tool report

clean:
	rm -f *.o test_virtual_block virtual_block_tool
