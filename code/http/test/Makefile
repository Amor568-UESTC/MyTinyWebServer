CXX = g++
CFLAGS = -std=c++23 -O0 -Wall -g

#默认只构建文件响应回归,旧HTTPS实验程序保持原有编译方式
BIN_DIR ?= .
TEST_PROGRAM := $(BIN_DIR)/httpresponseTest
TLS_TEST_PROGRAM := $(BIN_DIR)/sslcontextTest
SOURCES := httpresponseTest.cpp ../httpresponse.cpp ../../buffer/buffer.cpp ../../log/log.cpp
HEADERS := $(wildcard ../httpresponse.h ../../buffer/*.h ../../log/*.h ../../log/*.hpp)

.PHONY: all test clean
.DELETE_ON_ERROR:

all: $(TEST_PROGRAM) $(TLS_TEST_PROGRAM)

$(TEST_PROGRAM): $(SOURCES) $(HEADERS) Makefile
	mkdir -p "$(BIN_DIR)"
	$(CXX) $(CPPFLAGS) $(CFLAGS) -pthread $(SOURCES) -o "$@" -Wl,--wrap=mmap

$(TLS_TEST_PROGRAM): sslcontextTest.cpp ../sslContext.cpp ../sslContext.h ../../log/log.cpp $(HEADERS) Makefile
	mkdir -p "$(BIN_DIR)"
	$(CXX) $(CPPFLAGS) $(CFLAGS) -pthread sslcontextTest.cpp ../sslContext.cpp \
		../../buffer/buffer.cpp ../../log/log.cpp -o "$@" -lssl -lcrypto

test: $(TEST_PROGRAM) $(TLS_TEST_PROGRAM)
	"$(TEST_PROGRAM)"
	@set -e; \
	testCases=$$("$(TLS_TEST_PROGRAM)" --list); \
	for testCase in $$testCases; do \
		timeout 15s "$(TLS_TEST_PROGRAM)" "$$testCase"; \
	done

clean:
	rm -f -- "$(TEST_PROGRAM)" "$(TLS_TEST_PROGRAM)"
