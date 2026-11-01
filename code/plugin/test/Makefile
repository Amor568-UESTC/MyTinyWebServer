CXX = g++
CFLAGS = -std=c++23 -O2 -Wall -Wextra -g
CPPFLAGS += -I..
BIN_DIR ?= .
TEST_PROGRAM := $(BIN_DIR)/moduleTest
SMOKE_PROGRAM := $(BIN_DIR)/pluginSmoke
MANAGER_PROGRAM := $(BIN_DIR)/pluginmanagerTest
FIXTURE_DIR := $(BIN_DIR)/fixtures
FIXTURE_MODES := valid nullApi badAbi badSize missingCreate missingHandle missingDestroy \
	createFailure nullInstance partialCreateFailure handleFailure oversizedBody \
	ignoredAppendFailure invalidBody ignoredInvalidBody emptyBody binaryBody exactLimit chunkedBody versionTwo
FIXTURE_LIBRARIES := $(addprefix $(FIXTURE_DIR)/,$(addsuffix .so,$(FIXTURE_MODES)))
FIXTURE_LIBRARIES += $(FIXTURE_DIR)/noEntry.so $(FIXTURE_DIR)/business.so
FIXTURE_LIBRARIES += $(FIXTURE_DIR)/blockingHandle.so $(FIXTURE_DIR)/blockingCreate.so
HEADERS := ../module.h ../plugin_api.h

.PHONY: all test clean
.DELETE_ON_ERROR:

all: $(TEST_PROGRAM) $(SMOKE_PROGRAM) $(MANAGER_PROGRAM) $(FIXTURE_LIBRARIES)

$(MANAGER_PROGRAM): pluginmanagerTest.cpp ../module.cpp ../pluginmanager.cpp ../pluginreloader.cpp $(wildcard ../*.h ../../log/*.h ../../log/*.hpp ../../buffer/*.h) ../../log/log.cpp ../../buffer/buffer.cpp Makefile
	mkdir -p "$(BIN_DIR)"
	$(CXX) $(CPPFLAGS) $(CFLAGS) pluginmanagerTest.cpp ../module.cpp ../pluginmanager.cpp \
		../pluginreloader.cpp ../../log/log.cpp ../../buffer/buffer.cpp -o "$@" -pthread -ldl

$(TEST_PROGRAM): moduleTest.cpp ../module.cpp $(HEADERS) Makefile
	mkdir -p "$(BIN_DIR)"
	$(CXX) $(CPPFLAGS) $(CFLAGS) moduleTest.cpp ../module.cpp -o "$@" -pthread -ldl

$(SMOKE_PROGRAM): pluginSmoke.cpp ../module.cpp $(HEADERS) Makefile
	mkdir -p "$(BIN_DIR)"
	$(CXX) $(CPPFLAGS) $(CFLAGS) pluginSmoke.cpp ../module.cpp -o "$@" -ldl

$(FIXTURE_DIR)/business.so: ../../../plugins/business.cpp ../plugin_api.h Makefile
	mkdir -p "$(FIXTURE_DIR)"
	$(CXX) $(CPPFLAGS) $(CFLAGS) -fPIC -shared -fvisibility=hidden "$<" -o "$@"

$(FIXTURE_DIR)/noEntry.so: fixture.cpp ../plugin_api.h Makefile
	mkdir -p "$(FIXTURE_DIR)"
	$(CXX) $(CPPFLAGS) $(CFLAGS) -fPIC -shared -fvisibility=hidden -fno-gnu-unique \
		-DFIXTURE_NO_ENTRY "$<" -o "$@"

$(FIXTURE_DIR)/blockingHandle.so: blockingfixture.cpp ../plugin_api.h Makefile
	mkdir -p "$(FIXTURE_DIR)"
	$(CXX) $(CPPFLAGS) $(CFLAGS) -fPIC -shared -fvisibility=hidden -fno-gnu-unique "$<" -o "$@"

$(FIXTURE_DIR)/blockingCreate.so: blockingfixture.cpp ../plugin_api.h Makefile
	mkdir -p "$(FIXTURE_DIR)"
	$(CXX) $(CPPFLAGS) $(CFLAGS) -fPIC -shared -fvisibility=hidden -fno-gnu-unique -DBLOCK_CREATE "$<" -o "$@"

$(FIXTURE_DIR)/%.so: fixture.cpp ../plugin_api.h Makefile
	mkdir -p "$(FIXTURE_DIR)"
	$(CXX) $(CPPFLAGS) $(CFLAGS) -fPIC -shared -fvisibility=hidden -fno-gnu-unique \
		-DFIXTURE_MODE=FixtureMode::$* "$<" -o "$@"

test: all
	timeout 30s "$(TEST_PROGRAM)" "$(FIXTURE_DIR)"
	timeout 30s "$(MANAGER_PROGRAM)" "$(FIXTURE_DIR)"
	"$(SMOKE_PROGRAM)" "$(FIXTURE_DIR)/business.so"

clean:
	rm -f -- "$(TEST_PROGRAM)" "$(SMOKE_PROGRAM)" "$(MANAGER_PROGRAM)" $(FIXTURE_LIBRARIES) "$(FIXTURE_DIR)/lifecycle.log" "$(FIXTURE_DIR)/manager-lifecycle.log"
