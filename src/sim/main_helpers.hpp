#pragma once

#include <cstdint>
#include <iostream>
#include <numeric>
#include <stdexcept>
#include <string>
#include <vector>

#include "cpu_fifo/types.hpp"

// Shared utilities for all three simulation binaries.
namespace sim
{

inline std::uint64_t parse_u64(const std::string &value, const char *flag)
{
    std::size_t pos = 0;
    const auto parsed = std::stoull(value, &pos, 10);
    if (pos != value.size())
        throw std::invalid_argument(std::string(flag) + " must be an unsigned integer");
    return static_cast<std::uint64_t>(parsed);
}

inline double average(const std::vector<std::int64_t> &v)
{
    if (v.empty()) return 0.0;
    return std::accumulate(v.begin(), v.end(), 0.0) / static_cast<double>(v.size());
}

inline double drop_rate(const cpu_fifo::TrafficClassCounters &c)
{
    return c.arrived_packets ? static_cast<double>(c.dropped_packets) / c.arrived_packets : 0.0;
}

inline void print_stats(const cpu_fifo::SimStats &s)
{
    const double total_drop = s.arrived_packets
        ? static_cast<double>(s.dropped_packets) / s.arrived_packets : 0.0;

    std::cout << "arrived="     << s.arrived_packets     << '\n';
    std::cout << "dropped="     << s.dropped_packets
              << "  (" << total_drop * 100.0 << "%)\n";
    std::cout << "transmitted=" << s.transmitted_packets << '\n';
    std::cout << "control: arrived=" << s.control.arrived_packets
              << " dropped="         << s.control.dropped_packets
              << " (" << drop_rate(s.control) * 100.0 << "%)\n";
    std::cout << "bulk:    arrived=" << s.bulk.arrived_packets
              << " dropped="         << s.bulk.dropped_packets
              << " (" << drop_rate(s.bulk) * 100.0 << "%)\n";
    std::cout << "avg_queue_delay_us:  all="     << average(s.queue_delay_us_all)
              << "  control=" << average(s.queue_delay_us_control)
              << "  bulk="    << average(s.queue_delay_us_bulk) << '\n';
}

} // namespace sim
