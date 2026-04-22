PYTHON    ?= python3
CXX       ?= g++
CXXFLAGS  ?= -std=c++17 -Wall -Wextra -pedantic
NVCC      ?= nvcc
NVCCFLAGS ?= -std=c++17 -O2

SCENARIO         ?= high
LINK_BPS         ?= 45000000000
BUFFER_BYTES     ?= 52428800
SORT_INTERVAL_US ?= 9000
SOCKET_PORT      ?= 9000

COMPARE_SCENARIO ?= high

BUILD_DIR := build

CPU_FIFO_BIN  := $(BUILD_DIR)/cpu_fifo_sim
CPU_FIFO_SRCS := sim/cpu_fifo/main.cpp sim/cpu_fifo/engine.cpp \
                 sim/cpu_fifo/socket_source.cpp

CPU_PQ_BIN  := $(BUILD_DIR)/cpu_priority_queue_sim
CPU_PQ_SRCS := sim/cpu_priority_queue/main.cpp sim/cpu_priority_queue/engine.cpp \
               sim/cpu_fifo/socket_source.cpp

GPU_PQ_BIN  := $(BUILD_DIR)/gpu_priority_queue_sim
GPU_PQ_SRCS := sim/gpu_priority_queue/gpu_sorter.cu sim/gpu_priority_queue/engine.cpp \
               sim/gpu_priority_queue/main.cpp sim/cpu_fifo/socket_source.cpp

.PHONY: install build run-cpu-fifo run-cpu-priority-queue run-gpu-priority-queue compare clean

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

# ---- run targets (ns3 sends traffic over socket) -----------------------------

run-cpu-fifo: $(CPU_FIFO_BIN)
	$(CPU_FIFO_BIN) --socket $(SOCKET_PORT) --link-bps $(LINK_BPS) --buffer-bytes $(BUFFER_BYTES) & \
	sleep 0.5 && PYTHONPATH=. $(PYTHON) -m traffic.ns3_incast \
	  --scenario $(SCENARIO) --port $(SOCKET_PORT) \
	  --link-bps $(LINK_BPS) --buffer-bytes $(BUFFER_BYTES)

run-cpu-priority-queue: $(CPU_PQ_BIN)
	$(CPU_PQ_BIN) --socket $(SOCKET_PORT) --link-bps $(LINK_BPS) --buffer-bytes $(BUFFER_BYTES) \
	  --sort-interval-us $(SORT_INTERVAL_US) & \
	sleep 0.5 && PYTHONPATH=. $(PYTHON) -m traffic.ns3_incast \
	  --scenario $(SCENARIO) --port $(SOCKET_PORT) \
	  --link-bps $(LINK_BPS) --buffer-bytes $(BUFFER_BYTES)

run-gpu-priority-queue: $(GPU_PQ_BIN)
	$(GPU_PQ_BIN) --socket $(SOCKET_PORT) --link-bps $(LINK_BPS) --buffer-bytes $(BUFFER_BYTES) \
	  --sort-interval-us $(SORT_INTERVAL_US) & \
	sleep 0.5 && PYTHONPATH=. $(PYTHON) -m traffic.ns3_incast \
	  --scenario $(SCENARIO) --port $(SOCKET_PORT) \
	  --link-bps $(LINK_BPS) --buffer-bytes $(BUFFER_BYTES)

# compare: CPU PQ vs GPU PQ side by side, terminal output.
# Buffer size is chosen per-scenario by compare.py; override with BUFFER_BYTES=.
compare: $(CPU_PQ_BIN) $(GPU_PQ_BIN)
	PYTHONPATH=. \
	SCENARIO=$(COMPARE_SCENARIO) \
	LINK_BPS=$(LINK_BPS) \
	SORT_INTERVAL_US=$(SORT_INTERVAL_US) \
	SOCKET_PORT=$(SOCKET_PORT) \
	$(PYTHON) scripts/compare.py

clean:
	rm -rf $(BUILD_DIR)
