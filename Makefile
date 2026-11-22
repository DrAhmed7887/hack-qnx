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

CORE_SRCS = $(SRC_DIR)/qw_core.c $(SRC_DIR)/rules.c
CLI_SRCS = $(SRC_DIR)/qw_cli.c
ALL_SRCS = $(CORE_SRCS) $(CLI_SRCS)
OBJS = $(ALL_SRCS:$(SRC_DIR)/%.c=$(BUILD_DIR)/%.o)

TARGET = $(BUILD_DIR)/qw
RULES_DUMP_TARGET = $(BUILD_DIR)/qw_rules_dump

.PHONY: all clean test eval convert

all: $(TARGET)

$(TARGET): $(OBJS)
	$(CC) $(CFLAGS) $(LDFLAGS) -o $@ $(OBJS) $(LDLIBS)

# Test-only helper binary that exercises the real rules.json reader
# (src/rules.c) and prints its parsed fields; used by tests/test_rules.py.
# Not part of the shipped qw CLI.
$(RULES_DUMP_TARGET): $(BUILD_DIR)/rules.o $(BUILD_DIR)/qw_rules_dump.o
	$(CC) $(CFLAGS) $(LDFLAGS) -o $@ $(BUILD_DIR)/rules.o $(BUILD_DIR)/qw_rules_dump.o $(LDLIBS)

$(BUILD_DIR)/%.o: $(SRC_DIR)/%.c $(SRC_DIR)/qw_core.h $(SRC_DIR)/rules.h
	@mkdir -p $(BUILD_DIR)
	$(CC) $(CFLAGS) -I$(SRC_DIR) -c -o $@ $<

clean:
	rm -rf $(BUILD_DIR)

# Convert raw WFDB records in data/raw/training/ into data/bin/*.qw + labels.csv.
convert:
	$(PYTHON) scripts/convert.py

# Run the pytest suite (convert round-trip, rules reader, synthetic
# suppression behaviour). Builds qw first since some tests exercise the CLI.
test: $(TARGET) $(RULES_DUMP_TARGET)
	$(PYTHON) -m pytest -q tests

# Run build/qw over every record in data/bin/ and write
# eval/results/train_v0.json.
eval: $(TARGET)
	$(PYTHON) eval/run_eval.py
