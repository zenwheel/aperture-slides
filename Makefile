CC=gcc
VERSION=1.0
CFLAGS=-g -Wall -O3 -std=c99 -DDEBUG -D_GNU_SOURCE -DVERSION=\"$(VERSION)\" `pkg-config --cflags sdl2` `pkg-config --cflags MagickCore`
LDFLAGS=`pkg-config --libs sdl2` `pkg-config --libs MagickCore`
SRC=frame.c
DEPS=Makefile
OBJ=$(SRC:.c=.o)
OUT=frame

all: $(SRC) $(OUT)

$(OUT): $(OBJ)
	$(CC) $(OBJ) $(LDFLAGS) -o $@

.c.o: $(DEPS)
	$(CC) $(CFLAGS) -c $< -o $@

clean:
	rm -f $(OBJ) core $(OUT) $(OUT).core
