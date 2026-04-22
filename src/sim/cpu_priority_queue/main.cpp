#include <iostream>
#include <memory>
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
        sim::cpu_priority_queue::SimConfig config{};

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

        sim::cpu_fifo::SocketPacketSource    source(socket_port);
        sim::cpu_priority_queue::Engine      engine(config);
        const auto                           result = engine.run(source);
        sim::print_stats(result.sim);

        const auto &s = result.sort;
        const double avg_us = s.sort_epochs ? s.total_sort_us / s.sort_epochs : 0.0;
        std::cout << "sort_epochs="         << s.sort_epochs << '\n';
        std::cout << "sort_latency_avg_us=" << avg_us        << '\n';
        return 0;
    }
    catch (const std::exception &ex)
    {
        std::cerr << "error: " << ex.what() << '\n';
        return 1;
    }
}
