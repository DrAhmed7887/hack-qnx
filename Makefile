CC ?= cc
CSTD = -std=c11
WARN = -Wall -Wextra -Werror
OPT = -O2
CFLAGS ?= $(CSTD) $(OPT) $(WARN)
LDFLAGS ?=
LDLIBS = -lm

SRC_DIR = src
BUILD_DIR = build
PYTHON ?= $(shell [ -x .venv/bin/python ] && echo .venv/bin/python || echo python3)

CORE_SRCS = $(SRC_DIR)/qw_core.c $(SRC_DIR)/rules.c $(SRC_DIR)/qw_detector_config.c
CLI_SRCS = $(SRC_DIR)/qw_cli.c
ALL_SRCS = $(CORE_SRCS) $(CLI_SRCS)
OBJS = $(ALL_SRCS:$(SRC_DIR)/%.c=$(BUILD_DIR)/%.o)

TARGET = $(BUILD_DIR)/qw
RULES_DUMP_TARGET = $(BUILD_DIR)/qw_rules_dump
DETECTOR_CONFIG_DUMP_TARGET = $(BUILD_DIR)/qw_detector_config_dump
REENTRANT_TEST_TARGET = $(BUILD_DIR)/qw_reentrant_test
GATEWAY_TEST_TARGET = $(BUILD_DIR)/qw_gateway_test
QNX_DIR = $(SRC_DIR)/qnx
QNX_HDRS = $(QNX_DIR)/qw_ipc.h $(QNX_DIR)/qw_ring.h $(QNX_DIR)/qw_gateway_state.h $(QNX_DIR)/qw_log.h $(QNX_DIR)/qw_resmgr.h

.PHONY: all clean test qnx qnx-build qnx-check qnx-clean gateway-test reentrant-test fuzz eval eval-train eval-heldout eval-full demo-pack demo-verify convert

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

# Gateway subsystems (portable C11 under src/qnx/). Deliberately NOT linked
# into build/qw. Host test binary only; the QNX resource manager wraps these.
GATEWAY_OBJS = $(BUILD_DIR)/qw_ring.o $(BUILD_DIR)/qw_gateway_state.o $(BUILD_DIR)/qw_log.o $(BUILD_DIR)/qw_resmgr.o $(BUILD_DIR)/qw_gateway_test.o

$(BUILD_DIR)/%.o: $(QNX_DIR)/%.c $(QNX_HDRS) $(SRC_DIR)/qw_core.h $(SRC_DIR)/rules.h $(SRC_DIR)/qw_json_lexer.h
	@mkdir -p $(BUILD_DIR)
	$(CC) $(CFLAGS) -I$(SRC_DIR) -I$(QNX_DIR) -c -o $@ $<

$(GATEWAY_TEST_TARGET): $(GATEWAY_OBJS)
	$(CC) $(CFLAGS) $(LDFLAGS) -o $@ $(GATEWAY_OBJS) $(LDLIBS) -lpthread

$(BUILD_DIR)/%.o: $(SRC_DIR)/%.c $(SRC_DIR)/qw_core.h $(SRC_DIR)/rules.h $(SRC_DIR)/qw_detector_config.h $(SRC_DIR)/qw_json_lexer.h
	@mkdir -p $(BUILD_DIR)
	$(CC) $(CFLAGS) -I$(SRC_DIR) -c -o $@ $<

clean:
	rm -rf $(BUILD_DIR)

# ---- QNX 8.0 multi-process build (additive; never part of `make`, `make test`
# or build/qw). Cross-compile on x86_64 Linux with the SDP environment sourced
# (qnxsdp-env.sh). The six process sources under src/qnx/ compile to a host
# stub on other systems, so this section only matters on the QNX build host.
QCC ?= qcc
QNX_VARIANT ?= gcc_ntox86_64
QNX_BUILD_DIR = build/qnx
QNX_CFLAGS = -V$(QNX_VARIANT) -std=c11 -D_QNX_SOURCE -O2 -Wall -Wextra -Werror
QNX_LDLIBS = -lm -lslog2

QNX_OBJ_CORE = $(addprefix $(QNX_BUILD_DIR)/,qw_core.o rules.o qw_detector_config.o)
QNX_OBJ_RING = $(QNX_BUILD_DIR)/qw_ring.o
QNX_OBJ_STATE = $(QNX_BUILD_DIR)/qw_gateway_state.o
QNX_OBJ_LOG = $(QNX_BUILD_DIR)/qw_log.o
QNX_OBJ_RESMGR = $(QNX_BUILD_DIR)/qw_resmgr.o
QNX_BINS = $(addprefix $(QNX_BUILD_DIR)/,qw_acq qw_detector qw_alarm_mgr qw_failopen qw_supervisor qw_bedctl)

qnx-check:
	@command -v $(QCC) >/dev/null || { echo "qcc not found: source qnxsdp-env.sh (SDP 8.0 on x86_64 Linux)"; exit 1; }

# qnx-check runs first (recipe order), then the real build.
qnx: qnx-check
	@$(MAKE) --no-print-directory qnx-build

