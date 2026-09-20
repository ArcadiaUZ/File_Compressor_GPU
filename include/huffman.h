#pragma once
#include <array>
#include <cstdint>
#include <vector>

// Huffman kod jadvali: har bir bayt uchun (bit-kod, bit-uzunlik)
struct HuffCode { uint32_t bits = 0; uint8_t len = 0; };

// freq[256] dan Huffman daraxti qurib, jadval chiqaradi
std::array<HuffCode, 256> build_huffman_table(const std::array<uint64_t,256>& freq);

// CPU da histogram (GPU bo'lmasa shu ishlaydi, ko'p oqimli emas — sodda va to'g'ri)
std::array<uint64_t,256> histogram_cpu(const uint8_t* data, size_t n);

#ifdef USE_CUDA
// GPU da histogram + encode (kernels.cu da). Mavjud bo'lmasa CPU ishlaydi.
std::array<uint64_t,256> histogram_gpu(const uint8_t* data, size_t n);
// table bilan GPU da bit-packing; out/total_bits CPU bilan bir xil formatda.
// table bilan GPU da bit-packing; out/total_bits CPU bilan bir xil formatda.
void encode_gpu(const uint8_t* data, size_t n, const std::array<HuffCode,256>& table,
                std::vector<uint8_t>& out, uint64_t& total_bits);
// LZ77 nomzodlar: chunk dagi har pozitsiyaga 2 ta oldingi o'rin (-1 = yo'q).
// Pozitsiyalar chunk boshiga nisbatan. thread-safe (har oqim o'z buferi).
void lz_find_candidates(const uint8_t* data, size_t pos, size_t m,
                        int32_t* out_c1, int32_t* out_c2);
#endif
