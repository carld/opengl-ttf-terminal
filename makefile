

PLATFORM = $(shell uname)
REV = $(shell git rev-parse HEAD)

ifeq ($(findstring Linux,$(PLATFORM)),Linux)
  $(info Building revision $(REV) for $(PLATFORM))
else
  $(error $(PLATFORM) is not supported)
  $(shell exit 2)
endif

GIT = /usr/bin/git

CC = gcc

CFLAGS += -g -Wall -O2 
CFLAGS += -I./libtsm/src -I./libtsm -I./libshl/src -I./fontstash/src
CFLAGS += `sdl-config --cflags`
CFLAGS += -pg 

LFLAGS += `sdl-config --libs`
LFLAGS += -lm -lGL -lGLEW

TSM = $(wildcard libtsm/src/*.c) $(wildcard libtsm/external/*.c) 
SHL = libshl/src/shl_pty.c

SRC = $(SHL) $(TSM) main.c shader.c
OBJ = $(SRC:.c=.o)
BIN = togl

.PHONY: clean

.c.o:
	$(CC) -c $(CFLAGS) $< -o $@

$(BIN): $(OBJ)  fontstash/src/fontstash.h fontstash/src/glfontstash.h
	$(CC) -o $@ $(OBJ)  $(LFLAGS)

clean:
	rm -vf $(BIN) $(OBJ)


