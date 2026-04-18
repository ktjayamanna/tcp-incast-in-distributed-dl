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

// Sort key: smaller value → higher scheduling priority.
//   High 32 bits: (255 - priority_tag) — inverts so control (tag=46) sorts first
//   Low  32 bits: sequence             — FIFO tie-breaking within same priority
inline std::uint64_t make_sort_key(std::uint8_t priority_tag, std::uint32_t seq)
{
    return (static_cast<std::uint64_t>(255u - priority_tag) << 32) | seq;
}

struct PendingEntry { Packet packet; std::uint32_t sequence = 0; };

} // namespace

Engine::Engine(SimConfig config)
    : config_(config), sorter_(kMaxSortCapacity)
{
    validate_config_or_throw(config_);
}

GpuSimStats Engine::run(PacketSource &source)
{
    GpuSimStats   result{};
    SimStats     &stats      = result.sim;
    GpuSortStats &gpu_stats  = result.gpu;

    std::vector<PendingEntry>  pending;
    std::vector<std::uint64_t> sort_keys;
    std::vector<std::uint32_t> sorted_indices;
    std::vector<std::uint64_t> cpu_scratch;

    std::uint64_t queued_bytes  = 0;
    std::uint32_t sequence      = 0;
    std::int64_t  link_free_us  = 0;

    // Sort pending batch on GPU, transmit packets up to limit_us, compact remaining.
    auto drain_until = [&](std::int64_t limit_us)
    {
        if (pending.empty()) return;

        const int n = static_cast<int>(pending.size());
        sort_keys.resize(n);
        sorted_indices.resize(n);
        for (int i = 0; i < n; i++)
            sort_keys[i] = make_sort_key(pending[i].packet.priority_tag, pending[i].sequence);

        // Radix sort on GPU — sorted_indices[0] is the highest-priority packet.
        SortTiming t{};
        sorter_.sort(sort_keys.data(), sorted_indices.data(), n, &t);

        gpu_stats.sort_calls            += 1;
        gpu_stats.total_packets_sorted  += static_cast<std::uint64_t>(n);
        gpu_stats.total_h2d_ms          += t.h2d_ms;
        gpu_stats.total_kernel_ms       += t.kernel_ms;
        gpu_stats.total_d2h_ms          += t.d2h_ms;
        gpu_stats.total_gpu_wall_ms     += t.wall_ms;

        // Record equivalent std::sort time for comparison.
        cpu_scratch.assign(sort_keys.begin(), sort_keys.begin() + n);
        const auto t0 = std::chrono::steady_clock::now();
        std::sort(cpu_scratch.begin(), cpu_scratch.end());
        gpu_stats.total_cpu_sort_ms +=
            std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();

        std::vector<bool> consumed(static_cast<std::size_t>(n), false);
        for (int i = 0; i < n; i++)
        {
            const std::uint32_t idx    = sorted_indices[i];
            const Packet       &pkt    = pending[idx].packet;
            const std::int64_t  start  = std::max(link_free_us, pkt.arrival_time_us);
            if (start > limit_us) break;  // link too busy for all remaining

            link_free_us   = start + transmission_time_us(pkt.packet_size_bytes, config_.link_bandwidth_bps);
            consumed[idx]  = true;
            queued_bytes  -= pkt.packet_size_bytes;

            stats.transmitted_packets += 1;
            stats.transmitted_bytes   += pkt.packet_size_bytes;
            auto &cc = class_counters(stats, pkt.traffic_class);
            cc.transmitted_packets    += 1;
            cc.transmitted_bytes      += pkt.packet_size_bytes;
            record_queue_delay(stats, pkt, start - pkt.arrival_time_us);
        }

        // Remove transmitted entries; remaining wait for next sort.
        std::vector<PendingEntry> remaining;
        for (int i = 0; i < n; i++)
            if (!consumed[i]) remaining.push_back(std::move(pending[i]));
        pending = std::move(remaining);
    };

    while (source.has_next())
    {
        Packet pkt = source.next();
        stats.arrived_packets          += 1;
        stats.arrived_bytes            += pkt.packet_size_bytes;
        auto &cc = class_counters(stats, pkt.traffic_class);
        cc.arrived_packets             += 1;
        cc.arrived_bytes               += pkt.packet_size_bytes;

        drain_until(pkt.arrival_time_us);

        if (queued_bytes + pkt.packet_size_bytes > config_.buffer_capacity_bytes)
        {
            stats.dropped_packets += 1;
            stats.dropped_bytes   += pkt.packet_size_bytes;
            cc.dropped_packets    += 1;
            cc.dropped_bytes      += pkt.packet_size_bytes;
            continue;
        }

        queued_bytes += pkt.packet_size_bytes;
        pending.push_back({std::move(pkt), sequence++});
    }

    drain_until(std::numeric_limits<std::int64_t>::max());
    return result;
}

} // namespace sim::gpu_priority_queue
