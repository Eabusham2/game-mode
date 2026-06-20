# GameMode - native Win32 (C) build.
#
# Native build on Windows with MinGW-w64:
#     mingw32-make
#
# Cross-compile from Linux/macOS with MinGW-w64:
#     make CC=x86_64-w64-mingw32-gcc WINDRES=x86_64-w64-mingw32-windres
#
# Produces GameMode.exe.

CC      ?= gcc
WINDRES ?= windres
CFLAGS  := -O2 -Wall -Wextra -std=c99 -DWINVER=0x0601 -D_WIN32_WINNT=0x0601
LDFLAGS := -mwindows -static
LIBS    := -lcomctl32 -ladvapi32 -lgdi32 -luser32 -lkernel32 -lshell32 -lole32 -lpsapi

SRC := src/known_lists.c src/modes.c src/config.c src/engine.c src/gui.c
OBJ := $(SRC:.c=.o)

TARGET := GameMode.exe

all: $(TARGET)

$(TARGET): $(OBJ) resource.o
	$(CC) $(OBJ) resource.o -o $@ $(LDFLAGS) $(LIBS)

%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@

resource.o: resource.rc app.manifest
	$(WINDRES) -i resource.rc -o resource.o

clean:
	-rm -f $(OBJ) resource.o $(TARGET)

.PHONY: all clean
