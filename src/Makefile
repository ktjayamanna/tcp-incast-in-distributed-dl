PYTHON   ?= python3
CXX      ?= g++
CXXFLAGS ?= -std=c++17 -Wall -Wextra -pedantic
NVCC     ?= nvcc
NVCCFLAGS ?= -std=c++17 -O2

SCENARIO  ?= high
LINK_BPS  ?= 40000000000

BUFFER_BYTES ?= 52428800

BUILD_DIR        := build
TRACE_DIR        := data/traces

CPU_FIFO_BIN     := $(BUILD_DIR)/cpu_fifo_sim
CPU_FIFO_SRCS    := sim/cpu_fifo/main.cpp sim/cpu_fifo/engine.cpp sim/cpu_fifo/trace_csv.cpp

CPU_PQ_BIN       := $(BUILD_DIR)/cpu_priority_queue_sim
CPU_PQ_SRCS      := sim/cpu_priority_queue/main.cpp sim/cpu_priority_queue/engine.cpp \
                    sim/cpu_fifo/trace_csv.cpp

GPU_PQ_BIN       := $(BUILD_DIR)/gpu_priority_queue_sim
GPU_PQ_SRCS      := sim/gpu_priority_queue/gpu_sorter.cu sim/gpu_priority_queue/engine.cpp \
                    sim/gpu_priority_queue/main.cpp sim/cpu_fifo/trace_csv.cpp

.PHONY: install build run-all clean

install:
	$(PYTHON) -m pip install -e .[dev]

# ---- build targets -----------------------------------------------------------

$(CPU_FIFO_BIN): $(CPU_FIFO_SRCS)
	mkdir -p $(BUILD_DIR)
	$(CXX) $(CXXFLAGS) $^ -o $@

$(CPU_PQ_BIN): $(CPU_PQ_SRCS)
	mkdir -p $(BUILD_DIR)
	$(CXX) $(CXXFLAGS) $^ -o $@

$(GPU_PQ_BIN): $(GPU_PQ_SRCS)
	mkdir -p $(BUILD_DIR)
	$(NVCC) $(NVCCFLAGS) $^ -o $@

build: $(CPU_FIFO_BIN) $(CPU_PQ_BIN) $(GPU_PQ_BIN)

# ---- run targets -------------------------------------------------------------

TRACE_PATH := $(shell PYTHONPATH=. TRACE_DIR=$(TRACE_DIR) SCENARIO=$(SCENARIO) \
	$(PYTHON) -c 'from pathlib import Path; import os; \
	from traffic.config import ScenarioName, get_scenario; \
	from traffic.io.csv_export import build_trace_path; \
	s = ScenarioName(os.environ["SCENARIO"]); \
	print(build_trace_path(config=get_scenario(s), scenario_name=s, output_dir=Path(os.environ["TRACE_DIR"])))')

$(TRACE_PATH):
	PYTHONPATH=. TRACE_DIR=$(TRACE_DIR) SCENARIO=$(SCENARIO) \
	$(PYTHON) -c 'from pathlib import Path; import os; \
	from traffic.config import ScenarioName, get_scenario; \
	from traffic.io.csv_export import generate_and_export_csv; \
	s = ScenarioName(os.environ["SCENARIO"]); \
	generate_and_export_csv(config=get_scenario(s), scenario_name=s, output_dir=Path(os.environ["TRACE_DIR"]))'

run-cpu-fifo: $(CPU_FIFO_BIN) $(TRACE_PATH)
	$(CPU_FIFO_BIN) --input "$(TRACE_PATH)" --link-bps $(LINK_BPS) --buffer-bytes $(BUFFER_BYTES)

run-cpu-priority-queue: $(CPU_PQ_BIN) $(TRACE_PATH)
	$(CPU_PQ_BIN) --input "$(TRACE_PATH)" --link-bps $(LINK_BPS) --buffer-bytes $(BUFFER_BYTES)

run-gpu-priority-queue: $(GPU_PQ_BIN) $(TRACE_PATH)
	$(GPU_PQ_BIN) --input "$(TRACE_PATH)" --link-bps $(LINK_BPS) --buffer-bytes $(BUFFER_BYTES)

run-all: build
	PYTHONPATH=. SCENARIO=$(SCENARIO) LINK_BPS=$(LINK_BPS) BUFFER_BYTES=$(BUFFER_BYTES) \
	TRACE_DIR=$(TRACE_DIR) OUTPUT_FILE=results.xlsx \
	$(PYTHON) scripts/run_all_and_export.py

clean:
	rm -rf $(BUILD_DIR)
