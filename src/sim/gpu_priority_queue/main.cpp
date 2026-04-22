#include <iostream>
#include <stdexcept>
#include <string>

#include "../main_helpers.hpp"
#include "../cpu_fifo/socket_source.hpp"
#include "engine.hpp"

int main(int argc, char **argv)
{
    try
    {
        std::uint16_t socket_port = 0;
        sim::gpu_priority_queue::SimConfig config{};

        for (int i = 1; i < argc; ++i)
        {
            const std::string arg = argv[i];
            if (arg == "--socket" && i + 1 < argc)
                socket_port = static_cast<std::uint16_t>(sim::parse_u64(argv[++i], "--socket"));
            else if (arg == "--link-bps" && i + 1 < argc)
                config.link_bandwidth_bps = sim::parse_u64(argv[++i], "--link-bps");
            else if (arg == "--buffer-bytes" && i + 1 < argc)
                config.buffer_capacity_bytes = sim::parse_u64(argv[++i], "--buffer-bytes");
            else if (arg == "--sort-latency-us" && i + 1 < argc)
                config.sort_latency_us = static_cast<std::int64_t>(sim::parse_u64(argv[++i], "--sort-latency-us"));
            else if (arg == "--sort-interval-us" && i + 1 < argc)
                config.sort_interval_us = static_cast<std::int64_t>(sim::parse_u64(argv[++i], "--sort-interval-us"));
            else if (arg == "--help" || arg == "-h")
            {
                std::cerr << "Usage: " << argv[0]
                          << " --socket <port>"
                          << " [--link-bps N] [--buffer-bytes N]"
                          << " [--sort-latency-us N] [--sort-interval-us N]\n";
                return 0;
            }
        }

        if (socket_port == 0)
            throw std::invalid_argument("--socket <port> is required");

        sim::cpu_fifo::SocketPacketSource  source(socket_port);
        sim::gpu_priority_queue::Engine    engine(config);
        const auto                         result = engine.run(source);
        sim::print_stats(result.sim);

        const auto  &gpu                = result.gpu;
        const double avg_n              = gpu.sort_calls ? static_cast<double>(gpu.total_packets_sorted) / gpu.sort_calls : 0.0;
        const double speedup            = gpu.total_gpu_wall_ms > 0.0 ? gpu.total_cpu_sort_ms / gpu.total_gpu_wall_ms : 0.0;
        const double avg_kernel_us      = gpu.sort_epochs ? gpu.total_epoch_kernel_us / gpu.sort_epochs : 0.0;
        const double pipeline_eff       = gpu.total_gpu_wall_ms > 0.0
            ? (gpu.total_h2d_ms + gpu.total_kernel_ms + gpu.total_d2h_ms) / gpu.total_gpu_wall_ms : 0.0;
        const double gpu_kernel_util    = gpu.total_gpu_wall_ms > 0.0
            ? gpu.total_kernel_ms / gpu.total_gpu_wall_ms * 100.0 : 0.0;
        const double gpu_sort_active    = gpu.total_sim_wall_ms > 0.0
            ? gpu.total_gpu_wall_ms / gpu.total_sim_wall_ms * 100.0 : 0.0;
        std::cout << "gpu_sort_calls="         << gpu.sort_calls        << '\n';
        std::cout << "gpu_avg_batch="          << avg_n                 << '\n';
        std::cout << "gpu_h2d_ms="             << gpu.total_h2d_ms      << '\n';
        std::cout << "gpu_kernel_ms="          << gpu.total_kernel_ms   << '\n';
        std::cout << "gpu_d2h_ms="             << gpu.total_d2h_ms      << '\n';
        std::cout << "gpu_wall_ms="            << gpu.total_gpu_wall_ms << '\n';
        std::cout << "cpu_sort_ms="            << gpu.total_cpu_sort_ms << '\n';
        std::cout << "gpu_vs_cpu_speedup="     << speedup               << '\n';
        std::cout << "sort_epochs="            << gpu.sort_epochs       << '\n';
        std::cout << "sort_latency_avg_us="    << avg_kernel_us         << '\n';
        std::cout << "pipeline_efficiency="    << pipeline_eff          << '\n';
        std::cout << "gpu_kernel_util_pct="    << gpu_kernel_util       << '\n';
        std::cout << "gpu_sort_active_pct="    << gpu_sort_active       << '\n';
        std::cout << "sim_wall_ms="            << gpu.total_sim_wall_ms << '\n';
        return 0;
    }
    catch (const std::exception &ex)
    {
        std::cerr << "error: " << ex.what() << '\n';
        return 1;
    }
}
