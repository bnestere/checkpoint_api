CC=gcc
CXX=$(CC)
FC=gfortran
CFLAGS=-g -O3
CXXFLAGS=-std=c++11
LDFLAGS=-lm

BC_OPTS=-flto -Wl,-plugin-opt=emit-llvm

SRC_FILES=cp_impl.c

LIB_FILENAME=libcp_api.so
HEADER_FILENAME=cp_api.h

PREFIX=/usr/local
LIB_PREFIX=lib64

default: build

build: so

so: $(SRC_FILES) $(HEADER_FILENAME)
	$(CC) -c  -shared $(CFLAGS) $(CXXFLAGS) -o $(LIB_FILENAME) -fPIC $(SRC_FILES) $(LDFLAGS)


install: so $(HEADER_FILENAME)
	sudo cp $(LIB_FILENAME) $(PREFIX)/$(LIB_PREFIX)
	sudo cp $(HEADER_FILENAME) $(PREFIX)/include

clean:
	rm $(LIB_FILENAME)
