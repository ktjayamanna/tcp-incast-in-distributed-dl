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

class Engine
{
public:
    explicit Engine(SimConfig config);
    SimStats run(PacketSource &source);

private:
    SimConfig config_{};
};

} // namespace sim::cpu_priority_queue
