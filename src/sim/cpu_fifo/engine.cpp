#include "engine.hpp"

#include <algorithm>
#include <cstdint>
#include <deque>
#include <stdexcept>
#include <utility>

namespace sim::cpu_fifo
{

    namespace
    {

        TrafficClassCounters &class_counters(SimStats &stats, TrafficClass traffic_class)
        {
            if (traffic_class == TrafficClass::Control)
            {
                return stats.control;
            }
            return stats.bulk;
        }

        std::int64_t transmission_time_us(std::uint32_t packet_size_bytes, std::uint64_t link_bandwidth_bps)
        {
            const std::uint64_t packet_bits = static_cast<std::uint64_t>(packet_size_bytes) * 8ULL;
            const std::uint64_t scaled = packet_bits * 1'000'000ULL;
            const std::uint64_t rounded_up = (scaled + link_bandwidth_bps - 1ULL) / link_bandwidth_bps;
            return static_cast<std::int64_t>(std::max<std::uint64_t>(1ULL, rounded_up));
        }

        void record_queue_delay(SimStats &stats, const Packet &packet, std::int64_t queue_delay_us)
        {
            stats.queue_delay_us_all.push_back(queue_delay_us);
            if (packet.traffic_class == TrafficClass::Control)
            {
                stats.queue_delay_us_control.push_back(queue_delay_us);
                return;
            }
            stats.queue_delay_us_bulk.push_back(queue_delay_us);
        }

    } // namespace

    Engine::Engine(SimConfig config)
        : config_(config)
    {
        validate_config_or_throw(config_);
    }

    SimStats Engine::run(PacketSource &packet_source)
    {
        SimStats stats{};
        std::deque<std::pair<std::int64_t, std::uint32_t>> queued_departures;
        std::uint64_t queued_bytes = 0;
        std::int64_t last_departure_time_us = 0;

        while (packet_source.has_next())
        {
            Packet packet = packet_source.next();
            stats.arrived_packets += 1;
            stats.arrived_bytes += packet.packet_size_bytes;
            TrafficClassCounters &packet_class_stats = class_counters(stats, packet.traffic_class);
            packet_class_stats.arrived_packets += 1;
            packet_class_stats.arrived_bytes += packet.packet_size_bytes;

            while (!queued_departures.empty() && queued_departures.front().first <= packet.arrival_time_us)
            {
                queued_bytes -= queued_departures.front().second;
                queued_departures.pop_front();
            }

            if (queued_bytes + packet.packet_size_bytes > config_.buffer_capacity_bytes)
            {
                stats.dropped_packets += 1;
                stats.dropped_bytes += packet.packet_size_bytes;
                packet_class_stats.dropped_packets += 1;
                packet_class_stats.dropped_bytes += packet.packet_size_bytes;
                continue;
            }

            const std::int64_t start_time_us = std::max(packet.arrival_time_us, last_departure_time_us);
            const std::int64_t queue_delay_us = start_time_us - packet.arrival_time_us;
            const std::int64_t departure_time_us =
                start_time_us + transmission_time_us(packet.packet_size_bytes, config_.link_bandwidth_bps);

            last_departure_time_us = departure_time_us;
            queued_bytes += packet.packet_size_bytes;
            queued_departures.emplace_back(departure_time_us, packet.packet_size_bytes);

            stats.transmitted_packets += 1;
            stats.transmitted_bytes += packet.packet_size_bytes;
            packet_class_stats.transmitted_packets += 1;
            packet_class_stats.transmitted_bytes += packet.packet_size_bytes;
            record_queue_delay(stats, packet, queue_delay_us);
        }

        return stats;
    }

} // namespace sim::cpu_fifo
