
# --- Trick to allow: make <table file name> benchmark ---
ifndef VERSION
  ifneq ($(firstword $(MAKECMDGOALS)),benchmark)
    VERSION := $(firstword $(MAKECMDGOALS))
    $(eval MAKECMDGOALS := $(wordlist 2,$(words $(MAKECMDGOALS)),$(MAKECMDGOALS)))
  endif
endif
# ---------------------------------------------------------

# Compiler settings
CC = gcc
CXX = g++
CFLAGS = -Wall -Wextra -g -std=c99 -Iinclude -I../external/Unity/src
CXXFLAGS = -Wall -Wextra -g -std=c++11 -Iinclude -I../external/Unity/src -I../external/benchmark/include

# Directories
SRC_DIR = src
BUILD_DIR = build
TEST_DIR = test

# Unity settings
UNITY_SRC = ../external/Unity/src/unity.c
UNITY_OBJ = $(BUILD_DIR)/unity.o

# Regular build for the table implementation
SRC = $(SRC_DIR)/$(VERSION).c
OBJ = $(BUILD_DIR)/$(VERSION).o

.PHONY: all test clean benchmark

# 'all' builds the specified table version (e.g. open_table)
all: $(OBJ)

$(BUILD_DIR)/%.o: $(SRC_DIR)/%.c
	$(CC) $(CFLAGS) -c $< -o $@

# Catch-all rule so that "make open_table" sets VERSION appropriately
%:
	@$(MAKE) all VERSION=$@

# 'test' target: builds and runs the Unity-based tests
test: $(OBJ) $(UNITY_OBJ)
	$(CC) $(CFLAGS) $(OBJ) $(UNITY_OBJ) $(TEST_DIR)/test_open_table.c -o $(BUILD_DIR)/test_open_table
	./$(BUILD_DIR)/test_open_table

# Clean build artifacts
clean:
	rm -f $(BUILD_DIR)/*

# --- Benchmark target ---
# Benchmark source file (placed in Open_Table/benchmark)
BENCH_SRC = test/benchmark_open_table.cpp
BENCH_OBJ = $(BUILD_DIR)/benchmark_$(VERSION).o
BENCH_BIN = $(BUILD_DIR)/benchmark_$(VERSION)

# Compile the benchmark source (with the table object as a dependency)
$(BENCH_OBJ): $(BENCH_SRC) $(OBJ)
	$(CXX) $(CXXFLAGS) -c $(BENCH_SRC) -o $(BENCH_OBJ)

# Link the benchmark executable: combine the benchmark object and the table object.
$(BENCH_BIN): $(BENCH_OBJ)
	$(CXX) $(CXXFLAGS) $(BENCH_OBJ) $(OBJ) -o $(BENCH_BIN) -L../external/benchmark/build/src -lbenchmark -lpthread

# 'benchmark' target: build and run the benchmark executable.
benchmark: $(BENCH_BIN)
	./$(BENCH_BIN)
# --- End Benchmark target ---
