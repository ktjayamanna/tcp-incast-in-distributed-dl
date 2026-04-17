#include "engine.hpp"

#include <algorithm>
#include <cstdint>
#include <limits>
#include <queue>
#include <stdexcept>
#include <utility>
#include <vector>

namespace sim::cpu_priority_queue
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

        struct QueuedPacket
        {
            Packet packet;
            std::uint64_t sequence = 0;
        };

        struct QueuedPacketCompare
        {
            bool operator()(const QueuedPacket &lhs, const QueuedPacket &rhs) const
            {
                if (lhs.packet.priority_tag != rhs.packet.priority_tag)
                {
                    return lhs.packet.priority_tag < rhs.packet.priority_tag;
                }
                return lhs.sequence > rhs.sequence;
            }
        };

        using PendingQueue = std::priority_queue<
            QueuedPacket,
            std::vector<QueuedPacket>,
            QueuedPacketCompare>;

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

        void drain_until(
            PendingQueue &pending_packets,
            std::uint64_t &queued_bytes,
            std::int64_t limit_time_us,
            std::int64_t &link_free_time_us,
            const SimConfig &config,
            SimStats &stats)
        {
            while (!pending_packets.empty())
            {
                const QueuedPacket next = pending_packets.top();
                const std::int64_t start_time_us =
                    std::max(link_free_time_us, next.packet.arrival_time_us);
                if (start_time_us > limit_time_us)
                {
                    break;
                }

                pending_packets.pop();
                queued_bytes -= next.packet.packet_size_bytes;

                const std::int64_t queue_delay_us = start_time_us - next.packet.arrival_time_us;
                const std::int64_t departure_time_us =
                    start_time_us + transmission_time_us(next.packet.packet_size_bytes, config.link_bandwidth_bps);

                link_free_time_us = departure_time_us;
                stats.transmitted_packets += 1;
                stats.transmitted_bytes += next.packet.packet_size_bytes;
                TrafficClassCounters &packet_class_stats = class_counters(stats, next.packet.traffic_class);
                packet_class_stats.transmitted_packets += 1;
                packet_class_stats.transmitted_bytes += next.packet.packet_size_bytes;
                record_queue_delay(stats, next.packet, queue_delay_us);
            }
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
        PendingQueue pending_packets;
        std::uint64_t queued_bytes = 0;
        std::uint64_t sequence = 0;
        std::int64_t link_free_time_us = 0;

        while (packet_source.has_next())
        {
            Packet packet = packet_source.next();
            stats.arrived_packets += 1;
            stats.arrived_bytes += packet.packet_size_bytes;
            TrafficClassCounters &packet_class_stats = class_counters(stats, packet.traffic_class);
            packet_class_stats.arrived_packets += 1;
            packet_class_stats.arrived_bytes += packet.packet_size_bytes;

            drain_until(
                pending_packets,
                queued_bytes,
                packet.arrival_time_us,
                link_free_time_us,
                config_,
                stats);

            if (queued_bytes + packet.packet_size_bytes > config_.buffer_capacity_bytes)
            {
                stats.dropped_packets += 1;
                stats.dropped_bytes += packet.packet_size_bytes;
                packet_class_stats.dropped_packets += 1;
                packet_class_stats.dropped_bytes += packet.packet_size_bytes;
                continue;
            }

            QueuedPacket queued_packet{};
            queued_packet.packet = std::move(packet);
            queued_packet.sequence = sequence++;
            queued_bytes += queued_packet.packet.packet_size_bytes;
            pending_packets.push(std::move(queued_packet));
        }

        drain_until(
            pending_packets,
            queued_bytes,
            std::numeric_limits<std::int64_t>::max(),
            link_free_time_us,
            config_,
            stats);

        return stats;
    }

} // namespace sim::cpu_priority_queue
