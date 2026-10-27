CXX = g++
CFLAGS = -std=c++23 -O2 -Wall -g
CPPFLAGS += $(shell pkg-config --cflags tomlplusplus)
TOML_LIBS := $(shell pkg-config --libs tomlplusplus)

TARGET = server
BIN_DIR ?= ../bin
# 头文件由 #include 引入，不作为独立编译输入。
SOURCES := $(wildcard ../code/buffer/*.cpp ../code/pool/*.cpp ../code/log/*.cpp \
	../code/http/*.cpp ../code/timer/*.cpp ../code/server/*.cpp \
	../code/config/*.cpp ../code/handler/*.cpp ../code/main.cpp)

.PHONY: all clean config-test sql-test http-test log-test server-test handler-test

all: $(SOURCES)
	mkdir -p "$(BIN_DIR)"
	@printf 'Building %s\n' "$(BIN_DIR)/$(TARGET)"
	@set -eu; \
	buildTmpDir=$$(mktemp -d "$(BIN_DIR)/.$(TARGET).XXXXXX"); \
	trap 'rm -rf -- "$$buildTmpDir"' EXIT; \
	$(CXX) $(CPPFLAGS) $(CFLAGS) $(SOURCES) -o "$$buildTmpDir/$(TARGET)" \
		-pthread -lmysqlclient -lssl -lcrypto $(TOML_LIBS); \
	mv -f -- "$$buildTmpDir/$(TARGET)" "$(BIN_DIR)/$(TARGET)"

config-test:
	$(MAKE) -C ../code/config/test test

sql-test:
	$(MAKE) -C ../code/pool/test test

http-test:
	$(MAKE) -C ../code/http/test test

log-test:
	$(MAKE) -C ../code/log/test test

server-test:
	$(MAKE) -C ../code/server/test test

handler-test:
	$(MAKE) -C ../code/handler/test test

clean:
	$(MAKE) -C ../code/config/test clean
	$(MAKE) -C ../code/pool/test clean
	$(MAKE) -C ../code/http/test clean
	$(MAKE) -C ../code/log/test clean
	$(MAKE) -C ../code/server/test clean
	$(MAKE) -C ../code/handler/test clean
	rm -rf "$(BIN_DIR)"
	rm -rf ../log
