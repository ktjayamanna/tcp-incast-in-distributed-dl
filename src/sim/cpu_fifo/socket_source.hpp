#pragma once

#include "packet_source.hpp"

#include <cstdint>
#include <string>

namespace sim::cpu_fifo
{

class SocketPacketSource : public PacketSource
{
public:
    explicit SocketPacketSource(std::uint16_t port);
    ~SocketPacketSource();

    bool has_next() const override;
    Packet next() override;

private:
    int server_fd_;
    int conn_fd_;
    mutable std::string recv_buf_;
    mutable bool done_ = false;

    bool ensure_line() const;
};

} // namespace sim::cpu_fifo
