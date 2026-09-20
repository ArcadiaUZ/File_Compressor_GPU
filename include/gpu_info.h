#pragma once
#include <cstdint>
#include <string>
#include <vector>

/// Har bir NVIDIA GPU uchun avtomatik tanlangan optimal sozlama.
/// Dastur ishga tushganda GPU ni aniqlab, shuni yuklaydi (user talabi).
struct GpuConfig {
    std::string gpu_name = "CPU-fallback (CUDA yo'q)";
    int device_id = 0; // multi-GPU da tanlangan device (cudaSetDevice uchun)
    int sm_count = 0;
    int compute_major = 0, compute_minor = 0;
    size_t total_mem = 0;
    int block_size = 256;      // CUDA block o'lchami
    int num_streams = 2;       // parallel copy/compute
    size_t chunk_size = 1 << 20; // 1 MB default
    bool cuda_available = false;

    void print() const;
};

struct GpuDevice {
    int id = -1;
    std::string name;
};

/// Barcha GPU larni ro'yxatlash + eng kuchlisini tanlash
std::vector<GpuDevice> list_gpus();
GpuConfig detect_best_gpu(); // avtomatik aniqlash — asosiy funksiya
