#pragma once

#include "../cpu_fifo/config.hpp"
#include "../cpu_fifo/packet_source.hpp"
#include "../cpu_fifo/types.hpp"

namespace sim::cpu_priority_queue
{

using Packet                    = sim::cpu_fifo::Packet;
using PacketSource              = sim::cpu_fifo::PacketSource;
using SimConfig                 = sim::cpu_fifo::SimConfig;
using SimStats                  = sim::cpu_fifo::SimStats;
using TrafficClass              = sim::cpu_fifo::TrafficClass;
using TrafficClassCounters      = sim::cpu_fifo::TrafficClassCounters;
using sim::cpu_fifo::validate_config_or_throw;

// Sort timing measured from actual std::sort calls at epoch boundaries.
struct CpuSortStats
{
    std::uint64_t sort_epochs           = 0;
    double        total_sort_us         = 0.0;  // sum of measured std::sort durations
    double        total_sim_wall_us     = 0.0;  // total engine.run() duration
};

struct CpuPqSimStats
{
    SimStats     sim{};
    CpuSortStats sort{};
};

class Engine
{
public:
    explicit Engine(SimConfig config);
    CpuPqSimStats run(PacketSource &source);

private:
    SimConfig config_{};
};

} // namespace sim::cpu_priority_queue
