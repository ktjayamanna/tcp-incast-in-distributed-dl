#include <cstdint>
#include <exception>
#include <iostream>
#include <memory>
#include <numeric>
#include <stdexcept>
#include <string>
#include <vector>

#include "../cpu_fifo/socket_source.hpp"
#include "../cpu_fifo/trace_csv.hpp"
#include "config.hpp"
#include "engine.hpp"

namespace
{

void print_usage(const char* program_name)
{
    std::cerr
        << "Usage: " << program_name
        << " (--input <trace.csv> | --socket <port>)"
        << " [--link-bps <bits_per_second>] [--buffer-bytes <bytes>]\n";
}

std::uint64_t parse_u64_arg(const std::string& value, const char* flag_name)
{
    std::size_t pos = 0;
    const unsigned long long parsed = std::stoull(value, &pos, 10);
    if (pos != value.size())
        throw std::invalid_argument(std::string(flag_name) + " must be an unsigned integer");
    return static_cast<std::uint64_t>(parsed);
}

double average_or_zero(const std::vector<std::int64_t>& values)
{
    if (values.empty()) return 0.0;
    const auto total = std::accumulate(values.begin(), values.end(), 0.0);
    return total / static_cast<double>(values.size());
}

double ratio_or_zero(std::uint64_t numerator, std::uint64_t denominator)
{
    if (denominator == 0) return 0.0;
    return static_cast<double>(numerator) / static_cast<double>(denominator);
}

void print_class_summary(
    const char*                              label,
    const sim::cpu_fifo::TrafficClassCounters& counters)
{
    std::cout << label << "_arrived_packets="     << counters.arrived_packets     << '\n';
    std::cout << label << "_dropped_packets="     << counters.dropped_packets     << '\n';
    std::cout << label << "_transmitted_packets=" << counters.transmitted_packets << '\n';
    std::cout << label << "_arrived_bytes="       << counters.arrived_bytes       << '\n';
    std::cout << label << "_dropped_bytes="       << counters.dropped_bytes       << '\n';
    std::cout << label << "_transmitted_bytes="   << counters.transmitted_bytes   << '\n';
    std::cout << label << "_drop_packet_ratio="
              << ratio_or_zero(counters.dropped_packets,     counters.arrived_packets) << '\n';
    std::cout << label << "_transmit_packet_ratio="
              << ratio_or_zero(counters.transmitted_packets, counters.arrived_packets) << '\n';
    std::cout << label << "_drop_byte_ratio="
              << ratio_or_zero(counters.dropped_bytes,       counters.arrived_bytes)   << '\n';
    std::cout << label << "_transmit_byte_ratio="
              << ratio_or_zero(counters.transmitted_bytes,   counters.arrived_bytes)   << '\n';
}

} // namespace

