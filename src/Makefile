PYTHON ?= python3
CXX ?= g++
CXXFLAGS ?= -std=c++17 -Wall -Wextra -pedantic
NVCC ?= nvcc
NVCCFLAGS ?= -std=c++17 -O2

SCENARIO ?= normal_traffic
LINK_BPS ?= 40000000000
BUFFER_BYTES ?= 262144

BUILD_DIR ?= build
TRACE_DIR ?= data/traces
CPU_FIFO_BIN := $(BUILD_DIR)/cpu_fifo_sim
CPU_FIFO_SRCS := sim/cpu_fifo/main.cpp sim/cpu_fifo/engine.cpp sim/cpu_fifo/trace_csv.cpp
CPU_PRIORITY_BIN := $(BUILD_DIR)/cpu_priority_queue_sim
CPU_PRIORITY_SRCS := sim/cpu_priority_queue/main.cpp sim/cpu_priority_queue/engine.cpp sim/cpu_fifo/trace_csv.cpp
GPU_PRIORITY_BIN := $(BUILD_DIR)/gpu_priority_queue_sim
GPU_PRIORITY_SRCS := sim/gpu_priority_queue/gpu_sorter.cu \
                     sim/gpu_priority_queue/engine.cpp \
                     sim/gpu_priority_queue/main.cpp \
                     sim/cpu_fifo/trace_csv.cpp

.PHONY: tmp-install build-cpu-fifo build-cpu-priority-queue build-gpu-priority-queue \
        generate-trace run-cpu-fifo run-cpu-priority-queue run-gpu-priority-queue run-all clean

tmp-install:
	$(PYTHON) -m pip install -e .[dev]

build-cpu-fifo: $(CPU_FIFO_BIN)

build-cpu-priority-queue: $(CPU_PRIORITY_BIN)

build-gpu-priority-queue: $(GPU_PRIORITY_BIN)

$(CPU_FIFO_BIN): $(CPU_FIFO_SRCS)
	mkdir -p $(BUILD_DIR)
	$(CXX) $(CXXFLAGS) $(CPU_FIFO_SRCS) -o $(CPU_FIFO_BIN)

$(CPU_PRIORITY_BIN): $(CPU_PRIORITY_SRCS)
	mkdir -p $(BUILD_DIR)
	$(CXX) $(CXXFLAGS) $(CPU_PRIORITY_SRCS) -o $(CPU_PRIORITY_BIN)

$(GPU_PRIORITY_BIN): $(GPU_PRIORITY_SRCS)
	mkdir -p $(BUILD_DIR)
	$(NVCC) $(NVCCFLAGS) $(GPU_PRIORITY_SRCS) -o $(GPU_PRIORITY_BIN)

generate-trace:
	PYTHONPATH=. TRACE_DIR=$(TRACE_DIR) SCENARIO=$(SCENARIO) $(PYTHON) -c 'from pathlib import Path; import os; from traffic.config import ScenarioName, get_scenario; from traffic.io.csv_export import generate_and_export_csv; scenario = ScenarioName(os.environ["SCENARIO"]); output_path = generate_and_export_csv(config=get_scenario(scenario), scenario_name=scenario, output_dir=Path(os.environ["TRACE_DIR"])); print(output_path)'

run-cpu-fifo: build-cpu-fifo
	TRACE_PATH="$$(PYTHONPATH=. TRACE_DIR=$(TRACE_DIR) SCENARIO=$(SCENARIO) $(PYTHON) -c 'from pathlib import Path; import os; from traffic.config import ScenarioName, get_scenario; from traffic.io.csv_export import build_trace_path; scenario = ScenarioName(os.environ["SCENARIO"]); print(build_trace_path(config=get_scenario(scenario), scenario_name=scenario, output_dir=Path(os.environ["TRACE_DIR"])))')"; \
	if [ ! -f "$$TRACE_PATH" ]; then \
		$(MAKE) generate-trace SCENARIO=$(SCENARIO) TRACE_DIR=$(TRACE_DIR); \
	fi; \
	$(CPU_FIFO_BIN) --input "$$TRACE_PATH" --link-bps $(LINK_BPS) --buffer-bytes $(BUFFER_BYTES)

run-cpu-priority-queue: build-cpu-priority-queue
	TRACE_PATH="$$(PYTHONPATH=. TRACE_DIR=$(TRACE_DIR) SCENARIO=$(SCENARIO) $(PYTHON) -c 'from pathlib import Path; import os; from traffic.config import ScenarioName, get_scenario; from traffic.io.csv_export import build_trace_path; scenario = ScenarioName(os.environ["SCENARIO"]); print(build_trace_path(config=get_scenario(scenario), scenario_name=scenario, output_dir=Path(os.environ["TRACE_DIR"])))')"; \
	if [ ! -f "$$TRACE_PATH" ]; then \
		$(MAKE) generate-trace SCENARIO=$(SCENARIO) TRACE_DIR=$(TRACE_DIR); \
	fi; \
	$(CPU_PRIORITY_BIN) --input "$$TRACE_PATH" --link-bps $(LINK_BPS) --buffer-bytes $(BUFFER_BYTES)

run-gpu-priority-queue: build-gpu-priority-queue
	TRACE_PATH="$$(PYTHONPATH=. TRACE_DIR=$(TRACE_DIR) SCENARIO=$(SCENARIO) $(PYTHON) -c 'from pathlib import Path; import os; from traffic.config import ScenarioName, get_scenario; from traffic.io.csv_export import build_trace_path; scenario = ScenarioName(os.environ["SCENARIO"]); print(build_trace_path(config=get_scenario(scenario), scenario_name=scenario, output_dir=Path(os.environ["TRACE_DIR"])))')"; \
	if [ ! -f "$$TRACE_PATH" ]; then \
		$(MAKE) generate-trace SCENARIO=$(SCENARIO) TRACE_DIR=$(TRACE_DIR); \
	fi; \
	$(GPU_PRIORITY_BIN) --input "$$TRACE_PATH" --link-bps $(LINK_BPS) --buffer-bytes $(BUFFER_BYTES)

OUTPUT_FILE ?= results.xlsx

run-all:
	PYTHONPATH=. SCENARIO=$(SCENARIO) LINK_BPS=$(LINK_BPS) BUFFER_BYTES=$(BUFFER_BYTES) TRACE_DIR=$(TRACE_DIR) OUTPUT_FILE=$(OUTPUT_FILE) $(PYTHON) scripts/run_all_and_export.py

clean:
	rm -rf $(BUILD_DIR)
