CXX=g++
CFLAGS=-std=c++14 -O2 -Wall -g

TARGET=server
OBJS=../code/buffer/*.cpp ../code/pool/*.cpp ../code/log/*.cpp \
	../code/log/*.hpp ../code/http/*.cpp ../code/timer/*.cpp ../code/server/*.cpp \
	../code/main.cpp

all:$(OBJS)
	$(CXX) $(CFLAGS) $(OBJS) -o ../bin/$(TARGET) -pthread -lmysqlclient

clean:
	rm -rf ../bin