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
    const auto sim_wall_start = std::chrono::steady_clock::now();

    CpuPqSimStats result{};
    SimStats&     stats      = result.sim;
    CpuSortStats& sort_stats = result.sort;

    std::vector<PendingEntry>  pending;
    std::vector<std::uint32_t> sorted_indices;

    std::uint64_t queued_bytes = 0;
    std::uint32_t sequence     = 0;
    std::int64_t  link_free_us = 0;

    std::int64_t sort_free_us       = 0;
    std::int64_t next_sort_epoch_us = 0;
    bool         epoch_sorted       = false;

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

        sort_stats.sort_epochs   += 1;
        sort_stats.total_sort_us += measured_us;

        const std::int64_t latency_us = (config_.sort_latency_us > 0)
            ? config_.sort_latency_us
            : static_cast<std::int64_t>(measured_us);

        sort_free_us       = epoch_us + latency_us;
        next_sort_epoch_us = epoch_us + std::max(config_.sort_interval_us, latency_us);
        epoch_sorted       = true;
    };

    auto drain_until = [&](std::int64_t limit_us)
    {
        if (pending.empty()) return;

        const bool new_epoch = (config_.sort_interval_us == 0) ||
                               (limit_us >= next_sort_epoch_us);
        if (new_epoch)
            sort_at_epoch(limit_us);

        const int n = static_cast<int>(pending.size());

        if (epoch_sorted)
        {
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
                cc.transmitted_packets += 1;
                cc.transmitted_bytes   += pkt.packet_size_bytes;
                record_queue_delay(stats, pkt, start - pkt.arrival_time_us);
            }

            std::vector<PendingEntry> remaining;
            for (int i = 0; i < n; ++i)
                if (!consumed[i]) remaining.push_back(std::move(pending[i]));
            pending = std::move(remaining);
            epoch_sorted = false;
        }
        else
        {
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
                cc.transmitted_packets += 1;
                cc.transmitted_bytes   += pkt.packet_size_bytes;
                record_queue_delay(stats, pkt, start - pkt.arrival_time_us);
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
        cc.arrived_packets += 1;
        cc.arrived_bytes   += pkt.packet_size_bytes;

        drain_until(pkt.arrival_time_us);

        const bool buffer_full = queued_bytes + pkt.packet_size_bytes > config_.buffer_capacity_bytes;
        const bool sort_ready  = pkt.arrival_time_us >= sort_free_us;

        if (buffer_full && pkt.traffic_class == TrafficClass::Control && sort_ready)
        {
            // Find the worst-priority bulk to evict: last admitted bulk has the
            // highest sequence number = largest sort key = correct eviction target.
            // Scanning backwards is O(n) but fires only when buffer is congested.
            std::size_t worst = pending.size();
            for (std::size_t i = pending.size(); i-- > 0; )
            {
                if (pending[i].packet.traffic_class == TrafficClass::Bulk)
                {
                    worst = i;
                    break;
                }
            }

            if (worst < pending.size())
            {
                queued_bytes -= pending[worst].packet.packet_size_bytes;
                stats.dropped_packets += 1;
                stats.dropped_bytes   += pending[worst].packet.packet_size_bytes;
                auto &ec = class_counters(stats, pending[worst].packet.traffic_class);
                ec.dropped_packets += 1;
                ec.dropped_bytes   += pending[worst].packet.packet_size_bytes;
                pending.erase(pending.begin() + static_cast<std::ptrdiff_t>(worst));
                epoch_sorted = false;

                queued_bytes += pkt.packet_size_bytes;
                pending.push_back({std::move(pkt), sequence++});
            }
            else
            {
                // Queue is all control — no bulk to evict, drop incoming.
                stats.dropped_packets += 1;
                stats.dropped_bytes   += pkt.packet_size_bytes;
                cc.dropped_packets    += 1;
                cc.dropped_bytes      += pkt.packet_size_bytes;
            }
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

    sort_stats.total_sim_wall_us = std::chrono::duration<double, std::micro>(
        std::chrono::steady_clock::now() - sim_wall_start).count();

    return result;
}

} // namespace sim::cpu_priority_queue
