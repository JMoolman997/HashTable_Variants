ZIG ?= zig
ZIG_GLOBAL_CACHE_DIR ?= .zig-cache/global
RM := rm -rf

ZIG_BUILD := ZIG_GLOBAL_CACHE_DIR=$(ZIG_GLOBAL_CACHE_DIR) $(ZIG) build

.PHONY: all check help libht libht_resize htbench htbench_resize ht_test test clean

all: ## Build libraries, benchmark binaries, and the test binary.
	$(ZIG_BUILD) all

check: ## Build everything and run the C test suite.
	$(ZIG_BUILD) check

help: ## Show available make targets.
	@awk 'BEGIN { FS = ":.*## " } /^[A-Za-z0-9_-]+:.*## / { printf "  %-16s %s\n", $$1, $$2 }' $(firstword $(MAKEFILE_LIST))

libht: ## Build the fixed-capacity static library.
	$(ZIG_BUILD) libht

libht_resize: ## Build the resize-instrumented static library.
	$(ZIG_BUILD) libht_resize

htbench: ## Build the benchmark binary.
	$(ZIG_BUILD) htbench

htbench_resize: ## Build the resize-instrumented benchmark binary.
	$(ZIG_BUILD) htbench_resize

ht_test: ## Build the C test binary.
	$(ZIG_BUILD) ht_test

test: ## Build and run the C hash table test suite.
	$(ZIG_BUILD) test

clean: ## Remove build outputs.
	$(RM) zig-out .zig-cache zig-cache build htbench
