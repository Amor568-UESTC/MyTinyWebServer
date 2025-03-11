all:
	mkdir -p bin
	cd build && make

clean:
	cd build && make clean
	