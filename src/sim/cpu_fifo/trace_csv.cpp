#include "trace_csv.hpp"

#include <array>
#include <cstdint>
#include <fstream>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>
#include <cctype>

namespace sim::cpu_fifo::trace_csv
{

    namespace
    {

        constexpr std::array<const char *, 7> kExpectedHeader = {
            "packet_start_us",
            "wave_id",
            "sender_id",
            "packet_index_for_sender",
            "packet_size_bytes",
            "traffic_class",
            "priority_tag",
        };

        std::string trim_ascii_whitespace(std::string value)
        {
            std::size_t start = 0;
            while (start < value.size() && std::isspace(static_cast<unsigned char>(value[start])) != 0)
            {
                ++start;
            }

            std::size_t end = value.size();
            while (end > start && std::isspace(static_cast<unsigned char>(value[end - 1])) != 0)
            {
                --end;
            }

            return value.substr(start, end - start);
        }

        std::vector<std::string> split_line(const std::string &line, char delimiter)
        {
            std::vector<std::string> cells;
            std::stringstream stream(line);
            std::string cell;
            while (std::getline(stream, cell, delimiter))
            {
                cells.push_back(trim_ascii_whitespace(cell));
            }
            return cells;
        }

        bool is_blank_line(const std::string &line)
        {
            for (char ch : line)
            {
                if (ch != ' ' && ch != '\t' && ch != '\r' && ch != '\n')
                {
                    return false;
                }
            }
            return true;
        }

        std::int64_t parse_i64(const std::string &value, const char *field_name, std::size_t line_no)
        {
            std::size_t pos = 0;
            long long parsed = 0;
            try
            {
                parsed = std::stoll(value, &pos, 10);
            }
            catch (const std::exception &)
            {
                throw std::runtime_error(
                    "Invalid integer in field '" + std::string(field_name) +
                    "' at line " + std::to_string(line_no) + ": '" + value + "'");
            }
            if (pos != value.size())
            {
                throw std::runtime_error(
                    "Invalid integer suffix in field '" + std::string(field_name) +
                    "' at line " + std::to_string(line_no) + ": '" + value + "'");
            }
            return static_cast<std::int64_t>(parsed);
        }

        std::uint64_t parse_u64(const std::string &value, const char *field_name, std::size_t line_no)
        {
            std::size_t pos = 0;
            unsigned long long parsed = 0;
            try
            {
                parsed = std::stoull(value, &pos, 10);
            }
            catch (const std::exception &)
            {
                throw std::runtime_error(
                    "Invalid unsigned integer in field '" + std::string(field_name) +
                    "' at line " + std::to_string(line_no) + ": '" + value + "'");
            }
            if (pos != value.size())
            {
                throw std::runtime_error(
                    "Invalid unsigned integer suffix in field '" + std::string(field_name) +
                    "' at line " + std::to_string(line_no) + ": '" + value + "'");
            }
            return static_cast<std::uint64_t>(parsed);
        }

        TrafficClass parse_traffic_class(const std::string &value, std::size_t line_no)
        {
            if (value == "bulk")
            {
                return TrafficClass::Bulk;
            }
            if (value == "control")
            {
                return TrafficClass::Control;
            }
            throw std::runtime_error(
                "Invalid traffic_class at line " + std::to_string(line_no) +
                ": '" + value + "' (expected 'bulk' or 'control')");
        }

        void validate_header_or_throw(
            const std::vector<std::string> &header_cells,
            std::size_t line_no)
        {
            if (header_cells.size() != kExpectedHeader.size())
            {
                throw std::runtime_error(
                    "Invalid header field count at line " + std::to_string(line_no) +
                    ". Expected " + std::to_string(kExpectedHeader.size()) +
                    ", got " + std::to_string(header_cells.size()));
            }
            for (std::size_t i = 0; i < kExpectedHeader.size(); ++i)
            {
                if (header_cells[i] != kExpectedHeader[i])
                {
                    throw std::runtime_error(
                        "Invalid header field at column " + std::to_string(i) +
                        ". Expected '" + std::string(kExpectedHeader[i]) +
                        "', got '" + header_cells[i] + "'");
                }
            }
        }

        Packet parse_packet_row(const std::vector<std::string> &cells, std::size_t line_no)
        {
            if (cells.size() != kExpectedHeader.size())
            {
                throw std::runtime_error(
                    "Invalid field count at line " + std::to_string(line_no) +
                    ". Expected " + std::to_string(kExpectedHeader.size()) +
                    ", got " + std::to_string(cells.size()));
            }

            Packet packet{};
            packet.arrival_time_us = parse_i64(cells[0], "packet_start_us", line_no);
            packet.packet_size_bytes = static_cast<std::uint32_t>(
                parse_u64(cells[4], "packet_size_bytes", line_no));
            packet.traffic_class = parse_traffic_class(cells[5], line_no);
            packet.priority_tag = static_cast<std::uint8_t>(parse_u64(cells[6], "priority_tag", line_no));
            SyntheticPacketMetadata synthetic_metadata{};
            synthetic_metadata.wave_id = static_cast<std::uint32_t>(parse_u64(cells[1], "wave_id", line_no));
            synthetic_metadata.sender_id = static_cast<std::uint32_t>(parse_u64(cells[2], "sender_id", line_no));
            synthetic_metadata.packet_index_for_sender = static_cast<std::uint32_t>(
                parse_u64(cells[3], "packet_index_for_sender", line_no));
            packet.synthetic_metadata = synthetic_metadata;

            return packet;
        }

    } // namespace

    TraceReadResult read_trace_csv(const std::string &path, const TraceReadOptions &options)
    {
        std::ifstream file(path);
        if (!file.is_open())
        {
            throw std::runtime_error("Failed to open trace CSV: " + path);
        }

        TraceReadResult result{};
        std::string line;
        std::size_t line_no = 0;
        bool header_processed = false;
        std::int64_t previous_ts = 0;
        bool has_previous = false;

        while (std::getline(file, line))
        {
            ++line_no;
            if (is_blank_line(line))
            {
                ++result.skipped_rows;
                continue;
            }

            std::vector<std::string> cells = split_line(line, options.delimiter);

            if (!header_processed)
            {
                header_processed = true;
                if (options.enforce_header)
                {
                    validate_header_or_throw(cells, line_no);
                    continue;
                }
            }

            Packet packet = parse_packet_row(cells, line_no);
            if (options.enforce_sorted_timestamps && has_previous &&
                packet.arrival_time_us < previous_ts)
            {
                throw std::runtime_error(
                    "packet_start_us is not non-decreasing at line " + std::to_string(line_no) +
                    ". Previous=" + std::to_string(previous_ts) +
                    ", current=" + std::to_string(packet.arrival_time_us));
            }

            result.packets.push_back(packet);
            ++result.row_count;
            previous_ts = packet.arrival_time_us;
            has_previous = true;
        }

        return result;
    }

    CsvPacketSource::CsvPacketSource(std::string path, TraceReadOptions options)
        : trace_(read_trace_csv(path, options))
    {
    }

    bool CsvPacketSource::has_next() const
    {
        return cursor_ < trace_.packets.size();
    }

    Packet CsvPacketSource::next()
    {
        if (!has_next())
        {
            throw std::runtime_error("CsvPacketSource::next called with no packets remaining");
        }
        return trace_.packets[cursor_++];
    }

} // namespace sim::cpu_fifo::trace_csv
