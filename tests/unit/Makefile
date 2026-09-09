# Standalone host build for the pure-core unit tests.
#
# Plain `cc`, NO Zephyr — this IS the CI rail for the pure core (a gcc job runs
# `make` here; see .github/workflows/host-tests.yml) as well as the sub-second
# local loop. It doubles as a decoupling proof: if a pure module stops compiling
# without Zephyr, `make` breaks here. (run-host.ps1 is the equivalent MSVC loop.)
#
# The auto-overwrite policy (dm_config.h: DM_AUTO_OVERWRITE_RAM/_NVS) and the
# per-class NVS cap (dm_config.h: MAX_EVENTS_NVS) are compile-time switches, so
# the rails recompile the SAME sources with them flipped:
#   dm_unit_host          both policies off, cap == MAX_EVENTS (default Kconfig)
#   dm_unit_host_ow_ram   RAM overwrite on, NVS off (pins the per-class split)
#   dm_unit_host_ow_both  both on (pins the NVS-class path)
#   dm_unit_host_nvs_cap  MAX_EVENTS_NVS < MAX_EVENTS (pins the per-class cap
#                         rejection paths in slot_store and dm_machine)
# test_machine.c #if's select each rail's expectations from its policy.
#
# Usage (from tests/unit/):
#   make          # build + run all rails
#   make run      # alias
#   make clean

CC      ?= cc
REPO    := ../..
CFLAGS  := -std=c11 -Wall -Wextra -Werror -g -O0 \
           -DZTEST_SHIM_HOST \
           -I. -I$(REPO)/include

# Every test_*.c is a unit-test translation unit. host_runner.c provides main()
# and the registry storage.
TEST_SRCS := $(wildcard test_*.c)

# Pure-core module sources under test, compiled directly (Zephyr-free). Add each
# pure module's .c here as it is added (mirrors run-host.ps1).
MODULE_SRCS := $(REPO)/src/dm_render.c $(REPO)/src/slot_store.c $(REPO)/src/dm_machine.c \
               $(REPO)/src/dm_feedback_build.c $(REPO)/src/dm_query.c

SRCS      := host_runner.c $(TEST_SRCS) $(MODULE_SRCS)

BIN        := dm_unit_host
BIN_OW_RAM := dm_unit_host_ow_ram
BIN_OW_BOTH:= dm_unit_host_ow_both
BIN_NVS_CAP:= dm_unit_host_nvs_cap
BINS       := $(BIN) $(BIN_OW_RAM) $(BIN_OW_BOTH) $(BIN_NVS_CAP)

CFLAGS_OW_RAM  := $(CFLAGS) -DDM_AUTO_OVERWRITE_RAM=1
CFLAGS_OW_BOTH := $(CFLAGS) -DDM_AUTO_OVERWRITE_RAM=1 -DDM_AUTO_OVERWRITE_NVS=1
CFLAGS_NVS_CAP := $(CFLAGS) -DMAX_EVENTS_NVS=8

.PHONY: all run clean
all: run

$(BIN): $(SRCS) ztest_shim.h
	$(CC) $(CFLAGS) -o $(BIN) $(SRCS)

$(BIN_OW_RAM): $(SRCS) ztest_shim.h
	$(CC) $(CFLAGS_OW_RAM) -o $(BIN_OW_RAM) $(SRCS)

$(BIN_OW_BOTH): $(SRCS) ztest_shim.h
	$(CC) $(CFLAGS_OW_BOTH) -o $(BIN_OW_BOTH) $(SRCS)

$(BIN_NVS_CAP): $(SRCS) ztest_shim.h
	$(CC) $(CFLAGS_NVS_CAP) -o $(BIN_NVS_CAP) $(SRCS)

run: $(BINS)
	./$(BIN)
	./$(BIN_OW_RAM)
	./$(BIN_OW_BOTH)
	./$(BIN_NVS_CAP)

clean:
	rm -f $(BINS)