int main(int argc, char** argv)
{
    try
    {
        std::string input_path;
        std::uint16_t socket_port = 0;
        sim::gpu_priority_queue::SimConfig config{};

        for (int i = 1; i < argc; ++i)
        {
            const std::string arg = argv[i];
            if (arg == "--input")
            {
                if (i + 1 >= argc) throw std::invalid_argument("--input requires a value");
                input_path = argv[++i];
            }
            else if (arg == "--socket")
            {
                if (i + 1 >= argc) throw std::invalid_argument("--socket requires a port number");
                socket_port = static_cast<std::uint16_t>(parse_u64_arg(argv[++i], "--socket"));
            }
            else if (arg == "--link-bps")
            {
                if (i + 1 >= argc) throw std::invalid_argument("--link-bps requires a value");
                config.link_bandwidth_bps = parse_u64_arg(argv[++i], "--link-bps");
            }
            else if (arg == "--buffer-bytes")
            {
                if (i + 1 >= argc) throw std::invalid_argument("--buffer-bytes requires a value");
                config.buffer_capacity_bytes = parse_u64_arg(argv[++i], "--buffer-bytes");
            }
            else if (arg == "--help" || arg == "-h")
            {
                print_usage(argv[0]);
                return 0;
            }
            else
            {
                throw std::invalid_argument("Unknown argument: " + arg);
            }
        }

        if (input_path.empty() && socket_port == 0)
            throw std::invalid_argument("--input or --socket is required");

        std::unique_ptr<sim::cpu_fifo::PacketSource> source;
        if (socket_port != 0)
            source = std::make_unique<sim::cpu_fifo::SocketPacketSource>(socket_port);
        else
            source = std::make_unique<sim::cpu_fifo::trace_csv::CsvPacketSource>(input_path);

        sim::gpu_priority_queue::Engine            engine(config);
        const sim::gpu_priority_queue::GpuSimStats result = engine.run(*source);
        const auto& stats     = result.sim;
        const auto& gpu       = result.gpu;

        std::cout << "arrived_packets="    << stats.arrived_packets    << '\n';
        std::cout << "dropped_packets="    << stats.dropped_packets    << '\n';
        std::cout << "transmitted_packets=" << stats.transmitted_packets << '\n';
        std::cout << "arrived_bytes="      << stats.arrived_bytes      << '\n';
        std::cout << "dropped_bytes="      << stats.dropped_bytes      << '\n';
        std::cout << "transmitted_bytes="  << stats.transmitted_bytes  << '\n';
        std::cout << "drop_packet_ratio="
                  << ratio_or_zero(stats.dropped_packets,     stats.arrived_packets) << '\n';
        std::cout << "transmit_packet_ratio="
                  << ratio_or_zero(stats.transmitted_packets, stats.arrived_packets) << '\n';
        std::cout << "drop_byte_ratio="
                  << ratio_or_zero(stats.dropped_bytes,       stats.arrived_bytes)   << '\n';
        std::cout << "transmit_byte_ratio="
                  << ratio_or_zero(stats.transmitted_bytes,   stats.arrived_bytes)   << '\n';
        print_class_summary("control", stats.control);
        print_class_summary("bulk",    stats.bulk);
        std::cout << "avg_queue_delay_us_all="
                  << average_or_zero(stats.queue_delay_us_all) << '\n';
        std::cout << "avg_queue_delay_us_control="
                  << average_or_zero(stats.queue_delay_us_control) << '\n';
        std::cout << "avg_queue_delay_us_bulk="
                  << average_or_zero(stats.queue_delay_us_bulk) << '\n';

        // GPU timing summary
        const double avg_batch =
            gpu.sort_calls ? static_cast<double>(gpu.total_packets_sorted) / gpu.sort_calls : 0.0;
        const double speedup =
            gpu.total_gpu_wall_ms > 0.0 ? gpu.total_cpu_sort_ms / gpu.total_gpu_wall_ms : 0.0;
        std::cout << "gpu_sort_calls="          << gpu.sort_calls               << '\n';
        std::cout << "gpu_total_packets_sorted=" << gpu.total_packets_sorted     << '\n';
        std::cout << "gpu_avg_batch_size="       << avg_batch                   << '\n';
        std::cout << "gpu_total_h2d_ms="         << gpu.total_h2d_ms            << '\n';
        std::cout << "gpu_total_kernel_ms="      << gpu.total_kernel_ms         << '\n';
        std::cout << "gpu_total_d2h_ms="         << gpu.total_d2h_ms            << '\n';
        std::cout << "gpu_total_wall_ms="        << gpu.total_gpu_wall_ms       << '\n';
        std::cout << "cpu_sort_equivalent_ms="   << gpu.total_cpu_sort_ms       << '\n';
        std::cout << "gpu_vs_cpu_speedup="       << speedup                     << '\n';
        return 0;
    }
    catch (const std::exception& ex)
    {
        print_usage(argv[0]);
        std::cerr << "error: " << ex.what() << '\n';
        return 1;
    }
}
