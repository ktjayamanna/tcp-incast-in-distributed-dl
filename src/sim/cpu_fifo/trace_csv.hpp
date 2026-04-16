#pragma once

#include <cstddef>
#include <string>
#include <vector>

#include "packet_source.hpp"

namespace sim::cpu_fifo::trace_csv
{

    struct TraceReadOptions
    {
        bool enforce_header = true;
        bool enforce_sorted_timestamps = true;
        char delimiter = ',';
    };

    struct TraceReadResult
    {
        std::vector<Packet> packets;
        std::size_t row_count = 0;
        std::size_t skipped_rows = 0;
    };

    TraceReadResult read_trace_csv(
        const std::string &path,
        const TraceReadOptions &options = {});

    class CsvPacketSource final : public PacketSource
    {
    public:
        explicit CsvPacketSource(
            std::string path,
            TraceReadOptions options = {});

        bool has_next() const override;
        Packet next() override;

    private:
        TraceReadResult trace_;
        std::size_t cursor_ = 0;
    };

} // namespace sim::cpu_fifo::trace_csv
