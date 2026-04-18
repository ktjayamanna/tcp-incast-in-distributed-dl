#include "engine.hpp"
#include "engine_utils.hpp"

#include <algorithm>
#include <deque>
#include <utility>

namespace sim::cpu_fifo
{

Engine::Engine(SimConfig config) : config_(config) { validate_config_or_throw(config_); }

SimStats Engine::run(PacketSource &source)
{
    SimStats stats{};
    std::deque<std::pair<std::int64_t, std::uint32_t>> queued_departures;
    std::uint64_t queued_bytes        = 0;
    std::int64_t  last_departure_us   = 0;

    while (source.has_next())
    {
        Packet pkt = source.next();
        stats.arrived_packets          += 1;
        stats.arrived_bytes            += pkt.packet_size_bytes;
        auto &cc = class_counters(stats, pkt.traffic_class);
        cc.arrived_packets             += 1;
        cc.arrived_bytes               += pkt.packet_size_bytes;

        // Retire packets that finished transmitting before this one arrived.
        while (!queued_departures.empty() &&
               queued_departures.front().first <= pkt.arrival_time_us)
        {
            queued_bytes -= queued_departures.front().second;
            queued_departures.pop_front();
        }

        if (queued_bytes + pkt.packet_size_bytes > config_.buffer_capacity_bytes)
        {
            stats.dropped_packets += 1;
            stats.dropped_bytes   += pkt.packet_size_bytes;
            cc.dropped_packets    += 1;
            cc.dropped_bytes      += pkt.packet_size_bytes;
            continue;
        }

        const std::int64_t start_us     = std::max(pkt.arrival_time_us, last_departure_us);
        const std::int64_t departure_us = start_us + transmission_time_us(pkt.packet_size_bytes, config_.link_bandwidth_bps);

        last_departure_us = departure_us;
        queued_bytes      += pkt.packet_size_bytes;
        queued_departures.emplace_back(departure_us, pkt.packet_size_bytes);

        stats.transmitted_packets += 1;
        stats.transmitted_bytes   += pkt.packet_size_bytes;
        cc.transmitted_packets    += 1;
        cc.transmitted_bytes      += pkt.packet_size_bytes;
        record_queue_delay(stats, pkt, start_us - pkt.arrival_time_us);
    }

    return stats;
}

} // namespace sim::cpu_fifo
