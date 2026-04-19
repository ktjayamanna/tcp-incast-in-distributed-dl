#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>

#include "../main_helpers.hpp"
#include "config.hpp"
#include "engine.hpp"
#include "socket_source.hpp"
#include "trace_csv.hpp"

int main(int argc, char **argv)
{
    try
    {
        std::string   input_path;
        std::uint16_t socket_port = 0;
        sim::cpu_fifo::SimConfig config{};

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
            else if (arg == "--sort-latency-us" && i + 1 < argc)
                config.sort_latency_us = static_cast<std::int64_t>(sim::parse_u64(argv[++i], "--sort-latency-us"));
            else if (arg == "--sort-interval-us" && i + 1 < argc)
                config.sort_interval_us = static_cast<std::int64_t>(sim::parse_u64(argv[++i], "--sort-interval-us"));
            else if (arg == "--help" || arg == "-h")
            {
                std::cerr << "Usage: " << argv[0]
                          << " (--input <trace.csv> | --socket <port>)"
                          << " [--link-bps N] [--buffer-bytes N]"
                          << " [--sort-latency-us N] [--sort-interval-us N]\n";
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

        sim::cpu_fifo::Engine  engine(config);
        const auto             stats = engine.run(*source);
        sim::print_stats(stats);
        return 0;
    }
    catch (const std::exception &ex)
    {
        std::cerr << "error: " << ex.what() << '\n';
        return 1;
    }
}
