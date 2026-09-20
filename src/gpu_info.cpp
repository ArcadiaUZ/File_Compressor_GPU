#include "gpu_info.h"
#include <cstdio>
#include <iostream>

void GpuConfig::print() const {
    std::cout << "GPU: " << gpu_name << "\n"
              << "  CUDA: " << (cuda_available ? "HA" : "YO'Q (CPU-fallback)") << "\n"
              << "  CC: " << compute_major << "." << compute_minor
              << " | SM: " << sm_count
              << " | VRAM: " << (total_mem >> 20) << " MB\n"
              << "  block_size=" << block_size
              << " streams=" << num_streams
              << " chunk=" << (chunk_size >> 20) << " MB\n";
}

#ifdef USE_CUDA
#include <cuda_runtime.h>
std::vector<GpuDevice> list_gpus() {
    std::vector<GpuDevice> out;
    int n = 0;
    if (cudaGetDeviceCount(&n) != cudaSuccess) return out;
    for (int i = 0; i < n; i++) {
        cudaDeviceProp p{};
        if (cudaGetDeviceProperties(&p, i) != cudaSuccess) continue;
        out.push_back({i, p.name});
    }
    return out;
}
static GpuConfig tune(const cudaDeviceProp& p, int dev_id) {
    GpuConfig c;
    c.gpu_name = p.name; c.cuda_available = true;
    c.device_id = dev_id;
    c.sm_count = p.multiProcessorCount;
    c.compute_major = p.major; c.compute_minor = p.minor;
    c.total_mem = p.totalGlobalMem;
    // Avtomatik sozlash: kuchli GPU -> katta block/chunk/stream
    int cc = p.major * 10 + p.minor; // 86 = Ampere
    c.block_size = (cc >= 80) ? 512 : 256;
    c.num_streams = (p.multiProcessorCount >= 20) ? 4 : 2;
    c.chunk_size = (p.totalGlobalMem > (8ULL<<30)) ? (4<<20) : (2<<20);
    int best = 0; size_t bestBw = 0;
    // Eng ko'p SM x VRAM liligi — "kerakli kutubxona/yadro" tanlash
    (void)best; (void)bestBw;
    return c;
}
GpuConfig detect_best_gpu() {
    int n = 0;
    if (cudaGetDeviceCount(&n) != cudaSuccess || n == 0) return GpuConfig{};
    int best = 0; long long bestScore = -1;
    cudaDeviceProp bp{};
    bool found = false;
    for (int i = 0; i < n; i++) {
        cudaDeviceProp p{};
        if (cudaGetDeviceProperties(&p, i) != cudaSuccess) continue;
        long long score = (long long)p.multiProcessorCount * (p.totalGlobalMem >> 20);
        if (score > bestScore) { bestScore = score; best = i; bp = p; found = true; }
    }
    if (!found) return GpuConfig{};
    if (cudaSetDevice(best) != cudaSuccess) return GpuConfig{};
    GpuConfig c = tune(bp, best);
    return c;
}
#else
// CUDA yo'q: nvidia-smi orqali nomini bilib olamiz (Windows da ham ishlaydi),
// sozlamani taxminiy tanlaymiz — keyin CUDA o'rnatilgach to'liq GPU yoqiladi.
static std::string run_nvidia_smi(const char* args) {
    std::string cmd = std::string("nvidia-smi ") + args;
    std::string out;
#ifdef _WIN32
    FILE* p = _popen(cmd.c_str(), "r");
#else
    FILE* p = popen(cmd.c_str(), "r");
#endif
    if (!p) return out;
    char buf[512];
    while (fgets(buf, sizeof buf, p)) out += buf;
#ifdef _WIN32
    _pclose(p);
#else
    pclose(p);
#endif
    return out;
}
static std::string trim_str(std::string s) {
    while (!s.empty() && (s.back() == '\n' || s.back() == '\r' || s.back() == ' ' || s.back() == '\t')) s.pop_back();
    size_t i = 0;
    while (i < s.size() && (s[i] == ' ' || s[i] == '\t')) i++;
    if (i) s.erase(0, i);
    return s;
}
std::vector<GpuDevice> list_gpus() {
    std::vector<GpuDevice> out;
    std::string raw = run_nvidia_smi("--query-gpu=name --format=csv,noheader");
    if (raw.empty()) return out;
    int id = 0;
    size_t pos = 0;
    while (pos <= raw.size()) {
        size_t nl = raw.find('\n', pos);
        std::string line = trim_str(raw.substr(pos, nl == std::string::npos ? std::string::npos : nl - pos));
        if (!line.empty()) out.push_back({id++, line});
        if (nl == std::string::npos) break;
        pos = nl + 1;
    }
    return out;
}
GpuConfig detect_best_gpu() {
    GpuConfig c; // default CPU-fallback
    auto g = list_gpus();
    if (!g.empty()) {
        // Toza GPU nomi (qavsli izohsiz — backend prefiksi allaqachon "CPU-fallback" deydi)
        c.gpu_name = g[0].name;
        // VRAM ni ham bilib olsak chunk ni aniqroq tanlaymiz
        std::string mem_raw = trim_str(run_nvidia_smi("--query-gpu=memory.total --format=csv,noheader,nounits"));
        long mem_mb = 0;
        try { mem_mb = std::stol(mem_raw); } catch (...) { mem_mb = 0; }
        if (mem_mb > 0) c.total_mem = (size_t)mem_mb << 20;
        // Kuchli kartalar uchun oldindan optimal sozlama.
        // Oldin "40"/"50" substring bo'yicha aniqlanardi — "GT 540M", "GTX 1050" ham
        // "strong" chiqardi. Endi faqat RTX 30xx/40xx/50xx va katta VRAM.
        auto has = [&](const char* s){ return c.gpu_name.find(s) != std::string::npos; };
        bool rtx30 = has("RTX 30") || has("3060") || has("3070") || has("3080") || has("3090");
        bool rtx40 = has("RTX 40") || has("RTX 4060") || has("RTX 4070") || has("RTX 4080") || has("RTX 4090");
        bool rtx50 = has("RTX 50") || has("RTX 5060") || has("RTX 5070") || has("RTX 5080") || has("RTX 5090");
        bool strong = rtx30 || rtx40 || rtx50 || (mem_mb >= 8000);
        if (strong) {
            c.block_size = 512; c.num_streams = 4; c.chunk_size = (2 << 20);
        }
    }
    return c;
}
#endif
