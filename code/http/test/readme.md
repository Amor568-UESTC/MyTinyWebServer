

g++ -std=c++17 -pthread -g -O0 -Wall -Wextra httpsconnTest.cpp ../*.cpp ../../buffer/*.cpp ../../log/log.cpp ../../pool/sqlconnpool.cpp -o httpsconnTest -lssl -lcrypto