qnx-build: $(QNX_BINS)

$(QNX_BUILD_DIR)/%.o: $(QNX_DIR)/%.c $(QNX_HDRS) $(SRC_DIR)/qw_core.h $(SRC_DIR)/rules.h $(SRC_DIR)/qw_detector_config.h $(SRC_DIR)/qw_json_lexer.h
	@mkdir -p $(QNX_BUILD_DIR)
	$(QCC) $(QNX_CFLAGS) -I$(SRC_DIR) -I$(QNX_DIR) -c -o $@ $<

$(QNX_BUILD_DIR)/%.o: $(SRC_DIR)/%.c $(QNX_HDRS) $(SRC_DIR)/qw_core.h $(SRC_DIR)/rules.h $(SRC_DIR)/qw_detector_config.h $(SRC_DIR)/qw_json_lexer.h
	@mkdir -p $(QNX_BUILD_DIR)
	$(QCC) $(QNX_CFLAGS) -I$(SRC_DIR) -I$(QNX_DIR) -c -o $@ $<

$(QNX_BUILD_DIR)/qw_acq: $(QNX_BUILD_DIR)/qw_acq.o $(QNX_OBJ_CORE) $(QNX_OBJ_RING) $(QNX_OBJ_STATE) $(QNX_OBJ_LOG)
	$(QCC) $(QNX_CFLAGS) -o $@ $^ $(QNX_LDLIBS)

$(QNX_BUILD_DIR)/qw_detector: $(QNX_BUILD_DIR)/qw_detector.o $(QNX_OBJ_CORE) $(QNX_OBJ_RING) $(QNX_OBJ_LOG)
	$(QCC) $(QNX_CFLAGS) -o $@ $^ $(QNX_LDLIBS)

$(QNX_BUILD_DIR)/qw_alarm_mgr: $(QNX_BUILD_DIR)/qw_alarm_mgr.o $(QNX_OBJ_STATE) $(QNX_OBJ_LOG) $(QNX_OBJ_RESMGR)
	$(QCC) $(QNX_CFLAGS) -o $@ $^ $(QNX_LDLIBS)

$(QNX_BUILD_DIR)/qw_failopen: $(QNX_BUILD_DIR)/qw_failopen.o $(QNX_OBJ_STATE) $(QNX_OBJ_LOG)
	$(QCC) $(QNX_CFLAGS) -o $@ $^ $(QNX_LDLIBS)

$(QNX_BUILD_DIR)/qw_supervisor: $(QNX_BUILD_DIR)/qw_supervisor.o $(QNX_OBJ_LOG)
	$(QCC) $(QNX_CFLAGS) -o $@ $^ $(QNX_LDLIBS)

$(QNX_BUILD_DIR)/qw_bedctl: $(QNX_BUILD_DIR)/qw_bedctl.o
	$(QCC) $(QNX_CFLAGS) -o $@ $^ $(QNX_LDLIBS)

qnx-clean:
	rm -rf $(QNX_BUILD_DIR)

# Convert raw WFDB records in data/raw/training/ into data/bin/*.qw + labels.csv.
convert:
	$(PYTHON) scripts/convert.py

# Run the pytest suite (convert round-trip, rules reader, synthetic
# suppression behaviour). Builds qw first since some tests exercise the CLI.
# Also runs the standalone reentrancy C harness directly (not via pytest)
# since it has no Python bindings to call through.
test: $(TARGET) $(RULES_DUMP_TARGET) $(DETECTOR_CONFIG_DUMP_TARGET) $(REENTRANT_TEST_TARGET) $(GATEWAY_TEST_TARGET)
	$(PYTHON) -m pytest -q tests
	./$(REENTRANT_TEST_TARGET)
	./$(GATEWAY_TEST_TARGET)

gateway-test: $(GATEWAY_TEST_TARGET)
	./$(GATEWAY_TEST_TARGET)

reentrant-test: $(REENTRANT_TEST_TARGET)
	./$(REENTRANT_TEST_TARGET)

fuzz:
	$(PYTHON) scripts/fuzz_qw.py --count 1000

# Run build/qw over records in data/bin/ and write
# eval/results/{split}_{version}.json (see eval/run_eval.py --help).
eval: $(TARGET)
	$(PYTHON) eval/run_eval.py $(EVAL_ARGS)

eval-train: $(TARGET)
	$(PYTHON) eval/run_eval.py --split train $(EVAL_ARGS)

# Consumes one of the 3 allowed heldout runs (eval/heldout_runs.log).
eval-heldout: $(TARGET)
	$(PYTHON) eval/run_eval.py --split heldout $(EVAL_ARGS)

eval-full: $(TARGET)
	$(PYTHON) eval/run_eval.py --split full $(EVAL_ARGS)

# Build / verify the ODC-BY demo pack in data/demo/.
demo-pack:
	$(PYTHON) scripts/make_demo_pack.py

demo-verify:
	$(PYTHON) scripts/make_demo_pack.py --verify
