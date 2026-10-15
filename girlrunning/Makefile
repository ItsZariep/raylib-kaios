CC = gcc
CFLAGS = -Wall -std=c11 -O3
LDFLAGS = -lraylib -lGL -lm -lpthread -ldl -lrt -lX11

TARGET = game
SRC = main.c

RAYLIB_PATH ?= ../raylib

SHELL := /bin/bash

all: $(TARGET)

$(TARGET): $(SRC)
	$(CC) $(SRC) -o $(TARGET) $(CFLAGS) $(LDFLAGS)

clean:
	rm -f $(TARGET) index.html index.js index.wasm

run: all
	./$(TARGET)

# Emscripten Web Target
web:
# 	source /usr/lib/emsdk/emsdk_env.sh && emcc --version && emcc $(SRC) -o index.html
	source "/home/user/emsdk-3.1.29/emsdk_env.sh" && emcc --version && emcc $(SRC) -o index.html \
		-Wall -std=c11 -O2 \
		-DPLATFORM_WEB \
		-I/usr/include \
		~/.local/include/raylib/kaios/libraylib.a \
		-s USE_GLFW=3 \
		-s WASM=0 \
		--shell-file shell.html \
		--preload-file assets \
		--memory-init-file 0 \
		2> >(grep -v "libxml2.so.2")

