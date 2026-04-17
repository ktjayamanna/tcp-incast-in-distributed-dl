#include "engine.hpp"

#include <algorithm>
#include <cstdint>
#include <limits>
#include <vector>

namespace sim::gpu_priority_queue
{

namespace
{

TrafficClassCounters& class_counters(SimStats& stats, TrafficClass tc)
{
    return (tc == TrafficClass::Control) ? stats.control : stats.bulk;
}

std::int64_t transmission_time_us(std::uint32_t size_bytes, std::uint64_t bw_bps)
{
    const std::uint64_t bits   = static_cast<std::uint64_t>(size_bytes) * 8ULL;
    const std::uint64_t scaled = bits * 1'000'000ULL;
    const std::uint64_t result = (scaled + bw_bps - 1ULL) / bw_bps;
    return static_cast<std::int64_t>(std::max<std::uint64_t>(1ULL, result));
}

void record_queue_delay(SimStats& stats, const Packet& pkt, std::int64_t delay_us)
{
    stats.queue_delay_us_all.push_back(delay_us);
    if (pkt.traffic_class == TrafficClass::Control)
        stats.queue_delay_us_control.push_back(delay_us);
    else
        stats.queue_delay_us_bulk.push_back(delay_us);
}

// Sort key: smaller value => higher scheduling priority.
//   High 32 bits: (255 - priority_tag)  — higher tag means lower key (served first)
//   Low  32 bits: sequence              — FIFO tie-breaking within same priority
inline std::uint64_t make_sort_key(std::uint8_t priority_tag, std::uint32_t seq)
{
    return (static_cast<std::uint64_t>(255u - priority_tag) << 32) |
           static_cast<std::uint64_t>(seq);
}

struct PendingEntry
{
    Packet        packet;
    std::uint32_t sequence = 0;
};

} // namespace

// ---- Engine -----------------------------------------------------------------

Engine::Engine(SimConfig config)
    : config_(config), sorter_(kMaxSortCapacity)
{
    validate_config_or_throw(config_);
}

SimStats Engine::run(PacketSource& packet_source)
{
    SimStats stats{};

    std::vector<PendingEntry>  pending;         // unsorted packets currently in buffer
    std::vector<std::uint64_t> sort_keys;       // scratch: sort keys for GPU
    std::vector<std::uint32_t> sorted_indices;  // scratch: GPU output (priority order)

    std::uint64_t queued_bytes      = 0;
    std::uint32_t sequence          = 0;
    std::int64_t  link_free_time_us = 0;

    // Send the current pending batch to the GPU, sort by priority, then
    // transmit packets in that order until the next start time exceeds limit_time_us.
    // Transmitted entries are compacted out of `pending` before returning.
    auto drain_until = [&](std::int64_t limit_time_us)
    {
        if (pending.empty()) return;

        const int n = static_cast<int>(pending.size());
        sort_keys.resize(n);
        sorted_indices.resize(n);

        for (int i = 0; i < n; i++)
            sort_keys[i] = make_sort_key(
                pending[i].packet.priority_tag,
                pending[i].sequence);

        // GPU bitonic sort: sorted_indices[0] = index of highest-priority packet
        sorter_.sort(sort_keys.data(), sorted_indices.data(), n);

        std::vector<bool> consumed(static_cast<std::size_t>(n), false);

        for (int i = 0; i < n; i++)
        {
            const std::uint32_t idx   = sorted_indices[i];
            const Packet&        pkt   = pending[idx].packet;
            const std::int64_t   start = std::max(link_free_time_us, pkt.arrival_time_us);

            // All pending packets have already arrived (arrival_time <= limit_time_us),
            // so start > limit_time_us only when the link is busy past the limit.
            // Once the link is too busy for the highest-priority remaining packet,
            // it will be too busy for all lower-priority ones too, so we stop.
            if (start > limit_time_us) break;

            const std::int64_t departure =
                start + transmission_time_us(pkt.packet_size_bytes, config_.link_bandwidth_bps);

            link_free_time_us = departure;
            consumed[idx]     = true;
            queued_bytes     -= pkt.packet_size_bytes;

            stats.transmitted_packets += 1;
            stats.transmitted_bytes   += pkt.packet_size_bytes;
            TrafficClassCounters& cc   = class_counters(stats, pkt.traffic_class);
            cc.transmitted_packets    += 1;
            cc.transmitted_bytes      += pkt.packet_size_bytes;
            record_queue_delay(stats, pkt, start - pkt.arrival_time_us);
        }

        // Compact: remove transmitted entries so the next sort sees only live packets.
        std::vector<PendingEntry> remaining;
        remaining.reserve(static_cast<std::size_t>(n));
        for (int i = 0; i < n; i++)
            if (!consumed[i]) remaining.push_back(std::move(pending[i]));
        pending = std::move(remaining);
    };

    // ---- main simulation loop -----------------------------------------------

    while (packet_source.has_next())
    {
        Packet pkt = packet_source.next();

        stats.arrived_packets += 1;
        stats.arrived_bytes   += pkt.packet_size_bytes;
        TrafficClassCounters& cc = class_counters(stats, pkt.traffic_class);
        cc.arrived_packets += 1;
        cc.arrived_bytes   += pkt.packet_size_bytes;

        // Drain packets that could have been scheduled before this one arrived.
        drain_until(pkt.arrival_time_us);

        if (queued_bytes + pkt.packet_size_bytes > config_.buffer_capacity_bytes)
        {
            stats.dropped_packets += 1;
            stats.dropped_bytes   += pkt.packet_size_bytes;
            cc.dropped_packets    += 1;
            cc.dropped_bytes      += pkt.packet_size_bytes;
            continue;
        }

        PendingEntry entry{};
        entry.packet   = std::move(pkt);
        entry.sequence = sequence++;
        queued_bytes  += entry.packet.packet_size_bytes;
        pending.push_back(std::move(entry));
    }

    // Drain everything that remains.
    drain_until(std::numeric_limits<std::int64_t>::max());

    return stats;
}

} // namespace sim::gpu_priority_queue
