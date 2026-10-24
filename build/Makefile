CXX=g++
CFLAGS=-std=c++23 -O2 -Wall -g
CPPFLAGS += $(shell pkg-config --cflags tomlplusplus)
TOML_LIBS := $(shell pkg-config --libs tomlplusplus)

TARGET=server
OBJS=../code/buffer/*.cpp ../code/pool/*.cpp ../code/log/*.cpp \
	../code/log/*.hpp ../code/http/*.cpp ../code/timer/*.cpp ../code/server/*.cpp \
	../code/config/*.cpp ../code/main.cpp

.PHONY: all clean config-test

all:$(OBJS)
	$(CXX) $(CPPFLAGS) $(CFLAGS) $(OBJS) -o ../bin/$(TARGET) -pthread -lmysqlclient -lssl -lcrypto $(TOML_LIBS)

config-test:
	mkdir -p ../bin
	$(CXX) $(CPPFLAGS) $(CFLAGS) ../code/config/test/configloaderTest.cpp ../code/config/configloader.cpp -o ../bin/configloaderTest $(TOML_LIBS)
	../bin/configloaderTest

clean:
	rm -rf ../bin
	rm -rf ../log
