CXX = g++
CFLAGS = -std=c++23 -O2 -Wall -g

#只构建SQL连接池回归,原有ThreadPoolV2测试继续单独编译
BIN_DIR ?= .
TEST_PROGRAM := $(BIN_DIR)/sqlconnpoolTest
SOURCES := sqlconnpoolTest.cpp ../sqlconnpool.cpp ../../http/httprequest.cpp \
	../../buffer/buffer.cpp ../../log/log.cpp
HEADERS := $(wildcard ../*.h ../../http/*.h ../../buffer/*.h ../../log/*.h ../../log/*.hpp)
MYSQL_WRAPS := -Wl,--wrap=mysql_server_init -Wl,--wrap=mysql_init \
	-Wl,--wrap=mysql_real_connect -Wl,--wrap=mysql_error \
	-Wl,--wrap=mysql_errno -Wl,--wrap=mysql_close \
	-Wl,--wrap=mysql_thread_init -Wl,--wrap=mysql_thread_end

.PHONY: all test clean
.DELETE_ON_ERROR:

all: $(TEST_PROGRAM)

$(TEST_PROGRAM): $(SOURCES) $(HEADERS) Makefile
	mkdir -p "$(BIN_DIR)"
	$(CXX) $(CPPFLAGS) $(CFLAGS) -pthread $(SOURCES) -o "$@" -lmysqlclient $(MYSQL_WRAPS)

test: $(TEST_PROGRAM)
	@set -e; \
	testCases=$$("$(TEST_PROGRAM)" --list); \
	for testCase in $$testCases; do \
		timeout 10s "$(TEST_PROGRAM)" "$$testCase"; \
	done

clean:
	rm -f -- "$(TEST_PROGRAM)"
