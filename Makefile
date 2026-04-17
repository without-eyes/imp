CC = gcc
CFLAGS = -Wall -Wextra -Wpedantic -Werror -g -Iinclude
LDFLAGS = -ldl -rdynamic
MOD_CFLAGS = -fPIC -shared

BUILD_DIR = build
SRC_DIR = src
MOD_DIR = modules

CORE_SRCS = $(shell find $(SRC_DIR) -type f -name '*.c')
CORE_OBJS = $(patsubst $(SRC_DIR)/%.c, $(BUILD_DIR)/obj/%.o, $(CORE_SRCS))
CORE_BIN = $(BUILD_DIR)/imp

MODULE_NAMES = memory security prioritizer #interface
MODULE_BINS = $(patsubst %, $(BUILD_DIR)/modules/imp_%.so, $(MODULE_NAMES))

.PHONY: all clean

all: $(CORE_BIN) $(MODULE_BINS)

$(BUILD_DIR)/obj/%.o: $(SRC_DIR)/%.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -c $< -o $@

$(CORE_BIN): $(CORE_OBJS)
	$(CC) $(CORE_OBJS) -o $@ $(LDFLAGS)

$(BUILD_DIR)/modules/imp_%.so: $(MOD_DIR)/%.c
	@mkdir -p $(dir $@) 
	$(CC) $(CFLAGS) $(MOD_CFLAGS) $< -o $@

clean:
	rm -rf $(BUILD_DIR)