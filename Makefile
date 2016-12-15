RM=rm -rf 

CC=clang++
OUT=randompic
CFLAGS=-std=c++14 -O3 -pedantic -Wall -Wextra -Wno-unused-function $(shell pkg-config --cflags libpng) $(shell pkg-config --cflags x11)
LINKFLAGS=$(shell pkg-config --libs libpng) $(shell pkg-config --libs x11) -lboost_program_options
OFILES=main.o

all: $(OUT)

$(OUT): $(OFILES)
	$(CC) -o $@ $(CFLAGS) $^ $(LINKFLAGS)

%.o: %.cpp
	$(CC) -c -o $@ $(CFLAGS) $<

clean:
	$(RM) *.o $(OUT)
