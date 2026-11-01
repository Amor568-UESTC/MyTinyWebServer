CXX = g++
CFLAGS = -std=c++23 -O2 -Wall -g
CPPFLAGS += $(shell pkg-config --cflags tomlplusplus)
TOML_LIBS := $(shell pkg-config --libs tomlplusplus)

#默认在当前test目录生成程序,可通过BIN_DIR指定其他目录
BIN_DIR ?= .
TEST_PROGRAM := $(BIN_DIR)/configloaderTest
SOURCES := configloaderTest.cpp ../configloader.cpp
HEADERS := ../configloader.h ../serverconfig.h

.PHONY: all test clean
.DELETE_ON_ERROR:

all: $(TEST_PROGRAM)

$(TEST_PROGRAM): $(SOURCES) $(HEADERS) Makefile
	mkdir -p "$(BIN_DIR)"
	$(CXX) $(CPPFLAGS) $(CFLAGS) $(SOURCES) -o "$@" $(TOML_LIBS)

test: $(TEST_PROGRAM)
	"$(TEST_PROGRAM)"

clean:
	rm -f -- "$(TEST_PROGRAM)"
