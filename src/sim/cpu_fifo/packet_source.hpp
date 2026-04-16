#pragma once

#include "types.hpp"

namespace sim::cpu_fifo
{

    class PacketSource
    {
    public:
        virtual ~PacketSource() = default;

        virtual bool has_next() const = 0;
        virtual Packet next() = 0;
    };

} // namespace sim::cpu_fifo
