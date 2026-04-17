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

class Engine
{
public:
    explicit Engine(SimConfig config);

    SimStats run(PacketSource& packet_source);

private:
    SimConfig config_{};
    GpuSorter sorter_;
};

} // namespace sim::gpu_priority_queue
