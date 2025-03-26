# Directories
SRC_DIR = src
INC_DIR = include
TEST_DIR = test
UNITY_DIR = ../Unity/src
BUILD_DIR = build
BIN_DIR = bin

# CSV output directory 
CSV_DIR = benchmark/results

# Compiler and Flags
CC = gcc
CFLAGS = -Wall -Wextra -pedantic -std=c99 -g -I$(INC_DIR) -I$(UNITY_DIR)
CFLAGS_DEBUG = -DDEBUG_HASHTAB

# Source Files
LIB_SRCS = $(SRC_DIR)/open_addressing.c $(SRC_DIR)/cmp_func.c $(SRC_DIR)/hash_func.c $(SRC_DIR)/probe_func.c
TEST_SRCS = $(TEST_DIR)/test_open_addressing.c $(UNITY_DIR)/unity.c
BENCHMARK_SRCS = $(TEST_DIR)/benchmark_hashtab.c
MAIN_SRCS = $(SRC_DIR)/main.c

# Targets
LIB = $(BUILD_DIR)/libhashtable.a
TEST_EXEC = $(BIN_DIR)/test_open_addressing
BENCHMARK_EXEC = $(BIN_DIR)/benchmark_hashtab
MAIN_EXEC = $(BIN_DIR)/hashtable_main

# Object Files
LIB_OBJS = $(patsubst $(SRC_DIR)/%.c, $(BUILD_DIR)/%.o, $(LIB_SRCS))
TEST_OBJS = $(patsubst $(TEST_DIR)/%.c, $(BUILD_DIR)/%.o, $(TEST_SRCS))
BENCHMARK_OBJS = $(patsubst $(TEST_DIR)/%.c, $(BUILD_DIR)/%.o, $(BENCHMARK_SRCS))
MAIN_OBJS = $(patsubst $(SRC_DIR)/%.c, $(BUILD_DIR)/%.o, $(MAIN_SRCS))

# Headers
HEADERS = $(INC_DIR)/open_addressing.h $(INC_DIR)/basic_func.h $(INC_DIR)/debug_hashtab.h

# Phony Targets
.PHONY: all clean test benchmark

# Default Target: Build Library and Test Executable
all: $(LIB) $(TEST_EXEC) $(MAIN_EXEC) $(BENCHMARK_EXEC)

# Ensure directories exist
$(BUILD_DIR):
	mkdir -p $(BUILD_DIR)

$(BIN_DIR):
	mkdir -p $(BIN_DIR)

# Build Static Library
$(LIB): $(LIB_OBJS) | $(BUILD_DIR)
	@echo "Creating static library $@..."
	ar rcs $@ $^

# Compile Object Files
$(BUILD_DIR)/%.o: $(SRC_DIR)/%.c $(HEADERS) | $(BUILD_DIR)
	@echo "Compiling $<..."
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/%.o: $(TEST_DIR)/%.c $(HEADERS) | $(BUILD_DIR)
	@echo "Compiling $<..."
	$(CC) $(CFLAGS) -c $< -o $@

# Build Test Executable
$(TEST_EXEC): $(TEST_OBJS) $(LIB) | $(BIN_DIR)
	@echo "Linking $@..."
	$(CC) $(CFLAGS) -o $@ $(TEST_OBJS) -L$(BUILD_DIR) -lhashtable

# Build Benchmark Executable
$(BENCHMARK_EXEC): $(BENCHMARK_OBJS) $(LIB) | $(BIN_DIR)
	@echo "Linking $@..."
	$(CC) $(CFLAGS) -o $@ $(BENCHMARK_OBJS) -L$(BUILD_DIR) -lhashtable

# Build Main Executable
$(MAIN_EXEC): $(MAIN_OBJS) $(LIB) | $(BIN_DIR)
	@echo "Linking $@..."
	$(CC) $(CFLAGS) -o $@ $(MAIN_OBJS) -L$(BUILD_DIR) -lhashtable

# Debug Build Target
debug: CFLAGS += $(CFLAGS_DEBUG)
debug: $(LIB) $(TEST_EXEC) $(MAIN_EXEC) $(BENCHMARK_EXEC)

# Test Target: Run the Test Executable
test: $(TEST_EXEC)
	@echo "Running tests..."
	./$(TEST_EXEC)

# Benchmark Target: Run the Benchmark Executable
benchmark: $(BENCHMARK_EXEC)
	@echo "Running benchmark..."
	./$(BENCHMARK_EXEC)

# Clean Up Build Files
clean:
	@echo "Cleaning up..."
	rm -rf $(BUILD_DIR) $(BIN_DIR)
