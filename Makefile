CFLAGS = -MMD -g -Wall -pedantic
CXXFLAGS = -MMD -g -Wall -pedantic
LIBS = -lm
CC = gcc
CXX = g++
OFILES = $(patsubst %.c,%.o,$(wildcard *.c)) $(patsubst %.cpp,%.o,$(wildcard *.cpp))
DFILES = $(patsubst %.c,%.d,$(wildcard *.c)) $(patsubst %.cpp,%.d,$(wildcard *.cpp))
HFILES = $(wildcard *.h *.hpp)
PROG = cachesim

ifdef PROFILE
FAST=1
undefine DEBUG
CFLAGS += -pg
CXXFLAGS += -pg
LIBS += -pg
endif

ifdef DEBUG
CFLAGS += -DDEBUG
CXXFLAGS += -DDEBUG
endif

ifdef FAST
CFLAGS += -O3
CXXFLAGS += -O3
endif

.PHONY: all submit clean

all: $(PROG)

$(PROG): $(OFILES)
	$(CXX) -o $@ $^ $(LIBS)

%.o: %.c $(HFILES)
	$(CC) $(CFLAGS) -c -o $@ $<

%.o: %.cpp $(HFILES)
	$(CXX) $(CXXFLAGS) -c -o $@ $<

clean:
	rm -f $(TARBALL) $(PROG) $(OFILES) $(DFILES)

-include $(DFILES)