CXX = g++
CFLAGS = -std=c++23 -O0 -Wall -g

#构建解析、响应、连接处理链和TLS上下文回归,旧实验程序保留独立入口
BIN_DIR ?= .
TEST_PROGRAM := $(BIN_DIR)/httpresponseTest
TLS_TEST_PROGRAM := $(BIN_DIR)/sslcontextTest
REQUEST_TEST_PROGRAM := $(BIN_DIR)/httprequestTest
PROCESS_TEST_PROGRAM := $(BIN_DIR)/httpconnProcessTest
PLUGIN_FIXTURE_DIR := $(BIN_DIR)/plugin-fixtures
PLUGIN_LIBRARIES := $(addprefix $(PLUGIN_FIXTURE_DIR)/,versionTwo.so handleFailure.so binaryBody.so)
SOURCES := httpresponseTest.cpp ../httpresponse.cpp ../../buffer/buffer.cpp ../../log/log.cpp
HEADERS := $(wildcard ../httpresponse.h ../../buffer/*.h ../../log/*.h ../../log/*.hpp)

.PHONY: all test clean
.DELETE_ON_ERROR:

all: $(TEST_PROGRAM) $(TLS_TEST_PROGRAM) $(REQUEST_TEST_PROGRAM) $(PROCESS_TEST_PROGRAM) $(PLUGIN_LIBRARIES)

$(TEST_PROGRAM): $(SOURCES) $(HEADERS) Makefile
	mkdir -p "$(BIN_DIR)"
	$(CXX) $(CPPFLAGS) $(CFLAGS) -pthread $(SOURCES) -o "$@" -Wl,--wrap=mmap

$(TLS_TEST_PROGRAM): sslcontextTest.cpp ../sslContext.cpp ../sslContext.h ../../log/log.cpp $(HEADERS) Makefile
	mkdir -p "$(BIN_DIR)"
	$(CXX) $(CPPFLAGS) $(CFLAGS) -pthread sslcontextTest.cpp ../sslContext.cpp \
		../../buffer/buffer.cpp ../../log/log.cpp -o "$@" -lssl -lcrypto

$(REQUEST_TEST_PROGRAM): httprequestTest.cpp ../httprequest.cpp ../httprequest.h ../../buffer/buffer.cpp ../../log/log.cpp $(HEADERS) Makefile
	mkdir -p "$(BIN_DIR)"
	$(CXX) $(CPPFLAGS) $(CFLAGS) -pthread httprequestTest.cpp ../httprequest.cpp \
		../../buffer/buffer.cpp ../../log/log.cpp -o "$@"

$(PROCESS_TEST_PROGRAM): httpconnProcessTest.cpp ../httpconn.cpp ../httpsconn.cpp ../sslContext.cpp ../httprequest.cpp ../httpresponse.cpp $(wildcard ../../handler/*.cpp ../../handler/*.h ../../pool/*.h ../../plugin/*.h) ../../plugin/module.cpp ../httpconn.h ../httpsconn.h ../sslContext.h ../httprequest.h ../../buffer/iobuffer.cpp ../../buffer/iobuffer.h ../../buffer/buffer.cpp ../../pool/sqlconnpool.cpp ../../log/log.cpp $(HEADERS) Makefile
	mkdir -p "$(BIN_DIR)"
	$(CXX) $(CPPFLAGS) $(CFLAGS) -pthread httpconnProcessTest.cpp ../httpconn.cpp \
		../httprequest.cpp ../httpresponse.cpp ../httpsconn.cpp ../sslContext.cpp \
		../../handler/*.cpp ../../plugin/module.cpp ../../pool/sqlconnpool.cpp \
		../../buffer/buffer.cpp ../../buffer/iobuffer.cpp ../../log/log.cpp -o "$@" -lmysqlclient -lssl -lcrypto -ldl

$(PLUGIN_FIXTURE_DIR)/%.so: ../../plugin/test/fixture.cpp ../../plugin/plugin_api.h Makefile
	mkdir -p "$(PLUGIN_FIXTURE_DIR)"
	$(CXX) $(CPPFLAGS) $(CFLAGS) -fPIC -shared -fvisibility=hidden -fno-gnu-unique \
		-DFIXTURE_MODE=FixtureMode::$* "$<" -o "$@"

test: all
	"$(TEST_PROGRAM)"
	"$(REQUEST_TEST_PROGRAM)"
	"$(PROCESS_TEST_PROGRAM)" "$(PLUGIN_FIXTURE_DIR)/versionTwo.so" "$(PLUGIN_FIXTURE_DIR)/handleFailure.so" "$(PLUGIN_FIXTURE_DIR)/binaryBody.so"
	@set -e; \
	testCases=$$("$(TLS_TEST_PROGRAM)" --list); \
	for testCase in $$testCases; do \
		timeout 15s "$(TLS_TEST_PROGRAM)" "$$testCase"; \
	done

clean:
	rm -f -- "$(TEST_PROGRAM)" "$(TLS_TEST_PROGRAM)" "$(REQUEST_TEST_PROGRAM)" "$(PROCESS_TEST_PROGRAM)"
	rm -f -- $(PLUGIN_LIBRARIES)
