// GPU yadrolar — faqat USE_CUDA bilan yig'iladi.
// 1) histogram_gpu: bayt chastotasi (chunked, kam VRAM)
// 2) encode_gpu: Huffman bit-packing to'liq GPU da:
//    har baytning bit-uzunligi -> exclusive scan (bit offsetlar) ->
//    har oqim o'z kodini atomicOr bilan bit oqimiga yozadi.
//    Katta fayllar bo'laklab (chunk) ishlanadi, format CPU bilan bir xil.
#ifdef USE_CUDA
#include "huffman.h"
#include <cuda_runtime.h>
#include <thrust/device_ptr.h>
#include <thrust/scan.h>
#include <array>
#include <cstdint>
#include <cstdlib>
#include <stdexcept>
#include <vector>

// Windows da CUDA xatosi jim o'tib ketsa buzilgan arxiv chiqadi.
// Har chaqiruv tekshiriladi, xato bo'lsa exception bilan CPU-fallback ga tushish mumkin.
#define GCF_CUDA_CHECK(call) do { \
    cudaError_t _e = (call); \
    if (_e != cudaSuccess) throw std::runtime_error(std::string("CUDA xato: ") + cudaGetErrorString(_e)); \
} while (0)

namespace {

// Chunk o'lchami: bo'sh VRAM ga qarab (test uchun GCF_CHUNK_MB muhiti).
size_t gpu_chunk_size() {
    size_t freeB = 0, totalB = 0;
    if (cudaMemGetInfo(&freeB, &totalB) == cudaSuccess && freeB > 0) {
        size_t c = freeB / 12; // data + lens + offsets + out (~10x) zaxira bilan
        // Oldin min 64 MB majburlanardi — 10 MB bo'sh VRAM da cudaMalloc fail bo'lardi.
        // Endi freeB ga clamp qilinadi (min 1 MB).
        size_t max_by_free = freeB > (2ULL << 20) ? freeB - (2ULL << 20) : (1ULL << 20);
        if (c > max_by_free) c = max_by_free;
        if (c < (1ULL << 20)) c = (1ULL << 20);
        if (c > (512ULL << 20)) c = 512ULL << 20;
        return c;
    }
    return 256ULL << 20;
}

size_t chunk_override() {
    if (const char* e = std::getenv("GCF_CHUNK_MB")) {
        long v = std::atol(e);
        if (v >= 1 && v <= 4096) return (size_t)v << 20;
    }
    return gpu_chunk_size();
}

__global__ void hist_kernel(const uint8_t* __restrict__ d, size_t n,
                            unsigned long long* __restrict__ g) {
    __shared__ unsigned int sh[256];
    for (int i = threadIdx.x; i < 256; i += blockDim.x) sh[i] = 0;
    __syncthreads();
    size_t tid = blockIdx.x * blockDim.x + threadIdx.x;
    size_t stride = (size_t)blockDim.x * gridDim.x;
    for (size_t i = tid; i < n; i += stride) atomicAdd(&sh[d[i]], 1u);
    __syncthreads();
    for (int i = threadIdx.x; i < 256; i += blockDim.x)
        if (sh[i]) atomicAdd(&g[i], sh[i]);
}

// Har bayt uchun kod uzunligi
__global__ void lens_kernel(const uint8_t* __restrict__ d, const uint8_t* __restrict__ tab_len,
                            uint32_t* __restrict__ lens, size_t m) {
    size_t tid = blockIdx.x * blockDim.x + threadIdx.x;
    size_t stride = (size_t)blockDim.x * gridDim.x;
    for (size_t i = tid; i < m; i += stride) lens[i] = tab_len[d[i]];
}

// Bit oqimiga parallel yozish (har oqim o'z offsetiga)
__global__ void encode_kernel(const uint8_t* __restrict__ d, const uint32_t* __restrict__ off,
                              const uint32_t* __restrict__ codes, const uint8_t* __restrict__ tab_len,
                              uint32_t* __restrict__ out, size_t m) {
    size_t tid = blockIdx.x * blockDim.x + threadIdx.x;
    size_t stride = (size_t)blockDim.x * gridDim.x;
    for (size_t i = tid; i < m; i += stride) {
        uint8_t b = d[i];
        uint32_t len = tab_len[b];
        if (!len) continue;
        uint32_t code = codes[b];
        uint32_t bit = off[i];
        uint32_t w = bit >> 5, s = bit & 31;
        atomicOr(&out[w], code << s);
        if (s + len > 32) atomicOr(&out[w + 1], code >> (32 - s));
    }
}

void append_bits(std::vector<uint8_t>& dst, uint64_t& dst_bits,
                 const uint8_t* src, uint64_t src_bits) {
    if (!src_bits) return;
    size_t need = (size_t)((dst_bits + src_bits + 7) / 8);
    if (dst.size() < need) dst.resize(need, 0);
    size_t shift = (size_t)(dst_bits % 8), doff = (size_t)(dst_bits / 8);
    size_t nsrc = (size_t)((src_bits + 7) / 8);
    if (shift == 0) {
        memcpy(dst.data() + doff, src, nsrc);
    } else {
        for (size_t i = 0; i < nsrc; i++) {
            uint16_t v = src[i];
            dst[doff + i] = (uint8_t)(dst[doff + i] | (v << shift));
            if (doff + i + 1 < dst.size())
                dst[doff + i + 1] = (uint8_t)(dst[doff + i + 1] | (v >> (8 - shift)));
        }
    }
    dst_bits += src_bits;
}

} // namespace

