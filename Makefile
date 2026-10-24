.PHONY: all clean config-test

all:
	mkdir -p bin
	cd build && make

clean:
	cd build && make clean

config-test:
	$(MAKE) -C build config-test

