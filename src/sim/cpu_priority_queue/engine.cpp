#include "engine.hpp"
#include "../cpu_fifo/engine_utils.hpp"

#include <algorithm>
#include <limits>
#include <vector>

using sim::cpu_fifo::class_counters;
using sim::cpu_fifo::transmission_time_us;
using sim::cpu_fifo::record_queue_delay;

namespace sim::cpu_priority_queue
{

namespace
{

// Identical sort-key encoding to GPU engine.
inline std::uint64_t make_sort_key(std::uint8_t priority_tag, std::uint32_t seq)
{
    return (static_cast<std::uint64_t>(255u - priority_tag) << 32) | seq;
}

struct PendingEntry { Packet packet; std::uint32_t sequence = 0; };

} // namespace

Engine::Engine(SimConfig config) : config_(config) { validate_config_or_throw(config_); }

SimStats Engine::run(PacketSource &source)
{
    SimStats stats{};

    std::vector<PendingEntry>  pending;
    std::vector<std::uint64_t> sort_keys;
    std::vector<std::uint32_t> sorted_indices;

    std::uint64_t queued_bytes = 0;
    std::uint32_t sequence     = 0;
    std::int64_t  link_free_us = 0;

    // sort_free_us: simulation time when the latest sort result becomes valid for eviction.
    // Packets arriving before sort_free_us are in the "blind window" — the sort hasn't
    // completed yet on target hardware — so they fall back to tail-drop.
    // sort_interval_us controls how often sort_free_us is refreshed; between refreshes
    // the sorted order from the previous epoch is still used for draining.
    std::int64_t  sort_free_us        = 0;
    std::int64_t  next_sort_epoch_us  = 0;
    std::uint32_t evict_candidate_seq = std::numeric_limits<std::uint32_t>::max();

    // CPU batch sort + drain. Mirrors the GPU engine; only the sort implementation differs.
    // Sorting always runs so the queue drains in priority order.
    // sort_free_us is only refreshed on epoch boundaries (sort_interval_us), modelling
    // the delay before a fresh sort result is available on target hardware.
    auto drain_until = [&](std::int64_t limit_us)
    {
        if (pending.empty()) return;

        const int n = static_cast<int>(pending.size());
        sort_keys.resize(n);
        sorted_indices.resize(n);
        for (int i = 0; i < n; i++) {
            sort_keys[i]      = make_sort_key(pending[i].packet.priority_tag, pending[i].sequence);
            sorted_indices[i] = static_cast<std::uint32_t>(i);
        }

        // CPU batch sort (std::sort) — GPU engine uses thrust::sort_by_key here.
        std::sort(sorted_indices.begin(), sorted_indices.end(),
            [&](std::uint32_t a, std::uint32_t b){ return sort_keys[a] < sort_keys[b]; });

        // Refresh sort_free_us only on epoch boundaries.
        // Between epochs the previous sort_free_us remains in effect, meaning packets
        // arriving after sort_free_us from the LAST epoch are still sort_ready.
        if (config_.sort_interval_us == 0 || limit_us >= next_sort_epoch_us) {
            sort_free_us       = limit_us + config_.sort_latency_us;
            next_sort_epoch_us = limit_us + std::max(config_.sort_interval_us,
                                                      config_.sort_latency_us);
        }

        // O(1) eviction candidate from sort order — identical to GPU engine.
        evict_candidate_seq = std::numeric_limits<std::uint32_t>::max();
        for (int i = n - 1; i >= 0; i--)
        {
            const std::uint32_t idx = sorted_indices[i];
            if (pending[idx].packet.traffic_class == TrafficClass::Bulk)
            {
                evict_candidate_seq = pending[idx].sequence;
                break;
            }
        }

        // Drain in priority order (always, regardless of sort epoch).
        std::vector<bool> consumed(static_cast<std::size_t>(n), false);
        for (int i = 0; i < n; i++)
        {
            const std::uint32_t idx   = sorted_indices[i];
            const Packet       &pkt   = pending[idx].packet;
            const std::int64_t  start = std::max(link_free_us, pkt.arrival_time_us);
            if (start > limit_us) break;

            link_free_us  = start + transmission_time_us(pkt.packet_size_bytes, config_.link_bandwidth_bps);
            consumed[idx] = true;
            queued_bytes -= pkt.packet_size_bytes;

            stats.transmitted_packets += 1;
            stats.transmitted_bytes   += pkt.packet_size_bytes;
            auto &cc = class_counters(stats, pkt.traffic_class);
            cc.transmitted_packets    += 1;
            cc.transmitted_bytes      += pkt.packet_size_bytes;
            record_queue_delay(stats, pkt, start - pkt.arrival_time_us);

            if (pending[idx].sequence == evict_candidate_seq)
                evict_candidate_seq = std::numeric_limits<std::uint32_t>::max();
        }

        std::vector<PendingEntry> remaining;
        for (int i = 0; i < n; i++)
            if (!consumed[i]) remaining.push_back(std::move(pending[i]));
        pending = std::move(remaining);
    };

    while (source.has_next())
    {
        Packet pkt = source.next();
        stats.arrived_packets += 1;
        stats.arrived_bytes   += pkt.packet_size_bytes;
        auto &cc = class_counters(stats, pkt.traffic_class);
        cc.arrived_packets    += 1;
        cc.arrived_bytes      += pkt.packet_size_bytes;

        drain_until(pkt.arrival_time_us);

        const bool buffer_full = queued_bytes + pkt.packet_size_bytes > config_.buffer_capacity_bytes;

        // Preemptive eviction: gate on sort_free_us — packets arriving during the sort's
        // blind window cannot trigger eviction, modelling target hardware latency.
        // GPU's lower sort_latency_us means a shorter blind window → fewer missed evictions.
        const bool sort_ready = pkt.arrival_time_us >= sort_free_us;

        if (buffer_full && pkt.traffic_class == TrafficClass::Control
            && sort_ready
            && evict_candidate_seq != std::numeric_limits<std::uint32_t>::max())
        {
            auto it = std::find_if(pending.begin(), pending.end(),
                [s = evict_candidate_seq](const PendingEntry &e){ return e.sequence == s; });

            queued_bytes -= it->packet.packet_size_bytes;
            stats.dropped_packets                      += 1;
            stats.dropped_bytes                        += it->packet.packet_size_bytes;
            auto &ec = class_counters(stats, it->packet.traffic_class);
            ec.dropped_packets                         += 1;
            ec.dropped_bytes                           += it->packet.packet_size_bytes;
            pending.erase(it);
            evict_candidate_seq = std::numeric_limits<std::uint32_t>::max();

            queued_bytes += pkt.packet_size_bytes;
            pending.push_back({std::move(pkt), sequence++});
        }
        else if (buffer_full)
        {
            stats.dropped_packets += 1;
            stats.dropped_bytes   += pkt.packet_size_bytes;
            cc.dropped_packets    += 1;
            cc.dropped_bytes      += pkt.packet_size_bytes;
        }
        else
        {
            queued_bytes += pkt.packet_size_bytes;
            pending.push_back({std::move(pkt), sequence++});
        }
    }

    drain_until(std::numeric_limits<std::int64_t>::max());
    return stats;
}

} // namespace sim::cpu_priority_queue
