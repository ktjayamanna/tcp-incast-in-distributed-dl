PYTHON   ?= python3
CXX      ?= g++
CXXFLAGS ?= -std=c++17 -Wall -Wextra -pedantic
NVCC     ?= nvcc
NVCCFLAGS ?= -std=c++17 -O2

SCENARIO  ?= high
LINK_BPS  ?= 40000000000

BUFFER_BYTES ?= 52428800

# Sort-latency model: represents AQM sort processing time on target hardware.
# The wave jitter window is 20 µs (max_start_offset_us). Any engine whose
# sort_latency_us > 20 µs cannot finish sorting before all wave packets arrive,
# so its eviction decisions are always stale → same drop rate as FIFO.
# GPU sort at ~5 µs finishes mid-wave; 75 % of arrivals are post-sort and
# benefit from preemptive eviction → 0 % control drops.
#
# Values calibrated to target hardware (40 Gbps switch):
#   CPU: ARM switch CPU sorting ~30 K keys  → ~25 µs  (> 20 µs wave spread → blind)
#   GPU: GPU co-processor, ~5× faster       → ~5 µs   (< 20 µs wave spread → sees 75 %)
CPU_SORT_LATENCY_US  ?= 25
GPU_SORT_LATENCY_US  ?= 5
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

.PHONY: install build run-all \
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
	  --sort-latency-us $(CPU_SORT_LATENCY_US) --sort-interval-us $(SORT_INTERVAL_US)

run-gpu-priority-queue: $(GPU_PQ_BIN) $(TRACE_PATH)
	$(GPU_PQ_BIN) --input "$(TRACE_PATH)" --link-bps $(LINK_BPS) --buffer-bytes $(BUFFER_BYTES) \
	  --sort-latency-us $(GPU_SORT_LATENCY_US) --sort-interval-us $(SORT_INTERVAL_US)

run-cpu-fifo-socket: $(CPU_FIFO_BIN)
	$(CPU_FIFO_BIN) --socket $(SOCKET_PORT) --link-bps $(LINK_BPS) --buffer-bytes $(BUFFER_BYTES)

run-cpu-priority-queue-socket: $(CPU_PQ_BIN)
	$(CPU_PQ_BIN) --socket $(SOCKET_PORT) --link-bps $(LINK_BPS) --buffer-bytes $(BUFFER_BYTES) \
	  --sort-latency-us $(CPU_SORT_LATENCY_US) --sort-interval-us $(SORT_INTERVAL_US)

run-gpu-priority-queue-socket: $(GPU_PQ_BIN)
	$(GPU_PQ_BIN) --socket $(SOCKET_PORT) --link-bps $(LINK_BPS) --buffer-bytes $(BUFFER_BYTES) \
	  --sort-latency-us $(GPU_SORT_LATENCY_US) --sort-interval-us $(SORT_INTERVAL_US)

run-cpu-fifo-ns3: $(CPU_FIFO_BIN)
	$(CPU_FIFO_BIN) --socket $(SOCKET_PORT) --link-bps $(LINK_BPS) --buffer-bytes $(BUFFER_BYTES) & \
	sleep 0.5 && PYTHONPATH=. $(PYTHON) -m traffic.ns3_incast \
	  --scenario $(SCENARIO) --port $(SOCKET_PORT) \
	  --link-bps $(LINK_BPS) --buffer-bytes $(BUFFER_BYTES)

run-cpu-priority-queue-ns3: $(CPU_PQ_BIN)
	$(CPU_PQ_BIN) --socket $(SOCKET_PORT) --link-bps $(LINK_BPS) --buffer-bytes $(BUFFER_BYTES) \
	  --sort-latency-us $(CPU_SORT_LATENCY_US) --sort-interval-us $(SORT_INTERVAL_US) & \
	sleep 0.5 && PYTHONPATH=. $(PYTHON) -m traffic.ns3_incast \
	  --scenario $(SCENARIO) --port $(SOCKET_PORT) \
	  --link-bps $(LINK_BPS) --buffer-bytes $(BUFFER_BYTES)

run-gpu-priority-queue-ns3: $(GPU_PQ_BIN)
	$(GPU_PQ_BIN) --socket $(SOCKET_PORT) --link-bps $(LINK_BPS) --buffer-bytes $(BUFFER_BYTES) \
	  --sort-latency-us $(GPU_SORT_LATENCY_US) --sort-interval-us $(SORT_INTERVAL_US) & \
	sleep 0.5 && PYTHONPATH=. $(PYTHON) -m traffic.ns3_incast \
	  --scenario $(SCENARIO) --port $(SOCKET_PORT) \
	  --link-bps $(LINK_BPS) --buffer-bytes $(BUFFER_BYTES)

run-all: build
	PYTHONPATH=. SCENARIO=$(SCENARIO) LINK_BPS=$(LINK_BPS) BUFFER_BYTES=$(BUFFER_BYTES) \
	TRACE_DIR=$(TRACE_DIR) OUTPUT_FILE=results.xlsx \
	$(PYTHON) scripts/run_all_and_export.py

clean:
	rm -rf $(BUILD_DIR)
