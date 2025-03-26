
# Compiler and flags
CC = gcc
CFLAGS = -Wall -Wextra -g -std=c99 -Iinclude -I../Unity/src

# Directories
SRC_DIR = src
TEST_DIR = test
BUILD_DIR = build

# Default version (if no version is provided via the command line)
VERSION ?= open_table

# Source files
SRC = $(SRC_DIR)/$(VERSION).c
TEST_SRC = $(TEST_DIR)/test_open_table.c
UNITY_SRC = ../Unity/src/unity.c

# Object files
OBJ = $(BUILD_DIR)/$(VERSION).o
TEST_OBJS = $(BUILD_DIR)/test_open_table.o $(BUILD_DIR)/unity.o $(OBJ)

# Targets
.PHONY: all test clean

# 'all' builds the specified version
all: $(OBJ)

# Compile the version from src/
$(BUILD_DIR)/%.o: $(SRC_DIR)/%.c
	$(CC) $(CFLAGS) -c $< -o $@

# Compile the test file
$(BUILD_DIR)/test_open_table.o: $(TEST_SRC)
	$(CC) $(CFLAGS) -c $< -o $@

# Compile Unity framework
$(BUILD_DIR)/unity.o: $(UNITY_SRC)
	$(CC) $(CFLAGS) -c $< -o $@

# 'test' target links everything together and runs the test
test: $(TEST_OBJS)
	$(CC) $(CFLAGS) $(TEST_OBJS) -o $(BUILD_DIR)/test_open_table
	./$(BUILD_DIR)/test_open_table

# Clean build artifacts
clean:
	rm -f $(BUILD_DIR)/*

# Catch-all pattern rule to allow calling "make <filename>"
%:
	@$(MAKE) all VERSION=$@
