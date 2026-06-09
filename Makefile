CC ?= gcc
CXX ?= g++
CFLAGS ?= -std=c11 -O2 -Wall -Wextra -pedantic
CXXFLAGS ?= -std=c++17 -O2 -Wall -Wextra -pedantic
CPPFLAGS ?= -Iinclude
LDFLAGS ?= -lm
SYSTEMC_HOME ?= ../.ifc_systemc/systemc_sysroot/usr
SYSTEMC_LIB_DIR ?= $(SYSTEMC_HOME)/lib/x86_64-linux-gnu
SYSTEMC_CXXFLAGS ?= -I$(SYSTEMC_HOME)/include
SYSTEMC_LDFLAGS ?= -L$(SYSTEMC_LIB_DIR) -Wl,-rpath,$(abspath $(SYSTEMC_LIB_DIR)) -lsystemc -pthread

BIN_DIR := bin
BUILD_DIR := build
SIM_BIN := $(BIN_DIR)/ifc_cambricon_llm
TEST_BIN := $(BIN_DIR)/test_simulator
HW_CYCLE_BIN := $(BIN_DIR)/ifc_hw_cycle_model
SYSTEMC_CYCLE_BIN := $(BIN_DIR)/ifc_hw_cycle_systemc
SYSTEMC_COMPONENT_BIN := $(BIN_DIR)/ifc_component_systemc

CORE_OBJS := $(BUILD_DIR)/profiles.o $(BUILD_DIR)/config.o $(BUILD_DIR)/controller.o $(BUILD_DIR)/ssdsim_ifc.o $(BUILD_DIR)/simulator.o $(BUILD_DIR)/analysis.o $(BUILD_DIR)/plots.o
SIM_OBJS := $(CORE_OBJS) $(BUILD_DIR)/main.o
TEST_OBJS := $(CORE_OBJS) $(BUILD_DIR)/test_simulator.o

.PHONY: all run test hw-cycle systemc-cycle systemc-component systemc-full test-systemc test-systemc-component test-all clean

all: $(SIM_BIN)

run: $(SIM_BIN)
	$(SIM_BIN) --output-dir results

test: $(TEST_BIN)
	$(TEST_BIN)

hw-cycle: $(SIM_BIN) $(HW_CYCLE_BIN)
	$(SIM_BIN) --output-dir results
	$(HW_CYCLE_BIN) --platforms-csv configs/default_platforms.csv --trace results/hw_cycle_trace.csv --stats results/hw_cycle_stats.csv --compare results/hw_cycle_compare.csv --c-stats results/ssdsim_ifc_event_stats.csv

systemc-cycle: $(SIM_BIN) $(SYSTEMC_CYCLE_BIN)
	$(SIM_BIN) --output-dir results
	$(SYSTEMC_CYCLE_BIN) --platforms-csv configs/default_platforms.csv --trace results/systemc_cycle_trace.csv --stats results/systemc_cycle_stats.csv --compare results/systemc_cycle_compare.csv --c-stats results/ssdsim_ifc_event_stats.csv

systemc-component: $(SIM_BIN) $(SYSTEMC_COMPONENT_BIN)
	$(SIM_BIN) --output-dir results
	$(SYSTEMC_COMPONENT_BIN) --platforms-csv configs/default_platforms.csv --trace results/systemc_component_trace.csv --stats results/systemc_component_stats.csv --compare results/systemc_component_compare.csv --module-stats results/systemc_component_modules.csv --vcd results/systemc_component --c-stats results/ssdsim_ifc_event_stats.csv

systemc-full: hw-cycle systemc-cycle systemc-component

test-systemc: systemc-cycle
	test -s results/systemc_cycle_trace.csv
	test -s results/systemc_cycle_stats.csv
	test -s results/systemc_cycle_compare.csv
	awk -F, 'NR > 1 && $$5 != "PASS" { exit 1 } END { if (NR < 4) exit 1 }' results/systemc_cycle_compare.csv

test-systemc-component: systemc-component
	test -s results/systemc_component_trace.csv
	test -s results/systemc_component_stats.csv
	test -s results/systemc_component_compare.csv
	test -s results/systemc_component_modules.csv
	test -s results/systemc_component.vcd
	awk -F, 'NR > 1 && $$5 != "PASS" { exit 1 } END { if (NR < 4) exit 1 }' results/systemc_component_compare.csv

test-all: test test-systemc test-systemc-component

$(SIM_BIN): $(SIM_OBJS) | $(BIN_DIR)
	$(CC) $(CFLAGS) -o $@ $(SIM_OBJS) $(LDFLAGS)

$(TEST_BIN): $(TEST_OBJS) | $(BIN_DIR)
	$(CC) $(CFLAGS) -o $@ $(TEST_OBJS) $(LDFLAGS)

$(HW_CYCLE_BIN): systemc/ifc_hw_cycle_model.cpp | $(BIN_DIR)
	$(CXX) $(CXXFLAGS) -o $@ systemc/ifc_hw_cycle_model.cpp

$(SYSTEMC_CYCLE_BIN): systemc/ifc_hw_cycle_systemc.cpp systemc/ifc_hw_cycle_model.cpp | $(BIN_DIR)
	$(CXX) $(CXXFLAGS) $(SYSTEMC_CXXFLAGS) -o $@ systemc/ifc_hw_cycle_systemc.cpp $(SYSTEMC_LDFLAGS)

$(SYSTEMC_COMPONENT_BIN): systemc/ifc_component_systemc.cpp systemc/ifc_hw_cycle_model.cpp | $(BIN_DIR)
	$(CXX) $(CXXFLAGS) $(SYSTEMC_CXXFLAGS) -o $@ systemc/ifc_component_systemc.cpp $(SYSTEMC_LDFLAGS)

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
