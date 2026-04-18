#pragma once

#include <cstdint>
#include <vector>

namespace sim::cpu_fifo
{

// Packets are classified into two priorities:
//   Control (ACK / sync) — priority_tag=46 — must not be delayed; cluster stalls if dropped
//   Bulk (gradient data)  — priority_tag=0  — tolerates delay
enum class TrafficClass : std::uint8_t
{
    Bulk    = 0,
    Control = 1,
};

struct Packet
{
    std::int64_t  arrival_time_us    = 0;
    std::uint32_t packet_size_bytes  = 0;
    TrafficClass  traffic_class      = TrafficClass::Bulk;
    std::uint8_t  priority_tag       = 0;
};

struct TrafficClassCounters
{
    std::uint64_t arrived_packets     = 0;
    std::uint64_t dropped_packets     = 0;
    std::uint64_t transmitted_packets = 0;
    std::uint64_t arrived_bytes       = 0;
    std::uint64_t dropped_bytes       = 0;
    std::uint64_t transmitted_bytes   = 0;
};

struct SimStats
{
    std::uint64_t arrived_packets     = 0;
    std::uint64_t dropped_packets     = 0;
    std::uint64_t transmitted_packets = 0;
    std::uint64_t arrived_bytes       = 0;
    std::uint64_t dropped_bytes       = 0;
    std::uint64_t transmitted_bytes   = 0;

    TrafficClassCounters control{};
    TrafficClassCounters bulk{};

    std::vector<std::int64_t> queue_delay_us_all;
    std::vector<std::int64_t> queue_delay_us_control;
    std::vector<std::int64_t> queue_delay_us_bulk;
};

} // namespace sim::cpu_fifo
