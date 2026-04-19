#pragma once

#include <cstdint>
#include <stdexcept>

namespace sim::cpu_fifo
{

    struct SimConfig
    {
        std::uint64_t link_bandwidth_bps    = 40'000'000'000ULL;
        std::uint64_t buffer_capacity_bytes = 256 * 1024ULL;
        // sort_latency_us: simulation-time cost of one sort batch on target hardware.
        //   CPU PQ (e.g., switch ARM CPU): ~2000 µs for 30K packets
        //   GPU PQ (e.g., GPU co-processor, ~4× faster): ~500 µs
        //   0 = instant sort (default, backward compatible)
        std::int64_t  sort_latency_us       = 0;
        // sort_interval_us: how often a new sort batch is triggered in simulation time.
        //   0 = sort on every packet arrival (legacy behaviour)
        //   Set to wave_interval_us for a realistic once-per-wave batching model.
        std::int64_t  sort_interval_us      = 0;
    };

    inline void validate_config_or_throw(const SimConfig &config)
    {
        if (config.link_bandwidth_bps == 0)
            throw std::invalid_argument("link_bandwidth_bps must be > 0");
        if (config.buffer_capacity_bytes == 0)
            throw std::invalid_argument("buffer_capacity_bytes must be > 0");
    }

} // namespace sim::cpu_fifo
