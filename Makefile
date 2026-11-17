CC ?= cc
CFLAGS ?= -O2
SQLITE_INCLUDE_DIR ?= /usr/include

all: sqlitercs.so

sqlitercs.so: sqlite_rcs.c
	$(CC) $(CFLAGS) -fPIC -shared -I"$(SQLITE_INCLUDE_DIR)" $< -o $@

test: sqlitercs.so
	SQLITERCS_EXTENSION=./sqlitercs.so python -m unittest discover -s tests -v

clean:
	rm -f sqlitercs.so

.PHONY: all test clean
