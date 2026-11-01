CXX = g++
CFLAGS = -std=c++23 -O2 -Wall -g
BIN_DIR ?= .
TEST_PROGRAM := $(BIN_DIR)/logTest
SOURCES := logTest.cpp ../log.cpp ../../buffer/buffer.cpp
HEADERS := ../log.h ../blockqueue.hpp ../../buffer/buffer.h

.PHONY: all test clean
.DELETE_ON_ERROR:

all: $(TEST_PROGRAM)

$(TEST_PROGRAM): $(SOURCES) $(HEADERS) Makefile
	mkdir -p "$(BIN_DIR)"
	$(CXX) $(CPPFLAGS) $(CFLAGS) -pthread $(SOURCES) -o "$@"

test: $(TEST_PROGRAM)
	@set -e; \
	testCases=$$("$(TEST_PROGRAM)" --list); \
	for testCase in $$testCases; do \
		timeout 15s "$(TEST_PROGRAM)" "$$testCase"; \
	done

clean:
	rm -f -- "$(TEST_PROGRAM)"
