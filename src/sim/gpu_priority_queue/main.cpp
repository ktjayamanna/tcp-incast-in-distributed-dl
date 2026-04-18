#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>

#include "../main_helpers.hpp"
#include "../cpu_fifo/socket_source.hpp"
#include "../cpu_fifo/trace_csv.hpp"
#include "engine.hpp"

int main(int argc, char **argv)
{
    try
    {
        std::string   input_path;
        std::uint16_t socket_port = 0;
        sim::gpu_priority_queue::SimConfig config{};

        for (int i = 1; i < argc; ++i)
        {
            const std::string arg = argv[i];
            if (arg == "--input" && i + 1 < argc)
                input_path = argv[++i];
            else if (arg == "--socket" && i + 1 < argc)
                socket_port = static_cast<std::uint16_t>(sim::parse_u64(argv[++i], "--socket"));
            else if (arg == "--link-bps" && i + 1 < argc)
                config.link_bandwidth_bps = sim::parse_u64(argv[++i], "--link-bps");
            else if (arg == "--buffer-bytes" && i + 1 < argc)
                config.buffer_capacity_bytes = sim::parse_u64(argv[++i], "--buffer-bytes");
            else if (arg == "--help" || arg == "-h")
            {
                std::cerr << "Usage: " << argv[0]
                          << " (--input <trace.csv> | --socket <port>)"
                          << " [--link-bps N] [--buffer-bytes N]\n";
                return 0;
            }
        }

        if (input_path.empty() && socket_port == 0)
            throw std::invalid_argument("--input or --socket is required");

        std::unique_ptr<sim::cpu_fifo::PacketSource> source;
        if (socket_port)
            source = std::make_unique<sim::cpu_fifo::SocketPacketSource>(socket_port);
        else
            source = std::make_unique<sim::cpu_fifo::trace_csv::CsvPacketSource>(input_path);

        sim::gpu_priority_queue::Engine  engine(config);
        const auto                       result = engine.run(*source);
        sim::print_stats(result.sim);

        const auto &gpu     = result.gpu;
        const double avg_n  = gpu.sort_calls ? static_cast<double>(gpu.total_packets_sorted) / gpu.sort_calls : 0.0;
        const double speedup = gpu.total_gpu_wall_ms > 0.0 ? gpu.total_cpu_sort_ms / gpu.total_gpu_wall_ms : 0.0;
        std::cout << "gpu_sort_calls="    << gpu.sort_calls            << '\n';
        std::cout << "gpu_avg_batch="     << avg_n                     << '\n';
        std::cout << "gpu_h2d_ms="        << gpu.total_h2d_ms          << '\n';
        std::cout << "gpu_kernel_ms="     << gpu.total_kernel_ms       << '\n';
        std::cout << "gpu_d2h_ms="        << gpu.total_d2h_ms          << '\n';
        std::cout << "gpu_wall_ms="       << gpu.total_gpu_wall_ms     << '\n';
        std::cout << "cpu_sort_ms="       << gpu.total_cpu_sort_ms     << '\n';
        std::cout << "gpu_vs_cpu_speedup=" << speedup                  << '\n';
        return 0;
    }
    catch (const std::exception &ex)
    {
        std::cerr << "error: " << ex.what() << '\n';
        return 1;
    }
}
