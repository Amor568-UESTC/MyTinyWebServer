CXX = g++
CFLAGS = -std=c++23 -O2 -Wall -g
BIN_DIR ?= .
TEST_PROGRAM := $(BIN_DIR)/requesthandlerTest
SERVICE_TEST_PROGRAM := $(BIN_DIR)/userserviceTest
SOURCES := requesthandlerTest.cpp ../requesthandler.cpp ../../http/httprequest.cpp \
	../../buffer/buffer.cpp ../../log/log.cpp
HEADERS := $(wildcard ../*.h ../../http/httprequest.h ../../buffer/*.h ../../log/*.h ../../log/*.hpp)
WRAPS := mysql_server_init mysql_init mysql_real_connect mysql_close mysql_thread_init mysql_thread_end \
	mysql_stmt_init mysql_stmt_prepare mysql_stmt_bind_param mysql_stmt_execute mysql_stmt_bind_result \
	mysql_stmt_store_result mysql_stmt_num_rows mysql_stmt_affected_rows mysql_stmt_errno mysql_stmt_close
comma := ,

.PHONY: all test clean
.DELETE_ON_ERROR:

all: $(TEST_PROGRAM) $(SERVICE_TEST_PROGRAM)

$(TEST_PROGRAM): $(SOURCES) $(HEADERS) Makefile
	mkdir -p "$(BIN_DIR)"
	$(CXX) $(CPPFLAGS) $(CFLAGS) -pthread $(SOURCES) -o "$@"

$(SERVICE_TEST_PROGRAM): userserviceTest.cpp ../userservice.cpp ../../pool/sqlconnpool.cpp $(HEADERS) ../../pool/sqlconnRAII.h ../../pool/sqlconnpool.h Makefile
	mkdir -p "$(BIN_DIR)"
	$(CXX) $(CPPFLAGS) $(CFLAGS) -pthread userserviceTest.cpp ../userservice.cpp \
		../../pool/sqlconnpool.cpp ../../buffer/buffer.cpp ../../log/log.cpp -o "$@" \
		-lmysqlclient $(addprefix -Wl$(comma)--wrap=,$(WRAPS))

test: $(TEST_PROGRAM) $(SERVICE_TEST_PROGRAM)
	"$(TEST_PROGRAM)"
	@set -e; \
	testCases=$$("$(SERVICE_TEST_PROGRAM)" --list); \
	for testCase in $$testCases; do \
		timeout 10s "$(SERVICE_TEST_PROGRAM)" "$$testCase"; \
	done

clean:
	rm -f -- "$(TEST_PROGRAM)" "$(SERVICE_TEST_PROGRAM)"
