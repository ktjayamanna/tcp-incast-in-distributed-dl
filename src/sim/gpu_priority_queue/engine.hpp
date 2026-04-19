#pragma once

#include "../cpu_fifo/config.hpp"
#include "../cpu_fifo/packet_source.hpp"
#include "../cpu_fifo/types.hpp"
#include "gpu_sorter.cuh"

namespace sim::gpu_priority_queue
{

using Packet               = sim::cpu_fifo::Packet;
using PacketSource         = sim::cpu_fifo::PacketSource;
using SimConfig            = sim::cpu_fifo::SimConfig;
using SimStats             = sim::cpu_fifo::SimStats;
using TrafficClass         = sim::cpu_fifo::TrafficClass;
using TrafficClassCounters = sim::cpu_fifo::TrafficClassCounters;
using sim::cpu_fifo::validate_config_or_throw;

// Accumulated GPU sort timing across the entire simulation run.
struct GpuSortStats
{
    std::uint64_t sort_calls            = 0;
    std::uint64_t total_packets_sorted  = 0;
    double        total_h2d_ms          = 0.0;
    double        total_kernel_ms       = 0.0;
    double        total_d2h_ms          = 0.0;
    double        total_gpu_wall_ms     = 0.0;
    double        total_cpu_sort_ms     = 0.0;  // equivalent std::sort on same batches

    // Epoch-level kernel timing (used as measured blind window).
    std::uint64_t sort_epochs           = 0;
    double        total_epoch_kernel_us = 0.0;
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
    GpuSimStats run(PacketSource &source);

private:
    SimConfig config_{};
    GpuSorter sorter_;
};

} // namespace sim::gpu_priority_queue
