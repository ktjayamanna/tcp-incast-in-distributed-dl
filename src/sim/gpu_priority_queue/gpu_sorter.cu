#include "gpu_sorter.cuh"

#include <cuda_runtime.h>
#include <thrust/device_ptr.h>
#include <thrust/sort.h>
#include <chrono>
#include <cstdint>
#include <limits>
#include <stdexcept>
#include <string>

namespace sim::gpu_priority_queue
{

namespace
{

void cuda_check(cudaError_t err, const char* context)
{
    if (err != cudaSuccess)
        throw std::runtime_error(std::string(context) + ": " + cudaGetErrorString(err));
}

} // anonymous namespace

// ---- GpuSorter --------------------------------------------------------------

GpuSorter::GpuSorter(int max_batch_size)
    : capacity_(max_batch_size)
{
    cuda_check(
        cudaMalloc(&d_keys_,    capacity_ * sizeof(std::uint64_t)),
        "cudaMalloc d_keys_");
    cuda_check(
        cudaMalloc(&d_indices_, capacity_ * sizeof(std::uint32_t)),
        "cudaMalloc d_indices_");

    // Page-locked host buffers enable fast async DMA over PCIe (zero-copy ready).
    cuda_check(
        cudaHostAlloc(&h_keys_,    capacity_ * sizeof(std::uint64_t), cudaHostAllocDefault),
        "cudaHostAlloc h_keys_");
    cuda_check(
        cudaHostAlloc(&h_indices_, capacity_ * sizeof(std::uint32_t), cudaHostAllocDefault),
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

    if (n > capacity_)
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

    // Fill pinned staging buffer.
    for (int i = 0; i < n; i++)
    {
        h_keys_[i]    = sort_keys[i];
        h_indices_[i] = static_cast<std::uint32_t>(i);
    }

    // H2D transfer (pinned memory enables optimal DMA bandwidth).
    if (timing) cudaEventRecord(ev_h2d_start);
    cuda_check(
        cudaMemcpy(d_keys_,    h_keys_,    n * sizeof(std::uint64_t), cudaMemcpyHostToDevice),
        "H2D keys");
    cuda_check(
        cudaMemcpy(d_indices_, h_indices_, n * sizeof(std::uint32_t), cudaMemcpyHostToDevice),
        "H2D indices");
    if (timing) cudaEventRecord(ev_h2d_end);

    // Thrust radix sort by key — far lower overhead than bitonic for variable n.
    if (timing) cudaEventRecord(ev_kern_start);
    thrust::sort_by_key(
        thrust::device_ptr<std::uint64_t>(d_keys_),
        thrust::device_ptr<std::uint64_t>(d_keys_ + n),
        thrust::device_ptr<std::uint32_t>(d_indices_));
    if (timing) cudaEventRecord(ev_kern_end);
    cuda_check(cudaDeviceSynchronize(), "thrust sort sync");

    // D2H: copy sorted indices back.
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
