#pragma once

#include "packet_source.hpp"

#include <cstdint>
#include <vector>

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
    int  server_fd_;
    int  conn_fd_;
    mutable bool     done_    = false;
    mutable bool     has_hdr_ = false;
    mutable std::uint8_t hdr_buf_[10]{};  // buffered frame header (ts + len)

    bool recv_exact(void* buf, std::size_t n) const;
};

} // namespace sim::cpu_fifo
