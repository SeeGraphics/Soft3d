CC := gcc
SDL_CFLAGS := $(shell sdl2-config --cflags)
SDL_LIBS := $(shell sdl2-config --libs)
SDL_IMAGE_CFLAGS := $(shell pkg-config --cflags SDL2_image 2>/dev/null || pkg-config --cflags sdl2_image 2>/dev/null)
SDL_IMAGE_LIBS := $(shell pkg-config --libs SDL2_image 2>/dev/null || pkg-config --libs sdl2_image 2>/dev/null)

ifeq ($(SDL_IMAGE_LIBS),)
SDL_IMAGE_LIBS := -lSDL2_image
endif

CFLAGS := -Wall -std=c17 -O2 -DNDEBUG -iquote src $(SDL_CFLAGS) $(SDL_IMAGE_CFLAGS)
LIBS := $(SDL_LIBS) $(SDL_IMAGE_LIBS)

SRC_DIR := src
BUILD_DIR := build
TARGET := game
MC_TARGET := mc
MODEL_TARGET := model
LIB_NAME := libsoft3d.a

SRCS := $(wildcard $(SRC_DIR)/*.c)
CORE_SRCS := $(filter-out $(SRC_DIR)/main.c,$(SRCS))
CORE_OBJS := $(patsubst $(SRC_DIR)/%.c,$(BUILD_DIR)/%.o,$(CORE_SRCS))
LIB_STATIC := $(BUILD_DIR)/$(LIB_NAME)

BIN := $(BUILD_DIR)/$(TARGET)
MC_SRCS := $(wildcard demo/mc*.c)
MC_BIN := $(BUILD_DIR)/$(MC_TARGET)
MODEL_SRCS := demo/model_demo.c
MODEL_BIN := $(BUILD_DIR)/$(MODEL_TARGET)

.PHONY: all run mc mc-run model model-run lib clean

all: $(BIN)

run: $(BIN)
	$(BIN)

mc: $(MC_BIN)

mc-run: $(MC_BIN)
	$(MC_BIN)

model: $(MODEL_BIN)

model-run: $(MODEL_BIN)
	$(MODEL_BIN)

lib: $(LIB_STATIC)

clean:
	rm -rf $(BUILD_DIR)

$(LIB_STATIC): $(CORE_OBJS) | $(BUILD_DIR)
	ar rcs $@ $^

$(BUILD_DIR)/%.o: $(SRC_DIR)/%.c | $(BUILD_DIR)
	$(CC) $(CFLAGS) -c $< -o $@

$(BIN): $(LIB_STATIC) $(SRC_DIR)/main.c | $(BUILD_DIR)
	$(CC) $(CFLAGS) $(SRC_DIR)/main.c $(LIB_STATIC) -o $(BIN) $(LIBS)

$(MC_BIN): $(LIB_STATIC) $(MC_SRCS) | $(BUILD_DIR)
	$(CC) $(CFLAGS) $(MC_SRCS) $(LIB_STATIC) -o $(MC_BIN) $(LIBS)

$(MODEL_BIN): $(LIB_STATIC) $(MODEL_SRCS) | $(BUILD_DIR)
	$(CC) $(CFLAGS) $(MODEL_SRCS) $(LIB_STATIC) -o $(MODEL_BIN) $(LIBS)

$(BUILD_DIR):
	mkdir -p $(BUILD_DIR)
