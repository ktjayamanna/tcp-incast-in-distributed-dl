#pragma once

#include "config.hpp"
#include "packet_source.hpp"
#include "types.hpp"

namespace sim::cpu_fifo
{

    class Engine
    {
    public:
        explicit Engine(SimConfig config);

        SimStats run(PacketSource &packet_source);

    private:
        SimConfig config_{};
    };

} // namespace sim::cpu_fifo
