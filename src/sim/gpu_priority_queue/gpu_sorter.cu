#include "gpu_sorter.cuh"

#include <cuda_runtime.h>
#include <chrono>
#include <cstdint>
#include <limits>
#include <stdexcept>
#include <string>

namespace sim::gpu_priority_queue
{

namespace
{

// ---- utilities --------------------------------------------------------------

int next_power_of_two(int n)
{
    int p = 1;
    while (p < n) p <<= 1;
    return p;
}

void cuda_check(cudaError_t err, const char* context)
{
    if (err != cudaSuccess)
        throw std::runtime_error(std::string(context) + ": " + cudaGetErrorString(err));
}

// ---- parallel bitonic sort kernel -------------------------------------------
//
// Each thread handles one element at index `tid`.  It compares element[tid]
// with element[tid ^ j] and conditionally swaps to maintain the bitonic
// sequence.  Only the thread with the larger index executes the swap to avoid
// data races (each pair is owned by exactly one thread).
//
// After all log2(m)*(log2(m)+1)/2 steps the array is sorted ascending.

__global__ void bitonic_step(
    std::uint64_t* __restrict__ keys,
    std::uint32_t* __restrict__ indices,
    int j,
    int k,
    int m)
{
    const int tid = static_cast<int>(blockIdx.x) * blockDim.x + threadIdx.x;
    if (tid >= m) return;

    const int ixj = tid ^ j;
    if (ixj <= tid) return; // each pair handled once (by the larger-index thread)

    // Ascending within this block when (tid & k) == 0, descending otherwise.
    const bool ascending = ((tid & k) == 0);

    // Swap if the two elements are in the wrong order for this pass direction.
    if ((keys[tid] > keys[ixj]) == ascending)
    {
        const std::uint64_t tmp_k = keys[tid];
        keys[tid]                 = keys[ixj];
        keys[ixj]                 = tmp_k;

        const std::uint32_t tmp_i = indices[tid];
        indices[tid]               = indices[ixj];
        indices[ixj]               = tmp_i;
    }
}

} // anonymous namespace

// ---- GpuSorter --------------------------------------------------------------

GpuSorter::GpuSorter(int max_batch_size)
    : padded_capacity_(next_power_of_two(max_batch_size))
{
    cuda_check(
        cudaMalloc(&d_keys_,    padded_capacity_ * sizeof(std::uint64_t)),
        "cudaMalloc d_keys_");
    cuda_check(
        cudaMalloc(&d_indices_, padded_capacity_ * sizeof(std::uint32_t)),
        "cudaMalloc d_indices_");

    // Page-locked host buffers enable fast async DMA over PCIe (zero-copy ready).
    cuda_check(
        cudaHostAlloc(&h_keys_,    padded_capacity_ * sizeof(std::uint64_t), cudaHostAllocDefault),
        "cudaHostAlloc h_keys_");
    cuda_check(
        cudaHostAlloc(&h_indices_, padded_capacity_ * sizeof(std::uint32_t), cudaHostAllocDefault),
        "cudaHostAlloc h_indices_");
}

GpuSorter::~GpuSorter()
{
    cudaFree(d_keys_);
    cudaFree(d_indices_);
    cudaFreeHost(h_keys_);
    cudaFreeHost(h_indices_);
}

void GpuSorter::sort(
    const std::uint64_t* sort_keys,
    std::uint32_t*       out_indices,
    int                  n,
    SortTiming*          timing)
{
    if (n <= 0) return;

    const int m = next_power_of_two(n); // bitonic sort requires power-of-2 size
    if (m > padded_capacity_)
        throw std::runtime_error("GpuSorter: batch size exceeds allocated capacity");

    // Wall-clock start (covers everything including CUDA API overhead).
    const auto wall_start = std::chrono::steady_clock::now();

    // CUDA events for per-phase GPU timing.
    cudaEvent_t ev_h2d_start, ev_h2d_end, ev_kern_start, ev_kern_end, ev_d2h_start, ev_d2h_end;
    if (timing)
    {
        cudaEventCreate(&ev_h2d_start);  cudaEventCreate(&ev_h2d_end);
        cudaEventCreate(&ev_kern_start); cudaEventCreate(&ev_kern_end);
        cudaEventCreate(&ev_d2h_start);  cudaEventCreate(&ev_d2h_end);
    }

    // Fill pinned staging buffer: real keys first, then sentinel padding.
    // Sentinel UINT64_MAX ensures padding slots sort to the back and are ignored.
    for (int i = 0; i < n; i++)
    {
        h_keys_[i]    = sort_keys[i];
        h_indices_[i] = static_cast<std::uint32_t>(i);
    }
    for (int i = n; i < m; i++)
    {
        h_keys_[i]    = std::numeric_limits<std::uint64_t>::max();
        h_indices_[i] = static_cast<std::uint32_t>(i);
    }

    // H2D transfer (pinned memory enables optimal DMA bandwidth)
    if (timing) cudaEventRecord(ev_h2d_start);
    cuda_check(
        cudaMemcpy(d_keys_,    h_keys_,    m * sizeof(std::uint64_t), cudaMemcpyHostToDevice),
        "H2D keys");
    cuda_check(
        cudaMemcpy(d_indices_, h_indices_, m * sizeof(std::uint32_t), cudaMemcpyHostToDevice),
        "H2D indices");
    if (timing) cudaEventRecord(ev_h2d_end);

    // Parallel bitonic sort: O(log^2 m) kernel launches, each with m threads.
    // Outer loop k doubles each iteration (merge stage size).
    // Inner loop j halves each iteration (compare distance within stage).
    constexpr int kBlockSize = 256;
    const int     num_blocks = (m + kBlockSize - 1) / kBlockSize;

    if (timing) cudaEventRecord(ev_kern_start);
    for (int k = 2; k <= m; k <<= 1)
    {
        for (int j = k >> 1; j > 0; j >>= 1)
            bitonic_step<<<num_blocks, kBlockSize>>>(d_keys_, d_indices_, j, k, m);
    }
    if (timing) cudaEventRecord(ev_kern_end);
    cuda_check(cudaDeviceSynchronize(), "bitonic sort sync");

    // D2H: copy only the first n indices (padding slots stay on device)
    if (timing) cudaEventRecord(ev_d2h_start);
    cuda_check(
        cudaMemcpy(h_indices_, d_indices_, n * sizeof(std::uint32_t), cudaMemcpyDeviceToHost),
        "D2H indices");
    if (timing) cudaEventRecord(ev_d2h_end);

    for (int i = 0; i < n; i++)
        out_indices[i] = h_indices_[i];

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
