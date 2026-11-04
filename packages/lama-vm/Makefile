CC ?= gcc
CFLAGS ?= -O2 -g -std=gnu11 -Wall -Wextra -Wno-unused-parameter
LAMA_DIR ?= $(error LAMA_DIR is not set; run 'nix develop' first)
ROOT := $(abspath $(dir $(lastword $(MAKEFILE_LIST)))/../..)
RUNTIME_DIR := $(LAMA_DIR)/runtime
BUILD := build
BEAR ?= bear
COMPILE_COMMANDS := $(ROOT)/compile_commands.json

RUNTIME_FLAGS := -Wno-shift-negative-value
VM_SRCS := src/vm.c src/util.c src/unit.c src/decode.c src/link.c \
	   src/builtins.c src/execute.c
VM_OBJS := $(patsubst src/%.c,$(BUILD)/%.o,$(VM_SRCS))
VM_HEADERS := $(wildcard include/*.h)

.PHONY: all clean test bench compile-commands

all: $(BUILD)/lama-custom-vm

$(BUILD)/lama-custom-vm: $(VM_OBJS) $(BUILD)/runtime.o $(BUILD)/gc.o $(BUILD)/printf.o
	$(CC) $(CFLAGS) -o $@ $^

$(BUILD)/%.o: src/%.c $(VM_HEADERS) | $(BUILD)
	$(CC) $(CFLAGS) -Iinclude -I$(RUNTIME_DIR) -c $< -o $@

$(BUILD)/execute.o: CFLAGS += -foptimize-sibling-calls

$(BUILD)/runtime.o: $(RUNTIME_DIR)/runtime.c | $(BUILD)
	$(CC) $(CFLAGS) $(RUNTIME_FLAGS) -c $< -o $@

$(BUILD)/gc.o: $(RUNTIME_DIR)/gc.c | $(BUILD)
	$(CC) $(CFLAGS) $(RUNTIME_FLAGS) -c $< -o $@

$(BUILD)/printf.o: $(RUNTIME_DIR)/printf.S | $(BUILD)
	$(CC) $(CFLAGS) $(RUNTIME_FLAGS) -Wa,--noexecstack -x assembler-with-cpp -c $< -o $@

$(BUILD):
	mkdir -p $(BUILD)

compile-commands:
	$(BEAR) --output $(COMPILE_COMMANDS) -- $(MAKE) -B all

test: all
	bash $(ROOT)/scripts/run_tests.sh
	bash $(ROOT)/scripts/run_negative_tests.sh

bench: all
	bash $(ROOT)/scripts/bench.sh

clean:
	rm -rf $(BUILD)