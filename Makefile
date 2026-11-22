CC ?= cc
CSTD = -std=c11
WARN = -Wall -Wextra -Werror
OPT = -O2
CFLAGS ?= $(CSTD) $(OPT) $(WARN)
LDFLAGS ?=
LDLIBS = -lm

SRC_DIR = src
BUILD_DIR = build
PYTHON = .venv/bin/python

CORE_SRCS = $(SRC_DIR)/qw_core.c $(SRC_DIR)/rules.c $(SRC_DIR)/qw_detector_config.c
CLI_SRCS = $(SRC_DIR)/qw_cli.c
ALL_SRCS = $(CORE_SRCS) $(CLI_SRCS)
OBJS = $(ALL_SRCS:$(SRC_DIR)/%.c=$(BUILD_DIR)/%.o)

TARGET = $(BUILD_DIR)/qw
RULES_DUMP_TARGET = $(BUILD_DIR)/qw_rules_dump
DETECTOR_CONFIG_DUMP_TARGET = $(BUILD_DIR)/qw_detector_config_dump
REENTRANT_TEST_TARGET = $(BUILD_DIR)/qw_reentrant_test

.PHONY: all clean test eval convert

all: $(TARGET)

$(TARGET): $(OBJS)
	$(CC) $(CFLAGS) $(LDFLAGS) -o $@ $(OBJS) $(LDLIBS)

# Test-only helper binary that exercises the real rules.json reader
# (src/rules.c) and prints its parsed fields; used by tests/test_rules.py.
# Not part of the shipped qw CLI.
$(RULES_DUMP_TARGET): $(BUILD_DIR)/rules.o $(BUILD_DIR)/qw_rules_dump.o
	$(CC) $(CFLAGS) $(LDFLAGS) -o $@ $(BUILD_DIR)/rules.o $(BUILD_DIR)/qw_rules_dump.o $(LDLIBS)

# Test-only helper binary that exercises the real detector.json reader
# (src/qw_detector_config.c); used by tests/test_detector_config.py. Not
# part of the shipped qw CLI.
$(DETECTOR_CONFIG_DUMP_TARGET): $(BUILD_DIR)/qw_detector_config.o $(BUILD_DIR)/qw_detector_config_dump.o
	$(CC) $(CFLAGS) $(LDFLAGS) -o $@ $(BUILD_DIR)/qw_detector_config.o $(BUILD_DIR)/qw_detector_config_dump.o $(LDLIBS)

# Test-only harness for review finding #5 (qw_decide() reentrancy): runs
# many concurrent qw_decide() calls across pthreads, each with its own
# qw_decide_workspace_t, and fails if any call's result is inconsistent
# with its own record (which would indicate shared/static state bleeding
# across calls). Not part of the shipped qw CLI.
$(REENTRANT_TEST_TARGET): $(BUILD_DIR)/qw_core.o $(BUILD_DIR)/rules.o $(BUILD_DIR)/qw_detector_config.o $(BUILD_DIR)/qw_reentrant_test.o
	$(CC) $(CFLAGS) $(LDFLAGS) -o $@ $(BUILD_DIR)/qw_core.o $(BUILD_DIR)/rules.o $(BUILD_DIR)/qw_detector_config.o $(BUILD_DIR)/qw_reentrant_test.o $(LDLIBS) -lpthread

$(BUILD_DIR)/%.o: $(SRC_DIR)/%.c $(SRC_DIR)/qw_core.h $(SRC_DIR)/rules.h $(SRC_DIR)/qw_detector_config.h $(SRC_DIR)/qw_json_lexer.h
	@mkdir -p $(BUILD_DIR)
	$(CC) $(CFLAGS) -I$(SRC_DIR) -c -o $@ $<

clean:
	rm -rf $(BUILD_DIR)

# Convert raw WFDB records in data/raw/training/ into data/bin/*.qw + labels.csv.
convert:
	$(PYTHON) scripts/convert.py

# Run the pytest suite (convert round-trip, rules reader, synthetic
# suppression behaviour). Builds qw first since some tests exercise the CLI.
# Also runs the standalone reentrancy C harness directly (not via pytest)
# since it has no Python bindings to call through.
test: $(TARGET) $(RULES_DUMP_TARGET) $(DETECTOR_CONFIG_DUMP_TARGET) $(REENTRANT_TEST_TARGET)
	$(PYTHON) -m pytest -q tests
	./$(REENTRANT_TEST_TARGET)

# Run build/qw over every record in data/bin/ and write
# eval/results/train_v0.json.
eval: $(TARGET)
	$(PYTHON) eval/run_eval.py
