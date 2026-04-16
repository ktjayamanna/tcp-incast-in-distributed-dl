#include <cstdint>
#include <exception>
#include <iostream>
#include <numeric>
#include <stdexcept>
#include <string>
#include <vector>

#include "config.hpp"
#include "engine.hpp"
#include "trace_csv.hpp"

namespace
{

    void print_usage(const char *program_name)
    {
        std::cerr
            << "Usage: " << program_name << " --input <trace.csv> [--link-bps <bits_per_second>]"
            << " [--buffer-bytes <bytes>]\n";
    }

    std::uint64_t parse_u64_arg(const std::string &value, const char *flag_name)
    {
        std::size_t pos = 0;
        const unsigned long long parsed = std::stoull(value, &pos, 10);
        if (pos != value.size())
        {
            throw std::invalid_argument(std::string(flag_name) + " must be an unsigned integer");
        }
        return static_cast<std::uint64_t>(parsed);
    }

    double average_or_zero(const std::vector<std::int64_t> &values)
    {
        if (values.empty())
        {
            return 0.0;
        }
        const auto total = std::accumulate(values.begin(), values.end(), 0.0);
        return total / static_cast<double>(values.size());
    }

} // namespace

int main(int argc, char **argv)
{
    try
    {
        std::string input_path;
        sim::cpu_fifo::SimConfig config{};

        for (int i = 1; i < argc; ++i)
        {
            const std::string arg = argv[i];
            if (arg == "--input")
            {
                if (i + 1 >= argc)
                {
                    throw std::invalid_argument("--input requires a value");
                }
                input_path = argv[++i];
            }
            else if (arg == "--link-bps")
            {
                if (i + 1 >= argc)
                {
                    throw std::invalid_argument("--link-bps requires a value");
                }
                config.link_bandwidth_bps = parse_u64_arg(argv[++i], "--link-bps");
            }
            else if (arg == "--buffer-bytes")
            {
                if (i + 1 >= argc)
                {
                    throw std::invalid_argument("--buffer-bytes requires a value");
                }
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

        if (input_path.empty())
        {
            throw std::invalid_argument("--input is required");
        }

        sim::cpu_fifo::trace_csv::CsvPacketSource packet_source(input_path);
        sim::cpu_fifo::Engine engine(config);
        const sim::cpu_fifo::SimStats stats = engine.run(packet_source);

        std::cout << "arrived_packets=" << stats.arrived_packets << '\n';
        std::cout << "dropped_packets=" << stats.dropped_packets << '\n';
        std::cout << "transmitted_packets=" << stats.transmitted_packets << '\n';
        std::cout << "arrived_bytes=" << stats.arrived_bytes << '\n';
        std::cout << "dropped_bytes=" << stats.dropped_bytes << '\n';
        std::cout << "transmitted_bytes=" << stats.transmitted_bytes << '\n';
        std::cout << "avg_queue_delay_us_all=" << average_or_zero(stats.queue_delay_us_all) << '\n';
        std::cout << "avg_queue_delay_us_control=" << average_or_zero(stats.queue_delay_us_control) << '\n';
        std::cout << "avg_queue_delay_us_bulk=" << average_or_zero(stats.queue_delay_us_bulk) << '\n';
        return 0;
    }
    catch (const std::exception &ex)
    {
        print_usage(argv[0]);
        std::cerr << "error: " << ex.what() << '\n';
        return 1;
    }
}