std::array<uint64_t,256> histogram_gpu(const uint8_t* data, size_t n) {
    std::array<uint64_t,256> h{}; h.fill(0);
    if (n == 0) return h;
    size_t C = chunk_override();
    uint8_t* d = nullptr; unsigned long long* g = nullptr;
    try {
    GCF_CUDA_CHECK(cudaMalloc(&d, C));
    GCF_CUDA_CHECK(cudaMalloc(&g, 256 * sizeof(unsigned long long)));
    GCF_CUDA_CHECK(cudaMemset(g, 0, 256 * sizeof(unsigned long long)));
    for (size_t pos = 0; pos < n; pos += C) {
        size_t m = (n - pos < C) ? n - pos : C;
        GCF_CUDA_CHECK(cudaMemcpy(d, data + pos, m, cudaMemcpyHostToDevice));
        hist_kernel<<<256, 512>>>(d, m, g);
        GCF_CUDA_CHECK(cudaGetLastError());
        GCF_CUDA_CHECK(cudaDeviceSynchronize());
    }
    unsigned long long tmp[256];
    GCF_CUDA_CHECK(cudaMemcpy(tmp, g, sizeof tmp, cudaMemcpyDeviceToHost));
    GCF_CUDA_CHECK(cudaFree(d)); d = nullptr;
    GCF_CUDA_CHECK(cudaFree(g)); g = nullptr;
    for (int i = 0; i < 256; i++) h[i] = tmp[i];
    } catch (...) {
        // cudaMalloc dan keyin GCF_CUDA_CHECK throw qilsa leak bo'lardi — tozalaymiz
        if (d) cudaFree(d);
        if (g) cudaFree(g);
        throw;
    }
    return h;
}

