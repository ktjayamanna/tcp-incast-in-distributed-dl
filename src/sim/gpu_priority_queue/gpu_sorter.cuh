#pragma once

#include <cuda_runtime.h>
#include <cstdint>

namespace sim::gpu_priority_queue
{

// Maximum packets per sort wave.
static constexpr int kMaxSortCapacity = 131072;

// Number of independent pipeline slots (H2D / compute / D2H can all be
// in flight simultaneously across different slots).
static constexpr int kPipelineDepth = 3;

// Per-call timing breakdown from GpuSorter::sort() (legacy synchronous API).
struct SortTiming
{
    float h2d_ms    = 0.f;
    float kernel_ms = 0.f;
    float d2h_ms    = 0.f;
    float wall_ms   = 0.f;
};

// Per-slot timing returned by collect_with_timing() (pipelined async API).
// Times are measured by CUDA events recorded on the slot's stream, so they
// reflect true GPU-side execution even when the CPU is doing other work.
struct SlotTiming
{
    float h2d_ms    = 0.f;
    float kernel_ms = 0.f;
    float d2h_ms    = 0.f;
    float wall_ms   = 0.f;  // h2d_start → d2h_end, GPU side
};

// Triple-buffered sort engine.
//
// Each of the kPipelineDepth slots owns:
//   • a dedicated CUDA stream
//   • its own device buffers (d_keys_, d_indices_)
//   • its own pinned host buffers (h_keys_, h_indices_)
//
// This lets three operations proceed concurrently on the PCIe bus / SM:
//   slot N   : H2D transfer   (load wave N to GPU)
//   slot N-1 : radix sort     (compute wave N-1)
//   slot N-2 : D2H transfer   (move wave N-2 results back to CPU)
//
// Sort key convention: smaller key = higher scheduling priority.
//   key = ((uint64_t)(255 - priority_tag) << 32) | (uint64_t)sequence
class GpuSorter
{
public:
    explicit GpuSorter(int max_batch_size = kMaxSortCapacity);
    ~GpuSorter();

    // ── Pipelined async API ────────────────────────────────────────────────

    // Non-blocking. Copies keys into slot's pinned buffer, then queues
    // H2D + radix sort + D2H onto slot's dedicated CUDA stream.
    // Returns immediately; CPU can advance the traffic simulation while
    // all three PCIe and compute phases execute asynchronously.
    // Precondition: slot is not already in flight.
    void submit_async(int slot, const std::uint64_t* keys, int n);

    // Non-blocking query. Returns true when all operations on this slot
    // (H2D, kernel, D2H) have completed.
    bool poll_ready(int slot) const;

    // Blocking collect. Waits for the slot's stream to finish (a no-op if
    // poll_ready() already returned true), then writes the sorted permutation
    // of [0, n) into out_indices.
    void collect(int slot, std::uint32_t* out_indices, int n);

    // Collect variant that also returns per-phase GPU timing.
    void collect_with_timing(int slot, std::uint32_t* out_indices, int n,
                             SlotTiming& timing);

    // ── Legacy synchronous API (kept for benchmark comparisons) ───────────

    void sort(const std::uint64_t* sort_keys,
              std::uint32_t*       out_indices,
              int                  n,
              SortTiming*          timing = nullptr);

    int capacity() const { return capacity_; }

private:
    struct Slot
    {
        cudaStream_t   stream    = nullptr;

        // Device buffers
        std::uint64_t* d_keys    = nullptr;
        std::uint32_t* d_indices = nullptr;

        // Pinned (page-locked) host staging buffers
        std::uint64_t* h_keys    = nullptr;
        std::uint32_t* h_indices = nullptr;

        // CUDA events for per-phase timing (recorded in submit_async)
        cudaEvent_t ev_h2d_start  = nullptr;
        cudaEvent_t ev_h2d_end    = nullptr;
        cudaEvent_t ev_kern_start = nullptr;
        cudaEvent_t ev_kern_end   = nullptr;
        cudaEvent_t ev_d2h_start  = nullptr;
        cudaEvent_t ev_d2h_end    = nullptr;
    };

    int  capacity_;
    Slot slots_[kPipelineDepth];
};

} // namespace sim::gpu_priority_queue
