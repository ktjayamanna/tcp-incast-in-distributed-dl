#include "engine.hpp"
#include "../cpu_fifo/engine_utils.hpp"

#include <algorithm>
#include <chrono>
#include <limits>
#include <vector>

using sim::cpu_fifo::class_counters;
using sim::cpu_fifo::transmission_time_us;
using sim::cpu_fifo::record_queue_delay;

namespace sim::cpu_priority_queue
{

namespace
{

inline std::uint64_t make_sort_key(std::uint8_t priority_tag, std::uint32_t seq)
{
    return (static_cast<std::uint64_t>(255u - priority_tag) << 32) | seq;
}

struct PendingEntry { Packet packet; std::uint32_t sequence = 0; };

} // namespace

Engine::Engine(SimConfig config) : config_(config) { validate_config_or_throw(config_); }

CpuPqSimStats Engine::run(PacketSource &source)
{
    CpuPqSimStats result{};
    SimStats&     stats      = result.sim;
    CpuSortStats& sort_stats = result.sort;

    std::vector<PendingEntry>  pending;
    std::vector<std::uint32_t> sorted_indices;

    std::uint64_t queued_bytes = 0;
    std::uint32_t sequence     = 0;
    std::int64_t  link_free_us = 0;

    std::int64_t  sort_free_us        = 0;
    std::int64_t  next_sort_epoch_us  = 0;
    std::uint32_t evict_candidate_seq = std::numeric_limits<std::uint32_t>::max();

    // epoch_sorted: true when sorted_indices is valid for the current pending[]
    // contents at the epoch boundary.  Set false when pending grows (new arrivals
    // or eviction deletes an entry and the indices become stale).
    bool epoch_sorted = false;

    // sort_at_epoch: runs std::sort and refreshes sort_free_us / evict_candidate.
    // Called only when a new sort epoch starts, not on every packet arrival.
    // This makes the simulation O(n log n) per epoch vs O(n² log n) in the naive
    // per-arrival approach, without changing the simulated blind-window semantics.
    auto sort_at_epoch = [&](std::int64_t epoch_us)
    {
        const int n = static_cast<int>(pending.size());
        sorted_indices.resize(static_cast<std::size_t>(n));
        for (int i = 0; i < n; ++i)
            sorted_indices[i] = static_cast<std::uint32_t>(i);

        const auto t0 = std::chrono::steady_clock::now();
        std::sort(sorted_indices.begin(), sorted_indices.end(),
            [&](std::uint32_t a, std::uint32_t b)
            {
                return make_sort_key(pending[a].packet.priority_tag, pending[a].sequence)
                     < make_sort_key(pending[b].packet.priority_tag, pending[b].sequence);
            });
        const double measured_us = std::chrono::duration<double, std::micro>(
            std::chrono::steady_clock::now() - t0).count();

        sort_stats.sort_epochs    += 1;
        sort_stats.total_sort_us  += measured_us;

        // Use measured std::sort time as the blind window (how long the CPU
        // was occupied sorting before eviction decisions could be made).
        // If a manual override was provided via --sort-latency-us, honour it.
        const std::int64_t latency_us = (config_.sort_latency_us > 0)
            ? config_.sort_latency_us
            : static_cast<std::int64_t>(measured_us);

        sort_free_us       = epoch_us + latency_us;
        next_sort_epoch_us = epoch_us + std::max(config_.sort_interval_us, latency_us);

        evict_candidate_seq = std::numeric_limits<std::uint32_t>::max();
        for (int i = static_cast<int>(n) - 1; i >= 0; --i)
        {
            if (pending[sorted_indices[i]].packet.traffic_class == TrafficClass::Bulk)
            {
                evict_candidate_seq = pending[sorted_indices[i]].sequence;
                break;
            }
        }

        epoch_sorted = true;
    };

    auto drain_until = [&](std::int64_t limit_us)
    {
        if (pending.empty()) return;

        // Sort only at epoch boundaries (or every call when sort_interval_us == 0).
        const bool new_epoch = (config_.sort_interval_us == 0) ||
                               (limit_us >= next_sort_epoch_us);
        if (new_epoch)
            sort_at_epoch(limit_us);

        const int n = static_cast<int>(pending.size());

        if (epoch_sorted)
        {
            // Drain in priority order using the epoch sort result.
            // sorted_indices may reference entries appended after the sort;
            // guard with an index-bounds check.
            std::vector<bool> consumed(static_cast<std::size_t>(n), false);
            for (std::uint32_t idx : sorted_indices)
            {
                if (idx >= static_cast<std::uint32_t>(n)) break;
                const Packet       &pkt   = pending[idx].packet;
                const std::int64_t  start = std::max(link_free_us, pkt.arrival_time_us);
                if (start > limit_us) break;

                link_free_us  = start + transmission_time_us(pkt.packet_size_bytes,
                                                              config_.link_bandwidth_bps);
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
            for (int i = 0; i < n; ++i)
                if (!consumed[i]) remaining.push_back(std::move(pending[i]));
            pending = std::move(remaining);
            // sorted_indices now refers to old positions — mark stale.
            epoch_sorted = false;
        }
        else
        {
            // Between epoch sorts, drain in arrival order (FIFO).
            // Priority ordering resumes at the next epoch boundary.
            std::size_t kept = 0;
            for (int i = 0; i < n; ++i)
            {
                const Packet       &pkt   = pending[i].packet;
                const std::int64_t  start = std::max(link_free_us, pkt.arrival_time_us);
                if (start > limit_us)
                {
                    if (kept != static_cast<std::size_t>(i))
                        pending[kept] = std::move(pending[i]);
                    ++kept;
                    continue;
                }
                link_free_us  = start + transmission_time_us(pkt.packet_size_bytes,
                                                              config_.link_bandwidth_bps);
                queued_bytes -= pkt.packet_size_bytes;

                stats.transmitted_packets += 1;
                stats.transmitted_bytes   += pkt.packet_size_bytes;
                auto &cc = class_counters(stats, pkt.traffic_class);
                cc.transmitted_packets    += 1;
                cc.transmitted_bytes      += pkt.packet_size_bytes;
                record_queue_delay(stats, pkt, start - pkt.arrival_time_us);

                if (pending[i].sequence == evict_candidate_seq)
                    evict_candidate_seq = std::numeric_limits<std::uint32_t>::max();
            }
            pending.resize(kept);
        }
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
        const bool sort_ready  = pkt.arrival_time_us >= sort_free_us;

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
            epoch_sorted = false;
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
    return result;
}

} // namespace sim::cpu_priority_queue
