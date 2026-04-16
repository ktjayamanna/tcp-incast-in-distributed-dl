#pragma once // Prevents multiple inclusions of the same header file. Modern version of #ifndef/#define/#endif

#include <cstdint> // fixed-width integer types with guaranteed sizes.
#include <optional>
#include <vector>

namespace sim::cpu_fifo
{

    enum class TrafficClass : std::uint8_t
    {
        Bulk = 0,
        Control = 1,
    };

    struct SyntheticPacketMetadata
    {
        std::uint32_t wave_id = 0;
        std::uint32_t sender_id = 0;
        std::uint32_t packet_index_for_sender = 0;
    };

    struct Packet
    {
        std::int64_t arrival_time_us = 0;
        std::uint32_t packet_size_bytes = 0;
        TrafficClass traffic_class = TrafficClass::Bulk;
        std::uint8_t priority_tag = 0;
        std::optional<SyntheticPacketMetadata> synthetic_metadata;
    };

    struct SimStats
    {
        std::uint64_t arrived_packets = 0;
        std::uint64_t dropped_packets = 0;
        std::uint64_t transmitted_packets = 0;

        std::uint64_t arrived_bytes = 0;
        std::uint64_t dropped_bytes = 0;
        std::uint64_t transmitted_bytes = 0;

        std::vector<std::int64_t> queue_delay_us_all;
        std::vector<std::int64_t> queue_delay_us_control;
        std::vector<std::int64_t> queue_delay_us_bulk;
    };

} // namespace sim::cpu_fifo
