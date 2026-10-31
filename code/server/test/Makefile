CXX = g++
CFLAGS = -std=c++23 -O2 -Wall -g
CPPFLAGS += $(shell pkg-config --cflags tomlplusplus)
TOML_LIBS := $(shell pkg-config --libs tomlplusplus)
BIN_DIR ?= .
TEST_PROGRAM := $(BIN_DIR)/serverStartupTest
MAIN_OBJECT := $(BIN_DIR)/serverMain.o
SOURCES := serverStartupTest.cpp $(wildcard ../*.cpp ../../buffer/*.cpp ../../config/*.cpp \
	../../handler/*.cpp ../../http/*.cpp ../../log/*.cpp ../../pool/*.cpp ../../timer/*.cpp ../../plugin/*.cpp)
HEADERS := $(wildcard ../*.h ../../buffer/*.h ../../config/*.h ../../http/*.h \
	../../handler/*.h ../../log/*.h ../../log/*.hpp ../../pool/*.h ../../timer/*.h ../../plugin/*.h)
PLUGIN_FIXTURE_DIR := $(BIN_DIR)/plugin-fixtures
PLUGIN_LIBRARIES := $(addprefix $(PLUGIN_FIXTURE_DIR)/,valid.so badAbi.so createFailure.so)
WRAPS := mysql_server_init mysql_init mysql_real_connect mysql_error mysql_errno mysql_close \
	socket setsockopt bind listen fcntl epoll_create1 epoll_ctl close

.PHONY: all test clean
.DELETE_ON_ERROR:

all: $(TEST_PROGRAM) $(PLUGIN_LIBRARIES)

$(MAIN_OBJECT): ../../main.cpp $(HEADERS) Makefile
	mkdir -p "$(BIN_DIR)"
	$(CXX) $(CPPFLAGS) $(CFLAGS) -Dmain=TinywebServerMain -c ../../main.cpp -o "$@"

$(TEST_PROGRAM): $(SOURCES) $(HEADERS) $(MAIN_OBJECT) Makefile
	$(CXX) $(CPPFLAGS) $(CFLAGS) -pthread $(SOURCES) $(MAIN_OBJECT) -o "$@" \
		-lmysqlclient -lssl -lcrypto -ldl $(TOML_LIBS) $(addprefix -Wl$(comma)--wrap=,$(WRAPS))

$(PLUGIN_FIXTURE_DIR)/%.so: ../../plugin/test/fixture.cpp ../../plugin/plugin_api.h Makefile
	mkdir -p "$(PLUGIN_FIXTURE_DIR)"
	$(CXX) $(CPPFLAGS) $(CFLAGS) -fPIC -shared -fvisibility=hidden -fno-gnu-unique \
		-DFIXTURE_MODE=FixtureMode::$* "$<" -o "$@"

comma := ,

test: all
	@set -e; \
	testCases=$$("$(TEST_PROGRAM)" --list); \
	for testCase in $$testCases; do \
		timeout 15s "$(TEST_PROGRAM)" "$$testCase" "$(PLUGIN_FIXTURE_DIR)"; \
	done

clean:
	rm -f -- "$(TEST_PROGRAM)" "$(MAIN_OBJECT)"
	rm -f -- $(PLUGIN_LIBRARIES)
