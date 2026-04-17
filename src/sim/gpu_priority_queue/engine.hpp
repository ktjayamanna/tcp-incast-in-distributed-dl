#pragma once

#include "../cpu_fifo/packet_source.hpp"
#include "../cpu_fifo/types.hpp"
#include "config.hpp"
#include "gpu_sorter.cuh"

namespace sim::gpu_priority_queue
{

using Packet               = sim::cpu_fifo::Packet;
using PacketSource         = sim::cpu_fifo::PacketSource;
using SimStats             = sim::cpu_fifo::SimStats;
using TrafficClass         = sim::cpu_fifo::TrafficClass;
using TrafficClassCounters = sim::cpu_fifo::TrafficClassCounters;

// Accumulated GPU sort timing across the entire simulation run.
struct GpuSortStats
{
    uint64_t sort_calls       = 0;
    uint64_t total_packets_sorted = 0;
    double   total_h2d_ms     = 0.0;
    double   total_kernel_ms  = 0.0;
    double   total_d2h_ms     = 0.0;
    double   total_gpu_wall_ms = 0.0;  // GPU sort wall time (H2D+kernel+D2H+overhead)
    double   total_cpu_sort_ms = 0.0;  // equivalent std::sort time on same batches
};

struct GpuSimStats
{
    SimStats     sim{};
    GpuSortStats gpu{};
};

class Engine
{
public:
    explicit Engine(SimConfig config);

    GpuSimStats run(PacketSource& packet_source);

private:
    SimConfig config_{};
    GpuSorter sorter_;
};

} // namespace sim::gpu_priority_queue
