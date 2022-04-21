all: correction_simulation

CXX=gcc
CFLAGS=-D_GNU_SOURCE -Ofast -march=native -Wall -Wno-maybe-uninitialized
LDFLAGS=

SOURCES := $(wildcard *.c)
HEADERS := $(wildcard *.h)

correction_simulation: $(SOURCES) $(HEADERS)
	$(CXX) $(CFLAGS) $(SOURCES) -o $@ $(LDFLAGS)

clean:
	rm -f correction_simulation
