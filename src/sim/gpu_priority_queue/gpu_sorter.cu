#include "gpu_sorter.cuh"

#include <cuda_runtime.h>
#include <thrust/device_ptr.h>
#include <thrust/execution_policy.h>
#include <thrust/sort.h>
#include <chrono>
#include <cstdint>
#include <stdexcept>
#include <string>

namespace sim::gpu_priority_queue
{

namespace
{

void cuda_check(cudaError_t err, const char* ctx)
{
    if (err != cudaSuccess)
        throw std::runtime_error(std::string(ctx) + ": " + cudaGetErrorString(err));
}

} // anonymous namespace

// ── Construction / destruction ───────────────────────────────────────────────

GpuSorter::GpuSorter(int max_batch_size)
    : capacity_(max_batch_size)
{
    for (int s = 0; s < kPipelineDepth; ++s)
    {
        Slot& sl = slots_[s];

        cuda_check(cudaStreamCreate(&sl.stream), "cudaStreamCreate");

        cuda_check(cudaMalloc(&sl.d_keys,    capacity_ * sizeof(std::uint64_t)), "cudaMalloc d_keys");
        cuda_check(cudaMalloc(&sl.d_indices, capacity_ * sizeof(std::uint32_t)), "cudaMalloc d_indices");

        // Page-locked host buffers for maximum PCIe DMA throughput.
        cuda_check(cudaHostAlloc(&sl.h_keys,    capacity_ * sizeof(std::uint64_t), cudaHostAllocDefault), "cudaHostAlloc h_keys");
        cuda_check(cudaHostAlloc(&sl.h_indices, capacity_ * sizeof(std::uint32_t), cudaHostAllocDefault), "cudaHostAlloc h_indices");

        cuda_check(cudaEventCreate(&sl.ev_h2d_start),  "ev_h2d_start");
        cuda_check(cudaEventCreate(&sl.ev_h2d_end),    "ev_h2d_end");
        cuda_check(cudaEventCreate(&sl.ev_kern_start), "ev_kern_start");
        cuda_check(cudaEventCreate(&sl.ev_kern_end),   "ev_kern_end");
        cuda_check(cudaEventCreate(&sl.ev_d2h_start),  "ev_d2h_start");
        cuda_check(cudaEventCreate(&sl.ev_d2h_end),    "ev_d2h_end");
    }
}

GpuSorter::~GpuSorter()
{
    for (int s = 0; s < kPipelineDepth; ++s)
    {
        Slot& sl = slots_[s];
        // Drain before freeing so in-flight work doesn't write freed memory.
        if (sl.stream) cudaStreamSynchronize(sl.stream);

        cudaEventDestroy(sl.ev_h2d_start);
        cudaEventDestroy(sl.ev_h2d_end);
        cudaEventDestroy(sl.ev_kern_start);
        cudaEventDestroy(sl.ev_kern_end);
        cudaEventDestroy(sl.ev_d2h_start);
        cudaEventDestroy(sl.ev_d2h_end);

        cudaFree(sl.d_keys);
        cudaFree(sl.d_indices);
        cudaFreeHost(sl.h_keys);
        cudaFreeHost(sl.h_indices);
        if (sl.stream) cudaStreamDestroy(sl.stream);
    }
}

// ── Pipelined async API ──────────────────────────────────────────────────────

// submit_async queues four operations onto sl.stream and returns immediately:
//
//   1. cudaMemcpyAsync  keys    → d_keys     (H2D, PCIe)
//   2. cudaMemcpyAsync  indices → d_indices  (H2D, PCIe) [iota init]
//   3. thrust::sort_by_key on stream          (SM compute)
//   4. cudaMemcpyAsync  d_indices → h_indices (D2H, PCIe)
//
// Because all four are on the same per-slot stream they execute in order on
// the GPU side, but the CPU returns before any of them start.  Three slots
// with three independent streams let the GPU and PCIe bus overlap all three
// phases across consecutive waves.
void GpuSorter::submit_async(int slot, const std::uint64_t* keys, int n)
{
    if (n <= 0 || n > capacity_)
        throw std::runtime_error("GpuSorter::submit_async: invalid batch size");

    Slot& sl = slots_[slot];

    // Fill pinned host buffers (CPU writes to WC/pinned memory — fast).
    for (int i = 0; i < n; ++i)
    {
        sl.h_keys[i]    = keys[i];
        sl.h_indices[i] = static_cast<std::uint32_t>(i);  // iota for sort-by-key
    }

    // H2D — non-blocking for CPU, queued on sl.stream.
    cudaEventRecord(sl.ev_h2d_start, sl.stream);
    cuda_check(
        cudaMemcpyAsync(sl.d_keys, sl.h_keys,
                        n * sizeof(std::uint64_t),
                        cudaMemcpyHostToDevice, sl.stream),
        "async H2D keys");
    cuda_check(
        cudaMemcpyAsync(sl.d_indices, sl.h_indices,
                        n * sizeof(std::uint32_t),
                        cudaMemcpyHostToDevice, sl.stream),
        "async H2D indices");
    cudaEventRecord(sl.ev_h2d_end, sl.stream);

    // Radix sort — non-blocking for CPU, queued on sl.stream.
    cudaEventRecord(sl.ev_kern_start, sl.stream);
    thrust::sort_by_key(
        thrust::cuda::par.on(sl.stream),
        thrust::device_ptr<std::uint64_t>(sl.d_keys),
        thrust::device_ptr<std::uint64_t>(sl.d_keys + n),
        thrust::device_ptr<std::uint32_t>(sl.d_indices));
    cudaEventRecord(sl.ev_kern_end, sl.stream);

    // D2H sorted indices — non-blocking for CPU, queued on sl.stream.
    cudaEventRecord(sl.ev_d2h_start, sl.stream);
    cuda_check(
        cudaMemcpyAsync(sl.h_indices, sl.d_indices,
                        n * sizeof(std::uint32_t),
                        cudaMemcpyDeviceToHost, sl.stream),
        "async D2H indices");
    cudaEventRecord(sl.ev_d2h_end, sl.stream);

    // CPU returns here.  GPU/PCIe continues asynchronously.
}

bool GpuSorter::poll_ready(int slot) const
{
    return cudaStreamQuery(slots_[slot].stream) == cudaSuccess;
}

void GpuSorter::collect(int slot, std::uint32_t* out_indices, int n)
{
    Slot& sl = slots_[slot];
    // If poll_ready() already returned true this is an instant no-op.
    cuda_check(cudaStreamSynchronize(sl.stream), "cudaStreamSynchronize");
    for (int i = 0; i < n; ++i)
        out_indices[i] = sl.h_indices[i];
}

void GpuSorter::collect_with_timing(int slot, std::uint32_t* out_indices,
                                    int n, SlotTiming& t)
{
    Slot& sl = slots_[slot];
    cuda_check(cudaStreamSynchronize(sl.stream), "cudaStreamSynchronize (timing)");

    cudaEventElapsedTime(&t.h2d_ms,    sl.ev_h2d_start,  sl.ev_h2d_end);
    cudaEventElapsedTime(&t.kernel_ms, sl.ev_kern_start,  sl.ev_kern_end);
    cudaEventElapsedTime(&t.d2h_ms,    sl.ev_d2h_start,  sl.ev_d2h_end);
    cudaEventElapsedTime(&t.wall_ms,   sl.ev_h2d_start,  sl.ev_d2h_end);

    for (int i = 0; i < n; ++i)
        out_indices[i] = sl.h_indices[i];
}

// ── Legacy synchronous API ───────────────────────────────────────────────────

void GpuSorter::sort(
    const std::uint64_t* sort_keys,
    std::uint32_t*       out_indices,
    int                  n,
    SortTiming*          timing)
{
    if (n <= 0) return;
    if (n > capacity_)
        throw std::runtime_error("GpuSorter: batch size exceeds allocated capacity");

    const auto wall_start = std::chrono::steady_clock::now();

    // Legacy path reuses slot 0 with synchronous memcpy.
    Slot& sl = slots_[0];

    cudaEvent_t ev_h2d_start, ev_h2d_end, ev_kern_start, ev_kern_end, ev_d2h_start, ev_d2h_end;
    if (timing)
    {
        cudaEventCreate(&ev_h2d_start);  cudaEventCreate(&ev_h2d_end);
        cudaEventCreate(&ev_kern_start); cudaEventCreate(&ev_kern_end);
        cudaEventCreate(&ev_d2h_start);  cudaEventCreate(&ev_d2h_end);
    }

    for (int i = 0; i < n; i++)
    {
        sl.h_keys[i]    = sort_keys[i];
        sl.h_indices[i] = static_cast<std::uint32_t>(i);
    }

    if (timing) cudaEventRecord(ev_h2d_start);
    cuda_check(cudaMemcpy(sl.d_keys,    sl.h_keys,    n * sizeof(std::uint64_t), cudaMemcpyHostToDevice), "H2D keys");
    cuda_check(cudaMemcpy(sl.d_indices, sl.h_indices, n * sizeof(std::uint32_t), cudaMemcpyHostToDevice), "H2D indices");
    if (timing) cudaEventRecord(ev_h2d_end);

    if (timing) cudaEventRecord(ev_kern_start);
    thrust::sort_by_key(
        thrust::device_ptr<std::uint64_t>(sl.d_keys),
        thrust::device_ptr<std::uint64_t>(sl.d_keys + n),
        thrust::device_ptr<std::uint32_t>(sl.d_indices));
    if (timing) cudaEventRecord(ev_kern_end);
    cuda_check(cudaDeviceSynchronize(), "thrust sort sync");

    if (timing) cudaEventRecord(ev_d2h_start);
    cuda_check(cudaMemcpy(sl.h_indices, sl.d_indices, n * sizeof(std::uint32_t), cudaMemcpyDeviceToHost), "D2H indices");
    if (timing) cudaEventRecord(ev_d2h_end);

    for (int i = 0; i < n; i++)
        out_indices[i] = sl.h_indices[i];

    if (timing)
    {
        cudaEventSynchronize(ev_d2h_end);
        cudaEventElapsedTime(&timing->h2d_ms,    ev_h2d_start,  ev_h2d_end);
        cudaEventElapsedTime(&timing->kernel_ms, ev_kern_start, ev_kern_end);
        cudaEventElapsedTime(&timing->d2h_ms,    ev_d2h_start,  ev_d2h_end);
        const auto wall_end = std::chrono::steady_clock::now();
        timing->wall_ms = static_cast<float>(
            std::chrono::duration<double, std::milli>(wall_end - wall_start).count());
        cudaEventDestroy(ev_h2d_start);  cudaEventDestroy(ev_h2d_end);
        cudaEventDestroy(ev_kern_start); cudaEventDestroy(ev_kern_end);
        cudaEventDestroy(ev_d2h_start);  cudaEventDestroy(ev_d2h_end);
    }
}

} // namespace sim::gpu_priority_queue
