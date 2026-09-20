#pragma once
#include "gpu_info.h"
#include <cstdint>
#include <string>
#include <vector>

// .gcf formatlari:
//   v1 (GCF1): bitta fayl, nomsiz [magic][orig][jadval][bit_len][oqim]
//   v2 (GCF2): arxiv — bir nechta nomli fayl
//              [magic][n][har fayl: name_len u16, name, orig u64, bits u64,
//               stream_bytes u64, jadval 2048B][oqimlar ketma-ket]
//   v3 (GCF3): joriy yoziladigan format — v2 + method u8 (0=Huffman,1=store,2=LZ77+Huffman)
//              method==2 da chunklar: [nch][har chunk: nTokens u32, fb u32, lit_bits u64,
//              lb u64, mb u32, jadval 2048B][flags/lit/match oqimlari]
// Har doim v3 yoziladi; o'qishda v1/v2/v3 hammasi tushuniladi.

struct CompressStats {
    size_t in_bytes = 0, out_bytes = 0;
    double seconds = 0;
    std::string backend; // "CUDA-RTX3060" yoki "CPU-fallback"
    double ratio() const { return in_bytes ? (double)out_bytes / in_bytes : 0; }
};

struct GcfEntryInfo {
    std::string name;
    uint64_t orig_size = 0, packed_bytes = 0;
    std::string method = "huff"; // huff | store | lz
    double ratio() const { return orig_size ? (double)packed_bytes / orig_size : 0; }
};

// Bitta fayl -> v2 arxiv (bitta yozuv bilan). Eski v1 ni o'qiyveradi.
CompressStats gcf_compress(const std::string& in_path, const std::string& out_path,
                           const GpuConfig& cfg);
// v1 yoki v2-bitta-yozuvli arxivni ochish. Ko'p faylli bo'lsa xato beradi.
CompressStats gcf_decompress(const std::string& in_path, const std::string& out_path);
// Bir nechta fayldan arxiv yasash
CompressStats gcf_compress_multi(const std::vector<std::string>& in_paths,
                                 const std::string& out_path, const GpuConfig& cfg);
// Papkani rekursiv arxivlash (ichki yo'llar saqlanadi: sub/fayl.txt)
CompressStats gcf_compress_folder(const std::string& in_dir, const std::string& out_path,
                                  const GpuConfig& cfg);
// Arxiv ichidagilar ro'yxati
std::vector<GcfEntryInfo> gcf_list(const std::string& path);
// Chiqarish: indices bo'sh bo'lsa hammasi. out_dir ga yozadi.
CompressStats gcf_extract(const std::string& arch_path, const std::string& out_dir,
                          const std::vector<int>& indices);
// Arxivga fayl qo'shish (bo'lmasa yangisini yaratadi)
void gcf_add(const std::string& arch_path, const std::vector<std::string>& files,
             const GpuConfig& cfg);
// Arxivdan yozuv o'chirish (indekslar bo'yicha)
void gcf_remove(const std::string& arch_path, const std::vector<int>& indices);
// Arxiv butunligini tekshirish (hamma yozuv dekodlanib o'lcham solishtiriladi)
std::string gcf_test(const std::string& arch_path);
// Bitta yozuvni dekodlab baytlarga (GUI/extract ichki ishi)
std::vector<uint8_t> gcf_read_entry(const std::string& arch_path, int index);
void gcf_info(const std::string& path);
