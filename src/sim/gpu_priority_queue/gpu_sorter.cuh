#pragma once

#include <cstdint>

namespace sim::gpu_priority_queue
{

// Maximum packets per sort call (padded internally to next power of 2).
static constexpr int kMaxSortCapacity = 65536;

// Manages pinned host staging buffers and device buffers for one parallel
// bitonic-sort invocation at a time.
//
// Sort key convention: a *smaller* key means *higher* scheduling priority.
// Callers should encode:
//   key = ((uint64_t)(255 - priority_tag) << 32) | (uint64_t)sequence
// so that higher-priority, earlier-arriving packets sort to the front.
class GpuSorter
{
public:
    explicit GpuSorter(int max_batch_size = kMaxSortCapacity);
    ~GpuSorter();

    // Sort n entries.  Writes the permutation of [0, n) into out_indices such
    // that sort_keys[out_indices[0]] is the minimum (highest-priority) key.
    void sort(const std::uint64_t* sort_keys,
              std::uint32_t*       out_indices,
              int                  n);

private:
    int padded_capacity_;  // next power-of-2 >= max_batch_size

    // Device buffers
    std::uint64_t* d_keys_    = nullptr;
    std::uint32_t* d_indices_ = nullptr;

    // Pinned host staging buffers (cudaHostAlloc – page-locked for fast PCIe DMA)
    std::uint64_t* h_keys_    = nullptr;
    std::uint32_t* h_indices_ = nullptr;
};

} // namespace sim::gpu_priority_queue
