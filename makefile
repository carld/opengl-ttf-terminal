

PLATFORM = $(shell uname)
REV = $(shell git rev-parse HEAD)

ifeq ($(findstring Linux,$(PLATFORM)),Linux)
  $(info Building revision $(REV) for $(PLATFORM))
  $(info Using libsdl2 version $(shell sdl2-config --version))
else
  $(error $(PLATFORM) is not supported)
  $(shell exit 2)
endif

GIT = /usr/bin/git

CC = gcc

CFLAGS += -g -Wall -O2  -D_GNU_SOURCE
CFLAGS += -I./libtsm/src -I./libtsm -I./libshl/src -I./fontstash/src
CFLAGS += `sdl2-config --cflags`
CFLAGS += -pg 

LFLAGS += `sdl2-config --libs`
LFLAGS += -lm -lGL -lxkbcommon

TSM = $(wildcard libtsm/src/*.c) $(wildcard libtsm/external/*.c) 
SHL = libshl/src/shl_pty.c

SRC = $(SHL) $(TSM) main.c
OBJ = $(SRC:.c=.o)
BIN = togl

.PHONY: clean

.c.o:
	$(CC) -c $(CFLAGS) $< -o $@

$(BIN): $(OBJ)  fontstash/src/fontstash.h fontstash/src/glfontstash.h
	$(CC) -o $@ $(OBJ)  $(LFLAGS)

clean:
	rm -vf $(BIN) $(OBJ)


