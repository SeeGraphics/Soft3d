CC := gcc

SDL_CFLAGS := $(shell sdl2-config --cflags)
SDL_LIBS := $(shell sdl2-config --libs)
SDL_IMAGE_LIBS := $(shell pkg-config --libs SDL2_image 2>/dev/null || pkg-config --libs sdl2_image 2>/dev/null)

S3D_ROOT := soft3d
S3D_INC := -I$(S3D_ROOT)/src
S3D_LIB := $(S3D_ROOT)/build/libsoft3d.a

CFLAGS := -Wall -std=c17 -O2 -DNDEBUG $(S3D_INC) $(SDL_CFLAGS)
LIBS := $(S3D_LIB) $(SDL_LIBS) $(SDL_IMAGE_LIBS)

BUILD := build
SRCS := $(wildcard src/*.c)
BIN := $(BUILD)/game

.PHONY: all run clean

all: $(BIN)

run: $(BIN)
	$(BIN)

$(BIN): $(SRCS) $(S3D_LIB) | $(BUILD)
	$(CC) $(CFLAGS) $(SRCS) -o $@ $(LIBS)

$(S3D_LIB):
	$(MAKE) -C $(S3D_ROOT) lib

$(BUILD):
	mkdir -p $(BUILD)

clean:
	rm -rf $(BUILD)