void encode_gpu(const uint8_t* data, size_t n, const std::array<HuffCode,256>& table,
                std::vector<uint8_t>& out, uint64_t& total_bits) {    out.clear(); total_bits = 0;
    if (n == 0) return;
    uint32_t codes[256]; uint8_t lens[256];
    for (int i = 0; i < 256; i++) { codes[i] = table[i].bits; lens[i] = table[i].len; }

    size_t C = chunk_override();
    uint8_t *d_data = nullptr, *d_lens8 = nullptr;
    uint32_t *d_lens = nullptr, *d_off = nullptr, *d_codes = nullptr, *d_out = nullptr;
    std::vector<uint8_t> merged; uint64_t merged_bits = 0;
    try {
    GCF_CUDA_CHECK(cudaMalloc(&d_data, C));
    GCF_CUDA_CHECK(cudaMalloc(&d_lens, C * sizeof(uint32_t)));
    GCF_CUDA_CHECK(cudaMalloc(&d_off, C * sizeof(uint32_t)));
    GCF_CUDA_CHECK(cudaMalloc(&d_codes, 256 * sizeof(uint32_t)));
    GCF_CUDA_CHECK(cudaMalloc(&d_lens8, 256));
    GCF_CUDA_CHECK(cudaMemcpy(d_codes, codes, sizeof codes, cudaMemcpyHostToDevice));
    GCF_CUDA_CHECK(cudaMemcpy(d_lens8, lens, sizeof lens, cudaMemcpyHostToDevice));

    const int T = 512, B = 1024;
    for (size_t pos = 0; pos < n; pos += C) {
        size_t m = (n - pos < C) ? n - pos : C;
        GCF_CUDA_CHECK(cudaMemcpy(d_data, data + pos, m, cudaMemcpyHostToDevice));
        lens_kernel<<<(m + T - 1) / T > (size_t)B ? B : (int)((m + T - 1) / T), T>>>(
            d_data, d_lens8, d_lens, m);
        GCF_CUDA_CHECK(cudaGetLastError());
        thrust::device_ptr<uint32_t> in_p(d_lens), out_p(d_off);
        thrust::exclusive_scan(in_p, in_p + m, out_p);
        GCF_CUDA_CHECK(cudaDeviceSynchronize());
        uint32_t last_off = 0, last_len = 0;
        GCF_CUDA_CHECK(cudaMemcpy(&last_off, d_off + m - 1, 4, cudaMemcpyDeviceToHost));
        GCF_CUDA_CHECK(cudaMemcpy(&last_len, d_lens + m - 1, 4, cudaMemcpyDeviceToHost));
        uint64_t cb = (uint64_t)last_off + last_len;
        size_t words = (size_t)((cb + 31) / 32) + 1;
        GCF_CUDA_CHECK(cudaMalloc(&d_out, words * sizeof(uint32_t)));
        GCF_CUDA_CHECK(cudaMemset(d_out, 0, words * sizeof(uint32_t)));
        encode_kernel<<<B, T>>>(d_data, d_off, d_codes, d_lens8, d_out, m);
        GCF_CUDA_CHECK(cudaGetLastError());
        GCF_CUDA_CHECK(cudaDeviceSynchronize());
        std::vector<uint8_t> chunk_bytes(words * 4);
        GCF_CUDA_CHECK(cudaMemcpy(chunk_bytes.data(), d_out, words * 4, cudaMemcpyDeviceToHost));
        GCF_CUDA_CHECK(cudaFree(d_out)); d_out = nullptr;
        append_bits(merged, merged_bits, chunk_bytes.data(), cb);
    }
    GCF_CUDA_CHECK(cudaFree(d_data)); GCF_CUDA_CHECK(cudaFree(d_lens)); GCF_CUDA_CHECK(cudaFree(d_off));
    GCF_CUDA_CHECK(cudaFree(d_codes)); GCF_CUDA_CHECK(cudaFree(d_lens8));
    } catch (...) {
        if (d_data) cudaFree(d_data);
        if (d_lens) cudaFree(d_lens);
        if (d_off) cudaFree(d_off);
        if (d_codes) cudaFree(d_codes);
        if (d_lens8) cudaFree(d_lens8);
        if (d_out) cudaFree(d_out);
        throw;
    }
    // Oxirgi bayt ortiqcha bitlari tozalansin (CPU bilan bir xil)
    if (merged_bits % 8 && !merged.empty())
        merged.back() &= (uint8_t)(0xFF >> (8 - merged_bits % 8));
    out = std::move(merged); total_bits = merged_bits;
}

// ---------- LZ77 takror-qidiruvchi (Deflate uslubi: 32KB oyna, 3..258) ----------
// Har pozitsiyaga 2 ta nomzod (2 xil hash-jadval). Jadval raceli to'ladi —
// CPU parse har nomzodni baytma-bayt tekshiradi, shuning uchun har doim to'g'ri.
__global__ void lz_hash_kernel(const uint8_t* __restrict__ d, size_t m,
                               uint32_t* __restrict__ t1, uint32_t* __restrict__ t2,
                               int32_t* __restrict__ c1, int32_t* __restrict__ c2) {
    size_t tid = blockIdx.x * blockDim.x + threadIdx.x;
    size_t stride = (size_t)blockDim.x * gridDim.x;
    for (size_t i = tid; i < m; i += stride) {
        int32_t a = -1, b = -1;
        if (i + 4 <= m) {
            uint32_t w = (uint32_t)d[i] | ((uint32_t)d[i + 1] << 8) |
                         ((uint32_t)d[i + 2] << 16) | ((uint32_t)d[i + 3] << 24);
            uint32_t p1 = atomicExch(&t1[(w * 2654435761u) >> 12], (uint32_t)i);
            uint32_t p2 = atomicExch(&t2[(w * 2246822519u) >> 12], (uint32_t)i);
            if (p1 != 0xFFFFFFFFu) a = (int32_t)p1;
            if (p2 != 0xFFFFFFFFu) b = (int32_t)p2;
        }
        c1[i] = a; c2[i] = b;
    }
}

