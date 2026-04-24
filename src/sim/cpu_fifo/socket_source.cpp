#include "socket_source.hpp"
#include "types.hpp"

#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>

#include <cstring>
#include <iostream>
#include <stdexcept>
#include <vector>

namespace sim::cpu_fifo
{

// Wire format (little-endian):
//   [8B ts_us uint64] [2B ip_len uint16] [ip_len bytes: raw IP packet]
// DSCP is read from IP header byte 1 (ToS field, bits 7-2).
// DSCP 46 = control (Expedited Forwarding), all others = bulk.

static constexpr std::uint8_t  _CTRL_DSCP   = 46;
static constexpr std::size_t   _FRAME_HDR   = 10;  // 8B ts + 2B len
static constexpr std::size_t   _IP_TOS_OFF  =  1;  // ToS byte in IP header
static constexpr std::size_t   _IP_TOT_OFF  =  2;  // total-length field

SocketPacketSource::SocketPacketSource(std::uint16_t port)
    : server_fd_(-1), conn_fd_(-1)
{
    server_fd_ = ::socket(AF_INET, SOCK_STREAM, 0);
    if (server_fd_ < 0)
        throw std::runtime_error("socket() failed");

    int opt = 1;
    ::setsockopt(server_fd_, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));
    ::setsockopt(server_fd_, SOL_SOCKET, SO_REUSEPORT, &opt, sizeof(opt));

    sockaddr_in addr{};
    addr.sin_family      = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_port        = htons(port);

    if (::bind(server_fd_, reinterpret_cast<sockaddr *>(&addr), sizeof(addr)) < 0)
        throw std::runtime_error("bind() failed on port " + std::to_string(port));

    ::listen(server_fd_, 1);
    std::cerr << "Waiting for sender on port " << port << "...\n";

    conn_fd_ = ::accept(server_fd_, nullptr, nullptr);
    if (conn_fd_ < 0)
        throw std::runtime_error("accept() failed");

    std::cerr << "Sender connected.\n";
}

SocketPacketSource::~SocketPacketSource()
{
    if (conn_fd_ >= 0) ::close(conn_fd_);
    if (server_fd_ >= 0) ::close(server_fd_);
}

bool SocketPacketSource::recv_exact(void* buf, std::size_t n) const
{
    auto* p = static_cast<char*>(buf);
    while (n > 0)
    {
        ssize_t got = ::recv(conn_fd_, p, n, 0);
        if (got <= 0) { done_ = true; return false; }
        p += got;
        n -= static_cast<std::size_t>(got);
    }
    return true;
}

bool SocketPacketSource::has_next() const
{
    if (done_)    return false;
    if (has_hdr_) return true;
    // Block until we can read a full frame header; EOF here means stream is done.
    if (!recv_exact(hdr_buf_, _FRAME_HDR)) return false;
    has_hdr_ = true;
    return true;
}

Packet SocketPacketSource::next()
{
    if (!has_next())
        throw std::runtime_error("next() called with no data available");

    std::uint64_t ts_us;
    std::uint16_t ip_len;
    std::memcpy(&ts_us,  hdr_buf_,     sizeof(ts_us));
    std::memcpy(&ip_len, hdr_buf_ + 8, sizeof(ip_len));
    has_hdr_ = false;

    // Read raw IP packet
    std::vector<std::uint8_t> ip(ip_len);
    if (!recv_exact(ip.data(), ip_len))
        throw std::runtime_error("truncated IP packet in stream");

    // Parse IP header fields directly — same as a real queue would
    const std::uint8_t  dscp         = ip[_IP_TOS_OFF] >> 2;
    const std::uint16_t ip_total     = static_cast<std::uint16_t>(
                                           (ip[_IP_TOT_OFF] << 8) | ip[_IP_TOT_OFF + 1]);
    const TrafficClass  tc           = (dscp == _CTRL_DSCP)
                                           ? TrafficClass::Control
                                           : TrafficClass::Bulk;

    Packet pkt;
    pkt.arrival_time_us   = static_cast<std::int64_t>(ts_us);
    pkt.packet_size_bytes = ip_total;
    pkt.traffic_class     = tc;
    pkt.priority_tag      = dscp;
    return pkt;
}

} // namespace sim::cpu_fifo
