# X11 build (RVIP): make -C port   -> ../larn-x11, run from save/ (play.sh)
CC ?= cc
CFLAGS ?= -O2 -g
override CFLAGS += -std=gnu99 -DLARN_X11 -I. -I/opt/X11/include -I/opt/X11/include/freetype2 -w
LIBS = -L/opt/X11/lib -lX11 -lXft
GAME = $(wildcard ../*.c)
SRC = $(GAME) be_x11.c wcurses.c tiles.c rvip.c
OBJ = $(patsubst ../%.c,obj/%.o,$(GAME)) obj/be_x11.o obj/wcurses.o obj/tiles.o obj/rvip.o

all: ../larn-x11 tiles.rgba

../larn-x11: $(OBJ)
	$(CC) $(CFLAGS) -o $@ $(OBJ) $(LIBS) $(LDFLAGS)

obj/%.o: ../%.c ../*.h curses.h
	@mkdir -p obj
	$(CC) $(CFLAGS) -c -o $@ $<

obj/%.o: %.c ../*.h curses.h tilemap.h
	@mkdir -p obj
	$(CC) $(CFLAGS) -c -o $@ $<

tiles.rgba tilemap.h: mktiles.py amiga/*.png
	cd .. && python3 port/mktiles.py

clean:
	rm -rf obj ../larn-x11
