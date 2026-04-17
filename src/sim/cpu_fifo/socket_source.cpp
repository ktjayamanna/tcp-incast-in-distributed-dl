#include "socket_source.hpp"
#include "types.hpp"

#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>

#include <chrono>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <string>

namespace sim::cpu_fifo
{

namespace
{

std::int64_t now_us()
{
    return std::chrono::duration_cast<std::chrono::microseconds>(
               std::chrono::steady_clock::now().time_since_epoch())
        .count();
}

} // namespace

SocketPacketSource::SocketPacketSource(std::uint16_t port)
    : server_fd_(-1), conn_fd_(-1)
{
    server_fd_ = ::socket(AF_INET, SOCK_STREAM, 0);
    if (server_fd_ < 0)
        throw std::runtime_error("socket() failed");

    int opt = 1;
    ::setsockopt(server_fd_, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_port = htons(port);

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
    if (conn_fd_ >= 0)
        ::close(conn_fd_);
    if (server_fd_ >= 0)
        ::close(server_fd_);
}

bool SocketPacketSource::ensure_line() const
{
    while (recv_buf_.find('\n') == std::string::npos)
    {
        if (done_)
            return false;
        char tmp[4096];
        ssize_t n = ::recv(conn_fd_, tmp, sizeof(tmp), 0);
        if (n <= 0)
        {
            done_ = true;
            return false;
        }
        recv_buf_.append(tmp, static_cast<std::size_t>(n));
    }
    return true;
}

bool SocketPacketSource::has_next() const
{
    return ensure_line();
}

Packet SocketPacketSource::next()
{
    if (!ensure_line())
        throw std::runtime_error("next() called with no data available");

    const auto nl = recv_buf_.find('\n');
    std::string line = recv_buf_.substr(0, nl);
    recv_buf_.erase(0, nl + 1);

    // wire format: <size_bytes>,<traffic_class>,<priority_tag>
    std::istringstream ss(line);
    std::string tok;

    if (!std::getline(ss, tok, ','))
        throw std::runtime_error("malformed packet line: missing size_bytes");
    const auto size_bytes = static_cast<std::uint32_t>(std::stoul(tok));

    if (!std::getline(ss, tok, ','))
        throw std::runtime_error("malformed packet line: missing traffic_class");
    const TrafficClass tc = (tok == "control") ? TrafficClass::Control : TrafficClass::Bulk;

    if (!std::getline(ss, tok))
        throw std::runtime_error("malformed packet line: missing priority_tag");
    const auto priority_tag = static_cast<std::uint8_t>(std::stoul(tok));

    Packet pkt;
    pkt.arrival_time_us    = now_us();
    pkt.packet_size_bytes  = size_bytes;
    pkt.traffic_class      = tc;
    pkt.priority_tag       = priority_tag;
    return pkt;
}

} // namespace sim::cpu_fifo
