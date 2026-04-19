PYTHON   ?= python3
CXX      ?= g++
CXXFLAGS ?= -std=c++17 -Wall -Wextra -pedantic
NVCC     ?= nvcc
NVCCFLAGS ?= -std=c++17 -O2

SCENARIO  ?= high
LINK_BPS  ?= 40000000000

BUFFER_BYTES ?= 52428800

# Quick-compare settings.
# medium gives ~10K packets/wave — large enough that CPU std::sort > 20µs
# (blind for the whole wave) while GPU kernel < 20µs (can still evict).
# Override: make compare COMPARE_SCENARIO=high
COMPARE_SCENARIO     ?= medium
COMPARE_BUFFER_BYTES ?= 52428800

# Sort interval: how often the queue is re-sorted (µs of simulation time).
# Blind window is now auto-measured from actual sort execution time —
# std::sort wall time for CPU, GPU kernel time for GPU.
# Pass --sort-latency-us N to override with a fixed value.
SORT_INTERVAL_US     ?= 9000

BUILD_DIR        := build
TRACE_DIR        := data/traces

SOCKET_PORT ?= 9000

CPU_FIFO_BIN     := $(BUILD_DIR)/cpu_fifo_sim
CPU_FIFO_SRCS    := sim/cpu_fifo/main.cpp sim/cpu_fifo/engine.cpp sim/cpu_fifo/trace_csv.cpp \
                    sim/cpu_fifo/socket_source.cpp

CPU_PQ_BIN       := $(BUILD_DIR)/cpu_priority_queue_sim
CPU_PQ_SRCS      := sim/cpu_priority_queue/main.cpp sim/cpu_priority_queue/engine.cpp \
                    sim/cpu_fifo/trace_csv.cpp sim/cpu_fifo/socket_source.cpp

GPU_PQ_BIN       := $(BUILD_DIR)/gpu_priority_queue_sim
GPU_PQ_SRCS      := sim/gpu_priority_queue/gpu_sorter.cu sim/gpu_priority_queue/engine.cpp \
                    sim/gpu_priority_queue/main.cpp sim/cpu_fifo/trace_csv.cpp \
                    sim/cpu_fifo/socket_source.cpp

.PHONY: install build run-all compare \
	run-cpu-fifo-socket run-cpu-priority-queue-socket run-gpu-priority-queue-socket \
	run-cpu-fifo-ns3 run-cpu-priority-queue-ns3 run-gpu-priority-queue-ns3 \
	clean

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
	$(CPU_PQ_BIN) --input "$(TRACE_PATH)" --link-bps $(LINK_BPS) --buffer-bytes $(BUFFER_BYTES) \
	  --sort-interval-us $(SORT_INTERVAL_US)

run-gpu-priority-queue: $(GPU_PQ_BIN) $(TRACE_PATH)
	$(GPU_PQ_BIN) --input "$(TRACE_PATH)" --link-bps $(LINK_BPS) --buffer-bytes $(BUFFER_BYTES) \
	  --sort-interval-us $(SORT_INTERVAL_US)

run-cpu-fifo-socket: $(CPU_FIFO_BIN)
	$(CPU_FIFO_BIN) --socket $(SOCKET_PORT) --link-bps $(LINK_BPS) --buffer-bytes $(BUFFER_BYTES)

run-cpu-priority-queue-socket: $(CPU_PQ_BIN)
	$(CPU_PQ_BIN) --socket $(SOCKET_PORT) --link-bps $(LINK_BPS) --buffer-bytes $(BUFFER_BYTES) \
	  --sort-interval-us $(SORT_INTERVAL_US)

run-gpu-priority-queue-socket: $(GPU_PQ_BIN)
	$(GPU_PQ_BIN) --socket $(SOCKET_PORT) --link-bps $(LINK_BPS) --buffer-bytes $(BUFFER_BYTES) \
	  --sort-interval-us $(SORT_INTERVAL_US)

run-cpu-fifo-ns3: $(CPU_FIFO_BIN)
	$(CPU_FIFO_BIN) --socket $(SOCKET_PORT) --link-bps $(LINK_BPS) --buffer-bytes $(BUFFER_BYTES) & \
	sleep 0.5 && PYTHONPATH=. $(PYTHON) -m traffic.ns3_incast \
	  --scenario $(SCENARIO) --port $(SOCKET_PORT) \
	  --link-bps $(LINK_BPS) --buffer-bytes $(BUFFER_BYTES)

run-cpu-priority-queue-ns3: $(CPU_PQ_BIN)
	$(CPU_PQ_BIN) --socket $(SOCKET_PORT) --link-bps $(LINK_BPS) --buffer-bytes $(BUFFER_BYTES) \
	  --sort-interval-us $(SORT_INTERVAL_US) & \
	sleep 0.5 && PYTHONPATH=. $(PYTHON) -m traffic.ns3_incast \
	  --scenario $(SCENARIO) --port $(SOCKET_PORT) \
	  --link-bps $(LINK_BPS) --buffer-bytes $(BUFFER_BYTES)

run-gpu-priority-queue-ns3: $(GPU_PQ_BIN)
	$(GPU_PQ_BIN) --socket $(SOCKET_PORT) --link-bps $(LINK_BPS) --buffer-bytes $(BUFFER_BYTES) \
	  --sort-interval-us $(SORT_INTERVAL_US) & \
	sleep 0.5 && PYTHONPATH=. $(PYTHON) -m traffic.ns3_incast \
	  --scenario $(SCENARIO) --port $(SOCKET_PORT) \
	  --link-bps $(LINK_BPS) --buffer-bytes $(BUFFER_BYTES)

run-all: build
	PYTHONPATH=. SCENARIO=$(SCENARIO) LINK_BPS=$(LINK_BPS) BUFFER_BYTES=$(BUFFER_BYTES) \
	TRACE_DIR=$(TRACE_DIR) OUTPUT_FILE=results.xlsx \
	$(PYTHON) scripts/run_all_and_export.py

# compare: CPU PQ vs GPU PQ only, small scenario, fast terminal output.
# Typical runtime: <10 s.  Override scenario/buffer on the command line.
compare: $(CPU_PQ_BIN) $(GPU_PQ_BIN)
	PYTHONPATH=. \
	SCENARIO=$(COMPARE_SCENARIO) \
	LINK_BPS=$(LINK_BPS) \
	BUFFER_BYTES=$(COMPARE_BUFFER_BYTES) \
	SORT_INTERVAL_US=$(SORT_INTERVAL_US) \
	TRACE_DIR=$(TRACE_DIR) \
	$(PYTHON) scripts/compare.py

clean:
	rm -rf $(BUILD_DIR)
