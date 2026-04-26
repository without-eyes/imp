CC = gcc
CXX = g++

CFLAGS = -Wall -Wextra -Wpedantic -Werror -g -Iinclude -Isrc -Imodules
CXXFLAGS = -Wall -Wextra -g -Iinclude -Isrc -Imodules

LDFLAGS = -ldl -rdynamic
MOD_CFLAGS = -fPIC -shared
TEST_LDFLAGS = -lgtest -lgtest_main -pthread

BUILD_DIR = build
SRC_DIR = src
MOD_DIR = modules
TEST_DIR = tests

CORE_SRCS = $(shell find $(SRC_DIR) -type f -name '*.c')
CORE_OBJS = $(patsubst $(SRC_DIR)/%.c, $(BUILD_DIR)/obj/%.o, $(CORE_SRCS))
CORE_BIN = $(BUILD_DIR)/imp

MODULE_NAMES = memory security prioritizer
MODULE_BINS = $(patsubst %, $(BUILD_DIR)/modules/imp_%.so, $(MODULE_NAMES))

TEST_SRCS = $(shell find $(TEST_DIR) -type f -name '*.cpp')
TEST_BIN = $(BUILD_DIR)/test_runner

TEST_EXCLUDE_OBJS = $(BUILD_DIR)/obj/core/main.o \
                    $(BUILD_DIR)/obj/cli/dashboard.o \
                    $(BUILD_DIR)/obj/utils/logger.o \
                    $(BUILD_DIR)/obj/utils/ipc.o \
                    $(BUILD_DIR)/obj/core/imp.o

TEST_OBJS = $(filter-out $(TEST_EXCLUDE_OBJS), $(CORE_OBJS))

.PHONY: all clean test coverage

all: $(CORE_BIN) $(MODULE_BINS)

$(BUILD_DIR)/obj/%.o: $(SRC_DIR)/%.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -c $< -o $@

$(CORE_BIN): $(CORE_OBJS)
	@mkdir -p $(dir $@)
	$(CC) $(CORE_OBJS) -o $@ $(LDFLAGS)

$(BUILD_DIR)/modules/imp_%.so: $(MOD_DIR)/%.c
	@mkdir -p $(dir $@) 
	$(CC) $(CFLAGS) $(MOD_CFLAGS) $< -o $@

test: $(TEST_BIN)
	./$(TEST_BIN)

$(TEST_BIN): $(TEST_SRCS) $(CORE_OBJS)
	@mkdir -p $(dir $@)
	$(CXX) $(CXXFLAGS) $(TEST_SRCS) $(TEST_OBJS) $(TEST_LDFLAGS) -o $@

clean:
	rm -rf $(BUILD_DIR)

coverage: CFLAGS += --coverage
coverage: CXXFLAGS += --coverage
coverage: TEST_LDFLAGS += --coverage
coverage: clean test
	@mkdir -p $(BUILD_DIR)/coverage
	
	lcov --capture --directory . \
		--ignore-errors mismatch \
		--rc geninfo_unexecuted_blocks=1 \
		--output-file $(BUILD_DIR)/coverage/coverage.info
	
	lcov --ignore-errors unused,empty,mismatch --remove $(BUILD_DIR)/coverage/coverage.info \
		'/usr/*' \
		'*/tests/*' \
		'*/utils/cJSON.c' \
		--output-file $(BUILD_DIR)/coverage/coverage_filtered.info
		
	genhtml $(BUILD_DIR)/coverage/coverage_filtered.info --output-directory $(BUILD_DIR)/coverage/html
	@echo "======================================================================"
	@echo "Report: xdg-open $(BUILD_DIR)/coverage/html/index.html"