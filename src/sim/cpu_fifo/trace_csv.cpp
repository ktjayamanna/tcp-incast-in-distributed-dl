#include "trace_csv.hpp"

#include <array>
#include <cctype>
#include <fstream>
#include <sstream>
#include <stdexcept>
#include <string>

namespace sim::cpu_fifo::trace_csv
{

namespace
{

constexpr std::array<const char *, 4> kHeader = {
    "packet_start_us",
    "packet_size_bytes",
    "traffic_class",
    "priority_tag",
};

std::string trim(std::string s)
{
    std::size_t a = 0, b = s.size();
    while (a < b && std::isspace(static_cast<unsigned char>(s[a]))) ++a;
    while (b > a && std::isspace(static_cast<unsigned char>(s[b - 1]))) --b;
    return s.substr(a, b - a);
}

std::vector<std::string> split(const std::string &line, char delim)
{
    std::vector<std::string> cells;
    std::stringstream ss(line);
    std::string cell;
    while (std::getline(ss, cell, delim))
        cells.push_back(trim(cell));
    return cells;
}

bool blank(const std::string &line)
{
    for (char c : line)
        if (c != ' ' && c != '\t' && c != '\r' && c != '\n') return false;
    return true;
}

Packet parse_row(const std::vector<std::string> &c, std::size_t ln)
{
    if (c.size() != kHeader.size())
        throw std::runtime_error("Wrong column count at line " + std::to_string(ln));

    Packet p{};
    p.arrival_time_us   = static_cast<std::int64_t>(std::stoll(c[0]));
    p.packet_size_bytes = static_cast<std::uint32_t>(std::stoull(c[1]));
    if      (c[2] == "bulk")    p.traffic_class = TrafficClass::Bulk;
    else if (c[2] == "control") p.traffic_class = TrafficClass::Control;
    else throw std::runtime_error("Unknown traffic_class '" + c[2] + "' at line " + std::to_string(ln));
    p.priority_tag = static_cast<std::uint8_t>(std::stoull(c[3]));
    return p;
}

} // namespace

TraceReadResult read_trace_csv(const std::string &path, const TraceReadOptions &opts)
{
    std::ifstream file(path);
    if (!file.is_open())
        throw std::runtime_error("Cannot open: " + path);

    TraceReadResult result{};
    std::string     line;
    std::size_t     ln            = 0;
    bool            header_done   = false;
    std::int64_t    prev_ts       = 0;
    bool            has_prev      = false;

    while (std::getline(file, line))
    {
        ++ln;
        if (blank(line)) { ++result.skipped_rows; continue; }

        auto cells = split(line, opts.delimiter);

        if (!header_done)
        {
            header_done = true;
            if (opts.enforce_header)
            {
                if (cells.size() != kHeader.size())
                    throw std::runtime_error("Bad header at line " + std::to_string(ln));
                for (std::size_t i = 0; i < kHeader.size(); ++i)
                    if (cells[i] != kHeader[i])
                        throw std::runtime_error("Expected column '" + std::string(kHeader[i]) +
                                                 "' at position " + std::to_string(i));
                continue;
            }
        }

        Packet pkt = parse_row(cells, ln);
        if (opts.enforce_sorted_timestamps && has_prev && pkt.arrival_time_us < prev_ts)
            throw std::runtime_error("Timestamps not sorted at line " + std::to_string(ln));

        result.packets.push_back(pkt);
        ++result.row_count;
        prev_ts  = pkt.arrival_time_us;
        has_prev = true;
    }

    return result;
}

CsvPacketSource::CsvPacketSource(std::string path, TraceReadOptions opts)
    : trace_(read_trace_csv(path, opts)) {}

bool   CsvPacketSource::has_next() const { return cursor_ < trace_.packets.size(); }
Packet CsvPacketSource::next()
{
    if (!has_next()) throw std::runtime_error("CsvPacketSource::next past end");
    return trace_.packets[cursor_++];
}

} // namespace sim::cpu_fifo::trace_csv
