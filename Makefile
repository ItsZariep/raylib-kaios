SHELL := /bin/bash # needed for the 2> >(grep ...) process substitution

EMSDK_ENV ?= /home/zariep/emsdk-3.1.29/emsdk_env.sh
RAYLIB_WEB ?= $(HOME)/.local/include/raylib/kaios/libraylib.a
SHELL_HTML ?= common/shell.html
TEMPLATE ?= app-template

# Native build settings
CC = gcc
CFLAGS = -Wall -std=c11 -O3
LDFLAGS = -lraylib -lGL -lm -lpthread -ldl -lrt -lX11

# Web build
# $(1) = game directory, $(2) = extra emcc flags (e.g. --preload-file)
# Preloaded assets are mapped to /assets in the virtual FS, so the game
# can keep loading "assets/..." regardless of the build directory.
define WEB_BUILD
	source "$(EMSDK_ENV)" > /dev/null 2>&1 && emcc $(1)/main.c -o $(1)/index.html \
		-Wall  -O2 \
		-DPLATFORM_WEB \
		-I/usr/include \
		$(RAYLIB_WEB) \
		-s USE_GLFW=3 \
		-s WASM=0 \
		--shell-file $(SHELL_HTML) \
		--memory-init-file 0 \
		$(2) \
		2> >(grep -v "libxml2.so.2" >&2)
endef

# Copies the web output of game $(1) into the app template.
# index.data is removed first so a previous game's assets never ship by accident.
define COPY_TO_TEMPLATE
	rm -f $(TEMPLATE)/index.html $(TEMPLATE)/index.js $(TEMPLATE)/index.data
	cp $(1)/index.html $(1)/index.js $(TEMPLATE)/
	if [ -f $(1)/index.data ]; then cp $(1)/index.data $(TEMPLATE)/; fi
	@echo "Copied $(1) -> $(TEMPLATE)/"
endef

.PHONY: all build-girl build-arka native-girl native-arka run-girl run-arka clean

all: build-girl

build-girl:
	rm -f girlrunning/index.html girlrunning/index.js girlrunning/index.data
	$(call WEB_BUILD,girlrunning,--preload-file girlrunning/assets@/assets)
	$(call COPY_TO_TEMPLATE,girlrunning)

build-arka:
	rm -f arka/index.html arka/index.js arka/index.data
	$(call WEB_BUILD,arka,)
	$(call COPY_TO_TEMPLATE,arka)

native-girl:
	cd girlrunning && $(CC) main.c -o game $(CFLAGS) $(LDFLAGS)

native-arka:
	cd arka && $(CC) main.c -o game $(CFLAGS) $(LDFLAGS)

run-girl: native-girl
	cd girlrunning && ./game

run-arka: native-arka
	cd arka && ./game

clean:
	rm -f girlrunning/game girlrunning/index.html girlrunning/index.js girlrunning/index.data girlrunning/index.wasm
	rm -f arka/game arka/index.html arka/index.js arka/index.data arka/index.wasm