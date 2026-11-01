CXX = g++
CFLAGS = -std=c++23 -O2 -Wall -Wextra
CPPFLAGS += -I../code/plugin
VERSION ?= v1
TARGET := business.$(VERSION).so

.PHONY: all

all: $(TARGET)

$(TARGET): business.cpp ../code/plugin/plugin_api.h Makefile
	@set -eu; \
	if [ -e "$@" ]; then \
		printf 'Version already exists: %s. Use a new VERSION.\n' "$@" >&2; \
		exit 1; \
	fi; \
	pluginBuildDir=$$(mktemp -d .business.XXXXXX); \
	trap 'rm -rf -- "$$pluginBuildDir"' EXIT; \
	$(CXX) $(CPPFLAGS) $(CFLAGS) -fPIC -shared -fvisibility=hidden \
		-DTINYWEB_BUSINESS_VERSION='"$(VERSION)"' business.cpp -o "$$pluginBuildDir/business.so"; \
	ln -- "$$pluginBuildDir/business.so" "$@"; \
	printf 'Published %s\n' "$@"
