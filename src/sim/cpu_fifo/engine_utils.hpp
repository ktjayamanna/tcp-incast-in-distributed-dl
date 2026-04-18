#pragma once

#include <algorithm>
#include <cstdint>

#include "types.hpp"

// Shared utilities used by all three queue engines.
namespace sim::cpu_fifo
{

inline TrafficClassCounters &class_counters(SimStats &stats, TrafficClass tc)
{
    return (tc == TrafficClass::Control) ? stats.control : stats.bulk;
}

// Microseconds to transmit size_bytes at bw_bps (minimum 1us).
inline std::int64_t transmission_time_us(std::uint32_t size_bytes, std::uint64_t bw_bps)
{
    const std::uint64_t bits   = static_cast<std::uint64_t>(size_bytes) * 8ULL;
    const std::uint64_t result = (bits * 1'000'000ULL + bw_bps - 1ULL) / bw_bps;
    return static_cast<std::int64_t>(std::max<std::uint64_t>(1ULL, result));
}

inline void record_queue_delay(SimStats &stats, const Packet &pkt, std::int64_t delay_us)
{
    stats.queue_delay_us_all.push_back(delay_us);
    if (pkt.traffic_class == TrafficClass::Control)
        stats.queue_delay_us_control.push_back(delay_us);
    else
        stats.queue_delay_us_bulk.push_back(delay_us);
}

} // namespace sim::cpu_fifo
