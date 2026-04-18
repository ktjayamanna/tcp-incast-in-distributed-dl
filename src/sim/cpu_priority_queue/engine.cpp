#include "engine.hpp"
#include "../cpu_fifo/engine_utils.hpp"

#include <algorithm>
#include <limits>
#include <queue>
#include <utility>
#include <vector>

using sim::cpu_fifo::class_counters;
using sim::cpu_fifo::transmission_time_us;
using sim::cpu_fifo::record_queue_delay;

namespace sim::cpu_priority_queue
{

namespace
{

// Wrap Packet with a sequence number for FIFO tie-breaking within equal priority.
struct QueuedPacket
{
    Packet        packet;
    std::uint64_t sequence = 0;
};

// Higher priority_tag wins; equal priority → earlier arrival wins (lower sequence).
struct Compare
{
    bool operator()(const QueuedPacket &a, const QueuedPacket &b) const
    {
        if (a.packet.priority_tag != b.packet.priority_tag)
            return a.packet.priority_tag < b.packet.priority_tag;
        return a.sequence > b.sequence;
    }
};

using PQ = std::priority_queue<QueuedPacket, std::vector<QueuedPacket>, Compare>;

void drain_until(PQ &pq, std::uint64_t &queued_bytes, std::int64_t limit_us,
                 std::int64_t &link_free_us, const SimConfig &cfg, SimStats &stats)
{
    while (!pq.empty())
    {
        const QueuedPacket &top        = pq.top();
        const std::int64_t  start_us   = std::max(link_free_us, top.packet.arrival_time_us);
        if (start_us > limit_us) break;

        const Packet pkt = top.packet;
        pq.pop();
        queued_bytes -= pkt.packet_size_bytes;

        const std::int64_t departure_us = start_us + transmission_time_us(pkt.packet_size_bytes, cfg.link_bandwidth_bps);
        link_free_us = departure_us;

        stats.transmitted_packets += 1;
        stats.transmitted_bytes   += pkt.packet_size_bytes;
        auto &cc = class_counters(stats, pkt.traffic_class);
        cc.transmitted_packets    += 1;
        cc.transmitted_bytes      += pkt.packet_size_bytes;
        record_queue_delay(stats, pkt, start_us - pkt.arrival_time_us);
    }
}

} // namespace

Engine::Engine(SimConfig config) : config_(config) { validate_config_or_throw(config_); }

SimStats Engine::run(PacketSource &source)
{
    SimStats      stats{};
    PQ            pq;
    std::uint64_t queued_bytes  = 0;
    std::uint64_t sequence      = 0;
    std::int64_t  link_free_us  = 0;

    while (source.has_next())
    {
        Packet pkt = source.next();
        stats.arrived_packets          += 1;
        stats.arrived_bytes            += pkt.packet_size_bytes;
        auto &cc = class_counters(stats, pkt.traffic_class);
        cc.arrived_packets             += 1;
        cc.arrived_bytes               += pkt.packet_size_bytes;

        drain_until(pq, queued_bytes, pkt.arrival_time_us, link_free_us, config_, stats);

        if (queued_bytes + pkt.packet_size_bytes > config_.buffer_capacity_bytes)
        {
            stats.dropped_packets += 1;
            stats.dropped_bytes   += pkt.packet_size_bytes;
            cc.dropped_packets    += 1;
            cc.dropped_bytes      += pkt.packet_size_bytes;
            continue;
        }

        queued_bytes += pkt.packet_size_bytes;
        pq.push(QueuedPacket{std::move(pkt), sequence++});
    }

    drain_until(pq, queued_bytes, std::numeric_limits<std::int64_t>::max(),
                link_free_us, config_, stats);
    return stats;
}

} // namespace sim::cpu_priority_queue
