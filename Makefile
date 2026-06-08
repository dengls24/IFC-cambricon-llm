CC ?= gcc
CXX ?= g++
CFLAGS ?= -std=c11 -O2 -Wall -Wextra -pedantic
CXXFLAGS ?= -std=c++17 -O2 -Wall -Wextra -pedantic
CPPFLAGS ?= -Iinclude
LDFLAGS ?= -lm

BIN_DIR := bin
BUILD_DIR := build
SIM_BIN := $(BIN_DIR)/ifc_cambricon_llm
TEST_BIN := $(BIN_DIR)/test_simulator
HW_CYCLE_BIN := $(BIN_DIR)/ifc_hw_cycle_model

CORE_OBJS := $(BUILD_DIR)/profiles.o $(BUILD_DIR)/config.o $(BUILD_DIR)/controller.o $(BUILD_DIR)/ssdsim_ifc.o $(BUILD_DIR)/simulator.o $(BUILD_DIR)/analysis.o $(BUILD_DIR)/plots.o
SIM_OBJS := $(CORE_OBJS) $(BUILD_DIR)/main.o
TEST_OBJS := $(CORE_OBJS) $(BUILD_DIR)/test_simulator.o

.PHONY: all run test hw-cycle clean

all: $(SIM_BIN)

run: $(SIM_BIN)
	$(SIM_BIN) --output-dir results

test: $(TEST_BIN)
	$(TEST_BIN)

hw-cycle: $(SIM_BIN) $(HW_CYCLE_BIN)
	$(SIM_BIN) --output-dir results
	$(HW_CYCLE_BIN) --platforms-csv configs/default_platforms.csv --trace results/hw_cycle_trace.csv --stats results/hw_cycle_stats.csv --compare results/hw_cycle_compare.csv --c-stats results/ssdsim_ifc_event_stats.csv

$(SIM_BIN): $(SIM_OBJS) | $(BIN_DIR)
	$(CC) $(CFLAGS) -o $@ $(SIM_OBJS) $(LDFLAGS)

$(TEST_BIN): $(TEST_OBJS) | $(BIN_DIR)
	$(CC) $(CFLAGS) -o $@ $(TEST_OBJS) $(LDFLAGS)

$(HW_CYCLE_BIN): systemc/ifc_hw_cycle_model.cpp | $(BIN_DIR)
	$(CXX) $(CXXFLAGS) -o $@ systemc/ifc_hw_cycle_model.cpp

$(BUILD_DIR)/%.o: src/%.c include/ifc_cambricon_llm.h | $(BUILD_DIR)
	$(CC) $(CPPFLAGS) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/test_simulator.o: tests/test_simulator.c include/ifc_cambricon_llm.h | $(BUILD_DIR)
	$(CC) $(CPPFLAGS) $(CFLAGS) -c $< -o $@

$(BIN_DIR):
	mkdir -p $@

$(BUILD_DIR):
	mkdir -p $@

clean:
	rm -rf $(BIN_DIR) $(BUILD_DIR)
