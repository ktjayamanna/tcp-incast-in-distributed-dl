#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>

#include "../main_helpers.hpp"
#include "config.hpp"
#include "engine.hpp"
#include "socket_source.hpp"

int main(int argc, char **argv)
{
    try
    {
        std::uint16_t socket_port = 0;
        sim::cpu_fifo::SimConfig config{};

        for (int i = 1; i < argc; ++i)
        {
            const std::string arg = argv[i];
            if (arg == "--socket" && i + 1 < argc)
                socket_port = static_cast<std::uint16_t>(sim::parse_u64(argv[++i], "--socket"));
            else if (arg == "--link-bps" && i + 1 < argc)
                config.link_bandwidth_bps = sim::parse_u64(argv[++i], "--link-bps");
            else if (arg == "--buffer-bytes" && i + 1 < argc)
                config.buffer_capacity_bytes = sim::parse_u64(argv[++i], "--buffer-bytes");
            else if (arg == "--help" || arg == "-h")
            {
                std::cerr << "Usage: " << argv[0]
                          << " --socket <port>"
                          << " [--link-bps N] [--buffer-bytes N]\n";
                return 0;
            }
        }

        if (socket_port == 0)
            throw std::invalid_argument("--socket <port> is required");

        sim::cpu_fifo::SocketPacketSource source(socket_port);
        sim::cpu_fifo::Engine             engine(config);
        const auto                        stats = engine.run(source);
        sim::print_stats(stats);
        return 0;
    }
    catch (const std::exception &ex)
    {
        std::cerr << "error: " << ex.what() << '\n';
        return 1;
    }
}
