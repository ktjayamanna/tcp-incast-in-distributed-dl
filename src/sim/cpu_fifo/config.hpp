#pragma once

#include <cstdint>
#include <stdexcept>

namespace sim::cpu_fifo
{

    struct SimConfig
    {
        std::uint64_t link_bandwidth_bps = 40'000'000'000ULL;
        std::uint64_t buffer_capacity_bytes = 256 * 1024ULL;
    };

    inline void validate_config_or_throw(const SimConfig &config)
    {
        if (config.link_bandwidth_bps == 0)
        {
            throw std::invalid_argument("link_bandwidth_bps must be > 0");
        }
        if (config.buffer_capacity_bytes == 0)
        {
            throw std::invalid_argument("buffer_capacity_bytes must be > 0");
        }
    }

} // namespace sim::cpu_fifo
