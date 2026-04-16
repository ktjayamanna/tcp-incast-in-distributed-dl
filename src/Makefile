PYTHON ?= python3
CXX ?= g++
CXXFLAGS ?= -std=c++17 -Wall -Wextra -pedantic

SCENARIO ?= normal_traffic
LINK_BPS ?= 40000000000
BUFFER_BYTES ?= 262144

BUILD_DIR ?= build
TRACE_DIR ?= data/traces
CPU_FIFO_BIN := $(BUILD_DIR)/cpu_fifo_sim
CPU_FIFO_SRCS := sim/cpu_fifo/main.cpp sim/cpu_fifo/engine.cpp sim/cpu_fifo/trace_csv.cpp

.PHONY: tmp-install build-cpu-fifo generate-trace run-cpu-fifo clean

tmp-install:
	$(PYTHON) -m pip install -e .[dev]

build-cpu-fifo: $(CPU_FIFO_BIN)

$(CPU_FIFO_BIN): $(CPU_FIFO_SRCS)
	mkdir -p $(BUILD_DIR)
	$(CXX) $(CXXFLAGS) $(CPU_FIFO_SRCS) -o $(CPU_FIFO_BIN)

generate-trace:
	PYTHONPATH=. TRACE_DIR=$(TRACE_DIR) SCENARIO=$(SCENARIO) $(PYTHON) -c 'from pathlib import Path; import os; from traffic.config import ScenarioName, get_scenario; from traffic.io.csv_export import generate_and_export_csv; scenario = ScenarioName(os.environ["SCENARIO"]); output_path = generate_and_export_csv(config=get_scenario(scenario), scenario_name=scenario, output_dir=Path(os.environ["TRACE_DIR"])); print(output_path)'

run-cpu-fifo: build-cpu-fifo
	TRACE_PATH="$$(PYTHONPATH=. TRACE_DIR=$(TRACE_DIR) SCENARIO=$(SCENARIO) $(PYTHON) -c 'from pathlib import Path; import os; from traffic.config import ScenarioName, get_scenario; from traffic.io.csv_export import build_trace_path; scenario = ScenarioName(os.environ["SCENARIO"]); print(build_trace_path(config=get_scenario(scenario), scenario_name=scenario, output_dir=Path(os.environ["TRACE_DIR"])))')"; \
	if [ ! -f "$$TRACE_PATH" ]; then \
		$(MAKE) generate-trace SCENARIO=$(SCENARIO) TRACE_DIR=$(TRACE_DIR); \
	fi; \
	$(CPU_FIFO_BIN) --input "$$TRACE_PATH" --link-bps $(LINK_BPS) --buffer-bytes $(BUFFER_BYTES)

clean:
	rm -rf $(BUILD_DIR)
