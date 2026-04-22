#include "engine.hpp"
#include "../cpu_fifo/engine_utils.hpp"

#include <algorithm>
#include <chrono>
#include <limits>
#include <vector>

using sim::cpu_fifo::class_counters;
using sim::cpu_fifo::transmission_time_us;
using sim::cpu_fifo::record_queue_delay;

namespace sim::gpu_priority_queue
{

namespace
{

inline std::uint64_t make_sort_key(std::uint8_t priority_tag, std::uint32_t seq)
{
    return (static_cast<std::uint64_t>(255u - priority_tag) << 32) | seq;
}

struct PendingEntry { Packet packet; std::uint32_t sequence = 0; };

} // anonymous namespace

Engine::Engine(SimConfig config)
    : config_(config), sorter_(kMaxSortCapacity)
{
    validate_config_or_throw(config_);
}

GpuSimStats Engine::run(PacketSource &source)
{
    const auto sim_wall_start = std::chrono::steady_clock::now();

    GpuSimStats   result{};
    SimStats     &stats     = result.sim;
    GpuSortStats &gpu_stats = result.gpu;

    std::vector<PendingEntry>  pending;
    std::vector<std::uint64_t> sort_keys;
    std::vector<std::uint32_t> sorted_indices;

    std::uint64_t queued_bytes = 0;
    std::uint32_t sequence     = 0;
    std::int64_t  link_free_us = 0;

    std::int64_t  sort_free_us       = 0;
    std::int64_t  next_sort_epoch_us = 0;
    std::uint32_t evict_candidate_seq = std::numeric_limits<std::uint32_t>::max();
    bool          epoch_sorted        = false;
    int           pipeline_slot       = 0;  // cycles through 0, 1, 2

    // sort_at_epoch: GPU radix sort via the async pipeline API.
    // submit_async queues H2D + kernel + D2H onto the slot's CUDA stream and
    // returns immediately.  Cycling three slots lets consecutive epoch sorts
    // overlap: while slot N's kernel runs, slot N+1's H2D can proceed on the
    // copy engine concurrently.  collect_with_timing waits for completion and
    // returns per-phase GPU timings measured by CUDA events.
    //
    // Blind window = kernel time only.  H2D and D2H are modelled as running
    // on dedicated DMA hardware that does not stall packet processing, so
    // only the sort compute phase counts as "decision blackout".
    auto sort_at_epoch = [&](std::int64_t epoch_us)
    {
        const int n = static_cast<int>(pending.size());
        sort_keys.resize(static_cast<std::size_t>(n));
        sorted_indices.resize(static_cast<std::size_t>(n));
        for (int i = 0; i < n; ++i)
            sort_keys[i] = make_sort_key(pending[i].packet.priority_tag, pending[i].sequence);

        SlotTiming t{};
        sorter_.submit_async(pipeline_slot, sort_keys.data(), n);
        sorter_.collect_with_timing(pipeline_slot, sorted_indices.data(), n, t);
        pipeline_slot = (pipeline_slot + 1) % kPipelineDepth;

        gpu_stats.sort_epochs           += 1;
        gpu_stats.total_packets_sorted  += static_cast<std::uint64_t>(n);
        gpu_stats.total_h2d_ms          += t.h2d_ms;
        gpu_stats.total_kernel_ms       += t.kernel_ms;
        gpu_stats.total_d2h_ms          += t.d2h_ms;
        gpu_stats.total_gpu_wall_ms     += t.wall_ms;

        const double kernel_us = static_cast<double>(t.kernel_ms) * 1000.0;
        gpu_stats.total_epoch_kernel_us += kernel_us;

        const std::int64_t latency_us = (config_.sort_latency_us > 0)
            ? config_.sort_latency_us
            : static_cast<std::int64_t>(kernel_us);

        sort_free_us       = epoch_us + latency_us;
        next_sort_epoch_us = epoch_us + std::max(config_.sort_interval_us, latency_us);

        evict_candidate_seq = std::numeric_limits<std::uint32_t>::max();
        for (int i = n - 1; i >= 0; --i)
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

        if (buffer_full && pkt.traffic_class == TrafficClass::Control && sort_ready)
        {
            // Try sort-based candidate (worst-priority bulk from last epoch).
            // If stale or absent, fall back to scanning for the latest-admitted
            // bulk packet — identical to CPU PQ behaviour.
            std::size_t evict_idx = pending.size(); // sentinel = not found

            if (evict_candidate_seq != std::numeric_limits<std::uint32_t>::max())
            {
                auto it = std::find_if(pending.begin(), pending.end(),
                    [s = evict_candidate_seq](const PendingEntry &e){ return e.sequence == s; });
                if (it != pending.end())
                    evict_idx = static_cast<std::size_t>(it - pending.begin());
                evict_candidate_seq = std::numeric_limits<std::uint32_t>::max();
            }

            if (evict_idx == pending.size())
            {
                for (std::size_t i = pending.size(); i-- > 0; )
                {
                    if (pending[i].packet.traffic_class == TrafficClass::Bulk)
                    {
                        evict_idx = i;
                        break;
                    }
                }
            }

            if (evict_idx < pending.size())
            {
                queued_bytes -= pending[evict_idx].packet.packet_size_bytes;
                stats.dropped_packets += 1;
                stats.dropped_bytes   += pending[evict_idx].packet.packet_size_bytes;
                auto &ec = class_counters(stats, pending[evict_idx].packet.traffic_class);
                ec.dropped_packets    += 1;
                ec.dropped_bytes      += pending[evict_idx].packet.packet_size_bytes;
                pending.erase(pending.begin() + static_cast<std::ptrdiff_t>(evict_idx));
                epoch_sorted = false;

                queued_bytes += pkt.packet_size_bytes;
                pending.push_back({std::move(pkt), sequence++});
            }
            else
            {
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
            const std::uint32_t seq = sequence++;
            pending.push_back({std::move(pkt), seq});
            if (pending.back().packet.traffic_class == TrafficClass::Bulk)
                evict_candidate_seq = seq;
        }
    }

    drain_until(std::numeric_limits<std::int64_t>::max());

    gpu_stats.total_sim_wall_ms = std::chrono::duration<double, std::milli>(
        std::chrono::steady_clock::now() - sim_wall_start).count();

    return result;
}

} // namespace sim::gpu_priority_queue