namespace {
struct LzBufs {
    uint8_t* data = nullptr;
    uint32_t *t1 = nullptr, *t2 = nullptr;
    int32_t *c1 = nullptr, *c2 = nullptr;
    size_t cap = 0;
    ~LzBufs() {
        // Thread exit / program exit da CUDA context allaqachon o'lgan bo'lishi mumkin.
        // cudaFree xatosi crash emas (kod qaytaradi), lekin driver unload bo'lgan bo'lsa
        // chaqirmaslik xavfsizroq — shuning uchun device borligini tekshiramiz.
        int n = 0;
        bool cuda_ok = (cudaGetDeviceCount(&n) == cudaSuccess);
        if (!cuda_ok) { data = t1 = t2 = nullptr; c1 = c2 = nullptr; return; }
        if (data) cudaFree(data);
        if (t1) cudaFree(t1);
        if (t2) cudaFree(t2);
        if (c1) cudaFree(c1);
        if (c2) cudaFree(c2);
    }
};
thread_local LzBufs tls_lz;
} // namespace

// Chunk [pos, pos+m) uchun nomzodlar (host buferlarga). Har oqim o'z buferi bilan.
// DIQQAT: bu kernel racy (parallel atomicExch tartibi noaniq) — to'g'ri lekin
// siqish darajasi CPU deterministik versiyadan 3-4x yomon. Shuning uchun default
// o'chirilgan, faqat GCF_GPU_LZ=1 bo'lsa gpu_compressor.cpp dan chaqiriladi.
void lz_find_candidates(const uint8_t* data, size_t pos, size_t m,
                        int32_t* out_c1, int32_t* out_c2) {
    if (m == 0) return;
    if (tls_lz.cap < m) {
        if (tls_lz.data) { cudaFree(tls_lz.data); cudaFree(tls_lz.c1); cudaFree(tls_lz.c2); }
        tls_lz.data = nullptr; tls_lz.c1 = tls_lz.c2 = nullptr;
        GCF_CUDA_CHECK(cudaMalloc(&tls_lz.data, m));
        GCF_CUDA_CHECK(cudaMalloc(&tls_lz.c1, m * sizeof(int32_t)));
        GCF_CUDA_CHECK(cudaMalloc(&tls_lz.c2, m * sizeof(int32_t)));
        tls_lz.cap = m;
        if (!tls_lz.t1) {
            GCF_CUDA_CHECK(cudaMalloc(&tls_lz.t1, (1u << 20) * sizeof(uint32_t)));
            GCF_CUDA_CHECK(cudaMalloc(&tls_lz.t2, (1u << 20) * sizeof(uint32_t)));
        }
    }
    GCF_CUDA_CHECK(cudaMemset(tls_lz.t1, 0xFF, (1u << 20) * sizeof(uint32_t)));
    GCF_CUDA_CHECK(cudaMemset(tls_lz.t2, 0xFF, (1u << 20) * sizeof(uint32_t)));
    GCF_CUDA_CHECK(cudaMemcpy(tls_lz.data, data + pos, m, cudaMemcpyHostToDevice));
    lz_hash_kernel<<<1024, 512>>>(tls_lz.data, m, tls_lz.t1, tls_lz.t2, tls_lz.c1, tls_lz.c2);
    GCF_CUDA_CHECK(cudaGetLastError());
    GCF_CUDA_CHECK(cudaDeviceSynchronize());
    GCF_CUDA_CHECK(cudaMemcpy(out_c1, tls_lz.c1, m * sizeof(int32_t), cudaMemcpyDeviceToHost));
    GCF_CUDA_CHECK(cudaMemcpy(out_c2, tls_lz.c2, m * sizeof(int32_t), cudaMemcpyDeviceToHost));
}
#endif
