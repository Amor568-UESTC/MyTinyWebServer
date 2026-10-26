.PHONY: all clean config-test sql-test http-test log-test server-test

all:
	$(MAKE) -C build

clean:
	$(MAKE) -C build clean

config-test:
	$(MAKE) -C build config-test

sql-test:
	$(MAKE) -C build sql-test

http-test:
	$(MAKE) -C build http-test

log-test:
	$(MAKE) -C build log-test

server-test:
	$(MAKE) -C build server-test

