#include "gpu_compressor.h"
#include "huffman.h"
#include <algorithm>
#include <atomic>
#include <cctype>
#include <chrono>
#include <cstdlib>
#include <cstring>
#include <exception>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <mutex>
#include <thread>
#include <vector>
#ifdef USE_CUDA
#include <cuda_runtime.h>
#endif

namespace fs = std::filesystem;

// --- Windows Unicode (UTF-8) mosligi ---
// Windows da narrow std::string bilan ifstream ANSI codepage ishlatadi,
// shuning uchun kirill/emoji/bo'shliq yo'llar buziladi ("????").
//fs_path() UTF-8 satrni to'g'ri wide yo'lga o'tkazadi; fayl ochishda doim shu ishlatiladi.
#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
static fs::path fs_path(const std::string& s) {
    if (s.empty()) return fs::path();
    int wlen = MultiByteToWideChar(CP_UTF8, 0, s.c_str(), -1, nullptr, 0);
    if (wlen <= 1) return fs::path(s);
    std::wstring w((size_t)wlen - 1, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, s.c_str(), -1, w.data(), wlen);
    return fs::path(w);
}
static std::ifstream open_in_bin(const std::string& p) {
    return std::ifstream(fs_path(p).wstring(), std::ios::binary);
}
static std::ifstream open_in_bin_ate(const std::string& p) {
    return std::ifstream(fs_path(p).wstring(), std::ios::binary | std::ios::ate);
}
static std::ofstream open_out_bin(const std::string& p) {
    return std::ofstream(fs_path(p).wstring(), std::ios::binary);
}
#else
static fs::path fs_path(const std::string& s) { return fs::path(s); }
static std::ifstream open_in_bin(const std::string& p) {
    return std::ifstream(p, std::ios::binary);
}
static std::ifstream open_in_bin_ate(const std::string& p) {
    return std::ifstream(p, std::ios::binary | std::ios::ate);
}
static std::ofstream open_out_bin(const std::string& p) {
    return std::ofstream(p, std::ios::binary);
}
#endif

// Arxiv ichidagi nom xavfsizligini tekshirish: faqat ".." komponent bo'lsa bloklaymiz.
// Oldin `find("..")` edi — "my..file.txt" kabi qonuniy nomlarni ham xato bloklardi.
static bool is_unsafe_entry_name(const std::string& name) {
    if (name.empty() || name == "." || name == "..") return true;
    if (name[0] == '/' || name[0] == '\\') return true;
    if (name.find(':') != std::string::npos) return true; // Windows drive "C:" / ADS "f:stream"
    // Windows: oxirgi bo'shliq/nuqta, maxsus nomlar (CON, PRN, AUX, NUL, COM1-9, LPT1-9)
    if (!name.empty() && (name.back() == ' ' || name.back() == '.')) return true;
    size_t i = 0;
    bool has_real = false;
    while (i <= name.size()) {
        size_t j = name.find_first_of("/\\", i);
        std::string comp = (j == std::string::npos) ? name.substr(i) : name.substr(i, j - i);
        if (comp == "..") return true;
        if (comp == "." || comp.empty()) {
            // "a//b" yoki "a/./b" — normalizatsiyadan keyin xavfsiz bo'lsa ham,
            // chalkashlik oldini olish uchun rad etamiz (qonuniy arxivlar bunday nom ishlatmaydi)
            // Lekin bo'sh comp faqat oxirida "/" bo'lsa (papka belgisi) ruxsat.
            bool is_last = (j == std::string::npos);
            if (!(comp.empty() && is_last && has_real)) return true;
        } else {
            has_real = true;
            // Windows rezerv nomlari (case-insensitive, kengaytmasiz qism)
            std::string up = comp;
            std::transform(up.begin(), up.end(), up.begin(), ::toupper);
            size_t dot = up.find('.');
            std::string base = (dot == std::string::npos) ? up : up.substr(0, dot);
            if (base == "CON" || base == "PRN" || base == "AUX" || base == "NUL") return true;
            if ((base.size() == 4 && (base.compare(0, 3, "COM") == 0 || base.compare(0, 3, "LPT") == 0) &&
                 base[3] >= '1' && base[3] <= '9')) return true;
        }
        if (j == std::string::npos) break;
        i = j + 1;
    }
    if (!has_real) return true;
    return false;
}

// Xavfsiz limitlar (DoS/OOM himoyasi): arxivdan kelgan e'lon qilingan o'lchamlar
// fayl qoldig'i va oqilona cap bilan tekshiriladi.
static constexpr uint64_t GCF_MAX_ORIG_SINGLE = (8ULL << 30);   // bitta yozuv max 8 GB
static constexpr uint64_t GCF_MAX_STREAM      = (8ULL << 30);   // bitta oqim max 8 GB
static constexpr uint32_t GCF_MAX_ENTRIES     = 10000;          // arxivda max 10000 fayl
static constexpr uint32_t GCF_MAX_CHUNKS      = 10000;          // bitta faylda max chunk

// LZ yozuv uchun siqilgan hajm (statistika/info/extract uchun yagona formula)
static uint64_t packed_size_of(const PackedFile& e);

static const char MAGIC1[4] = {'G','C','F','1'};
static const char MAGIC2[4] = {'G','C','F','2'};
static const char MAGIC3[4] = {'G','C','F','3'};

struct PackedFile {
    std::string name;
    uint64_t orig = 0;
    uint8_t method = 0; // 0 = Huffman, 1 = raw saqlash (siqilmaydigan fayl uchun)
    std::array<HuffCode,256> table{};
    uint64_t total_bits = 0;
    std::vector<uint8_t> stream;
    // method 2 = LZ77+Huffman (chunklar)
    struct LzChunk {
        uint32_t nTokens = 0;
        std::vector<uint8_t> flags;    // 1 bit/token
        std::vector<uint8_t> lits;     // literallar (siqilmagan, vaqtincha)
        std::array<HuffCode,256> lit_table{};
        uint64_t lit_bits = 0;
        std::vector<uint8_t> lit_stream;
        std::vector<uint8_t> matches;  // 3 bayt/match
    };
    std::vector<LzChunk> lz;
};

static const uint32_t LZ_WINDOW = 32768, LZ_MIN = 3, LZ_MAX = 258;

// CPU finder — har doim deterministik va tez.
// GPU kerneli (atomicExch, racy) bir xil faylda 3-4x yomonroq ratio beradi
// va hatto sekinroq (12 MB test: CPU 0.08s/158KB vs GPU 0.55s/597KB),
// shuning uchun default CPU ishlatiladi. GPU faqat GCF_GPU_LZ=1 bo'lsa.
static void lz_find_candidates_cpu(const uint8_t* data, size_t pos, size_t m,
                               int32_t* o1, int32_t* o2) {
    static thread_local std::vector<int32_t> t1, t2;
    if (t1.size() != (1u << 20)) { t1.assign(1u << 20, -1); t2.assign(1u << 20, -1); }
    else { std::fill(t1.begin(), t1.end(), -1); std::fill(t2.begin(), t2.end(), -1); }
    for (size_t i = 0; i < m; i++) {
        int32_t a = -1, b = -1;
        if (i + 4 <= m) {
            const uint8_t* d = data + pos + i;
            uint32_t w = (uint32_t)d[0] | ((uint32_t)d[1] << 8) |
                         ((uint32_t)d[2] << 16) | ((uint32_t)d[3] << 24);
            uint32_t h1 = (w * 2654435761u) >> 12, h2 = (w * 2246822519u) >> 12;
            a = t1[h1]; t1[h1] = (int32_t)i;
            b = t2[h2]; t2[h2] = (int32_t)i;
        }
        o1[i] = a; o2[i] = b;
    }
}
#ifndef USE_CUDA
// CPU-only yig'ilishda eski nom ham shu funksiyaga yo'naltiriladi
static void lz_find_candidates(const uint8_t* data, size_t pos, size_t m,
                               int32_t* o1, int32_t* o2) {
    lz_find_candidates_cpu(data, pos, m, o1, o2);
}
#endif

static size_t lz_match_len(const uint8_t* d, size_t m, size_t pos, int c) {
    if (c < 0 || (size_t)c >= pos || pos - (size_t)c > LZ_WINDOW) return 0;
    size_t k = 0;
    while (k < LZ_MAX && pos + k < m && d[(size_t)c + k] == d[pos + k]) k++;
    return k;
}

// Ochko'z parse + 1 qadam lazy (Deflate uslubi). Nomzodlar tekshirilgan.
static void lz_parse_chunk(const uint8_t* d, size_t m, const int32_t* c1, const int32_t* c2,
                           PackedFile::LzChunk& out) {
    std::vector<uint8_t> flags; flags.reserve(m / 4 + 1);
    uint8_t facc = 0; int fn = 0; uint32_t ntok = 0;
    auto push_flag = [&](int bit) {
        if (bit) facc |= (uint8_t)(1u << fn);
        if (++fn == 8) { flags.push_back(facc); facc = 0; fn = 0; }
        ntok++;
    };
    size_t i = 0;
    while (i < m) {
        size_t la = lz_match_len(d, m, i, c1[i]), lb = lz_match_len(d, m, i, c2[i]);
        size_t best = la > lb ? la : lb;
        int bc = la > lb ? c1[i] : c2[i];
        if (best >= LZ_MIN && i + 1 < m) {
            size_t a2 = lz_match_len(d, m, i + 1, c1[i + 1]);
            size_t b2 = lz_match_len(d, m, i + 1, c2[i + 1]);
            size_t best2 = a2 > b2 ? a2 : b2;
            if (best2 > best + 1) { push_flag(0); out.lits.push_back(d[i]); i++; continue; }
        }
        if (best >= LZ_MIN) {
            push_flag(1);
            uint32_t o = (uint32_t)(i - (size_t)bc) - 1; // 0..32767
            uint32_t L = (uint32_t)best - 3;             // 0..255
            out.matches.push_back((uint8_t)(o & 0xFF));
            out.matches.push_back((uint8_t)(((o >> 8) & 0x7F) | ((L & 1) << 7)));
            out.matches.push_back((uint8_t)(L >> 1));
            i += best;
        } else { push_flag(0); out.lits.push_back(d[i]); i++; }
    }
    if (fn) flags.push_back(facc);
    out.flags = std::move(flags); out.nTokens = ntok;
}

static size_t lz_chunk_size() {
    if (const char* e = std::getenv("GCF_LZ_CHUNK_KB")) {
        long v = std::atol(e);
        // 16 MB cap: 64 MB * 8 thread = 512 MB/thread nusxa (c1+c2 int32) OOM qiladi.
        // Katta chunk kerak bo'lsa ham 16 MB yetadi (ratio deyarli bir xil).
        if (v >= 16 && v <= 16384) return (size_t)v << 10;
    }
    return 4ULL << 20;
}

static PackedFile::LzChunk lz_process_chunk(const uint8_t* base, size_t pos, size_t m) {
    // GPU bitta — concurrent thrust/kernel chaqiruvlar race qiladi,
    // shuning uchun GPU ishlar navbat bilan (CPU parse parallel qoladi).
    static std::mutex gpu_mutex;
    std::vector<int32_t> c1(m), c2(m);
#ifdef USE_CUDA
    // GCF_GPU_LZ=1 bo'lmasa deterministik CPU finder (yaxshi ratio + tezroq)
    if (const char* e = std::getenv("GCF_GPU_LZ"); e && e[0] == '1') {
        std::lock_guard<std::mutex> lk(gpu_mutex);
        lz_find_candidates(base, pos, m, c1.data(), c2.data());
    } else {
        lz_find_candidates_cpu(base, pos, m, c1.data(), c2.data());
    }
#else
    lz_find_candidates_cpu(base, pos, m, c1.data(), c2.data());
#endif
    PackedFile::LzChunk ch;
    lz_parse_chunk(base + pos, m, c1.data(), c2.data(), ch);
    auto f = histogram_cpu(ch.lits.data(), ch.lits.size());
    ch.lit_table = build_huffman_table(f);
#ifdef USE_CUDA
    { std::lock_guard<std::mutex> lk(gpu_mutex);
      encode_gpu(ch.lits.data(), ch.lits.size(), ch.lit_table, ch.lit_stream, ch.lit_bits); }
#else
    // CPU bit-packing: acc 64-bit bo'lishi shart (len 32 gacha + an 0..7 = 39 bit).
    // Oldin uint32_t edi — yuqori bitlar kesilib CPU/GPU format diverjensi bo'lardi.
    uint64_t acc = 0; int an = 0; uint64_t bits = 0;
    for (uint8_t b : ch.lits) {
        if (ch.lit_table[b].len == 0 || ch.lit_table[b].len > 32)
            throw std::runtime_error("buzilgan Huffman jadval (lz lit)");
        acc |= ((uint64_t)ch.lit_table[b].bits << an); an += ch.lit_table[b].len; bits += ch.lit_table[b].len;
        while (an >= 8) { ch.lit_stream.push_back((uint8_t)acc); acc >>= 8; an -= 8; }
    }
    if (an > 0) ch.lit_stream.push_back((uint8_t)acc);
    ch.lit_bits = bits;
#endif
    ch.lits.clear(); ch.lits.shrink_to_fit(); // xotira bo'shasin
    return ch;
}

static std::vector<PackedFile::LzChunk> lz_process_all(const uint8_t* data, size_t n, size_t C) {
    size_t nch = (n + C - 1) / C;
    std::vector<PackedFile::LzChunk> res(nch);
    unsigned T = std::thread::hardware_concurrency();
    if (!T) T = 4; if (T > 8) T = 8;
    std::atomic<size_t> next{0};
    std::exception_ptr err;
    std::mutex err_mu; // exception_ptr data race himoyasi (bir nechta worker bir vaqtda yozardi)
    std::atomic<bool> failed{false};
    auto worker = [&]() {
        try {
#ifdef USE_CUDA
            // Har oqim o'z CUDA kontekstini tanlashi shart; best device id ishlatiladi (0 hardcode emas)
            // cudaSetDevice xatosi bo'lsa CPU ga tushamiz (worker ichida throw -> err ga)
#endif
            for (;;) {
                if (failed.load()) break;
                size_t k = next.fetch_add(1);
                if (k >= nch) break;
                size_t pos = k * C, m = (n - pos < C) ? n - pos : C;
                res[k] = lz_process_chunk(data, pos, m);
                { std::lock_guard<std::mutex> lk(err_mu); if (err) break; }
                if (failed.load()) break;
            }
        } catch (...) {
            std::lock_guard<std::mutex> lk(err_mu);
            if (!err) err = std::current_exception();
            failed.store(true);
            next = nch;
        }
    };
    std::vector<std::thread> ts;
    for (unsigned i = 0; i < T; i++) ts.emplace_back(worker);
    for (auto& t : ts) t.join();
    if (err) std::rethrow_exception(err);
    return res;
}

// Eng kichik usulni tanlash: 2 = LZ+Huffman, 0 = Huffman, 1 = raw
static void pack_choose_best(const std::vector<uint8_t>& data, const GpuConfig* cfg,
                             std::string* backend_out, PackedFile& e) {
    e.orig = data.size();
    auto set_backend = [&]() {
        if (!backend_out) return;
        if (!cfg) { *backend_out = "CPU-fallback"; return; }
#ifdef USE_CUDA
        *backend_out = "CUDA-" + cfg->gpu_name;
#else
        *backend_out = "CPU-fallback (" + cfg->gpu_name + ")";
#endif
    };
    set_backend();
    if (data.empty()) { e.method = 1; e.stream.clear(); e.total_bits = 0; return; }
    // GPU build bo'lsa ham runtime da drayver/GPU bo'lmasligi mumkin.
    // cuda_available==false bo'lsa to'g'ridan-to'g'ri CPU yo'l (throw siz).
    bool use_gpu = false;
#ifdef USE_CUDA
    use_gpu = (cfg && cfg->cuda_available);
#endif
    std::array<uint64_t,256> freq{};
    try {
#ifdef USE_CUDA
        if (use_gpu) freq = histogram_gpu(data.data(), data.size());
        else freq = histogram_cpu(data.data(), data.size());
#else
        freq = histogram_cpu(data.data(), data.size());
        (void)cfg;
#endif
    } catch (...) {
        // GPU histogram xatosi (drayver yo'q / VRAM to'la) -> CPU fallback
#ifdef USE_CUDA
        if (use_gpu) {
            use_gpu = false;
            freq = histogram_cpu(data.data(), data.size());
            if (backend_out && cfg) *backend_out = "CPU-fallback (" + cfg->gpu_name + ")";
        } else throw;
#else
        throw;
#endif
    }
    std::array<HuffCode,256> table_all{};
    try {
        table_all = build_huffman_table(freq);
    } catch (...) {
        // Huffman daraxti qurilmasa (juda chuqur) -> raw saqlash (ma'lumot yo'qotmaslik)
        e.method = 1; e.stream = data; e.total_bits = 0;
        return;
    }
    // Validatsiya: chastotasi bor simvol kodsiz qolmasin (jim korrupsiya himoyasi)
    for (int i = 0; i < 256; i++) {
        if (freq[i] && (table_all[i].len == 0 || table_all[i].len > 32)) {
            e.method = 1; e.stream = data; e.total_bits = 0;
            return;
        }
    }
    uint64_t huff_bits = 0;
    for (int i = 0; i < 256; i++) huff_bits += freq[i] * table_all[i].len;
    uint64_t huff_size = (huff_bits + 7) / 8 + 2048;

    auto chunks = lz_process_all(data.data(), data.size(), lz_chunk_size());
    uint64_t lz_size = 4; // num_chunks
    for (auto& ch : chunks)
        lz_size += 28 + 2048 + ch.flags.size() + ch.lit_stream.size() + ch.matches.size();

    if (lz_size < huff_size && lz_size < e.orig) {
        e.method = 2; e.lz = std::move(chunks);
    } else if (huff_size < e.orig) {
        e.method = 0; e.table = table_all;
        bool gpu_ok = false;
#ifdef USE_CUDA
        if (use_gpu) {
            try { encode_gpu(data.data(), data.size(), table_all, e.stream, e.total_bits); gpu_ok = true; }
            catch (...) { gpu_ok = false; } // GPU encode xatosi -> pastda CPU ga tushamiz
        }
        if (!gpu_ok) {
#else
        {
#endif
        uint64_t acc = 0; int an = 0; uint64_t bits = 0;
        for (uint8_t b : data) {
            acc |= ((uint64_t)table_all[b].bits << an); an += table_all[b].len; bits += table_all[b].len;
            while (an >= 8) { e.stream.push_back((uint8_t)acc); acc >>= 8; an -= 8; }
        }
        if (an > 0) e.stream.push_back((uint8_t)acc);
        e.total_bits = bits;
#ifdef USE_CUDA
        }
#else
        }
#endif
    } else {
        e.method = 1; e.stream = data; e.total_bits = 0; // Store
    }
}

static std::string basename_of(const std::string& p) {
    size_t i = p.find_last_of("/\\");
    std::string b = (i == std::string::npos) ? p : p.substr(i + 1);
    return b.empty() ? p : b;
}

static uint64_t packed_size_of(const PackedFile& e) {
    if (e.method == 2) {
        uint64_t s = 4;
        for (auto& ch : e.lz)
            s += 28 + 2048 + ch.flags.size() + ch.lit_stream.size() + ch.matches.size();
        return s;
    }
    return (uint64_t)e.stream.size();
}

static std::vector<uint8_t> read_all(const std::string& path) {
    // Katta fayl OOM himoyasi: bitta faylni butunlay RAM ga olamiz,
    // shuning uchun 2 GB dan kattasini rad etamiz (papka/chunk oqimi hali yo'q).
    std::error_code ec;
    uintmax_t fsz = fs::file_size(fs_path(path), ec);
    if (!ec) {
        if (!fs::is_regular_file(fs_path(path), ec))
            throw std::runtime_error("maxsus fayl siqilmaydi (faqat oddiy fayl): " + path);
        if (fsz > (2ULL << 30))
            throw std::runtime_error("fayl juda katta (>2 GB, bitta yozuv limiti): " + path);
    }
    std::ifstream f = open_in_bin(path);
    if (!f) throw std::runtime_error("fayl ochilmadi: " + path);
    return std::vector<uint8_t>((std::istreambuf_iterator<char>(f)),
                                 std::istreambuf_iterator<char>());
}

// Ma'lumot -> Huffman jadval + bit oqimi (GPU/CPU histogram bilan)
static void pack_data(const std::vector<uint8_t>& data, const GpuConfig* cfg,
                      std::string* backend_out,
                      std::array<HuffCode,256>* table_out,
                      uint64_t* bits_out, std::vector<uint8_t>* stream_out) {
    std::array<uint64_t,256> freq{};
#ifdef USE_CUDA
    freq = histogram_gpu(data.data(), data.size());
    if (backend_out && cfg) *backend_out = "CUDA-" + cfg->gpu_name;
#else
    freq = histogram_cpu(data.data(), data.size());
    if (backend_out && cfg) *backend_out = "CPU-fallback (" + cfg->gpu_name + ")";
    (void)cfg;
#endif
    auto table = build_huffman_table(freq);
#ifdef USE_CUDA
    // Encode to'liq GPU da (gistogramma kabi) — fayl GPU xotirasida qoladi
    std::vector<uint8_t> out; uint64_t bits = 0;
    encode_gpu(data.data(), data.size(), table, out, bits);
    *table_out = table; *bits_out = bits; *stream_out = std::move(out);
#else
    std::vector<uint8_t> out; out.reserve(data.size());
    uint64_t acc = 0; int acc_n = 0; uint64_t bits = 0;
    for (uint8_t b : data) {
        if (table[b].len == 0 || table[b].len > 32)
            throw std::runtime_error("buzilgan Huffman jadval");
        acc |= ((uint64_t)table[b].bits << acc_n); acc_n += table[b].len; bits += table[b].len;
        while (acc_n >= 8) { out.push_back((uint8_t)acc); acc >>= 8; acc_n -= 8; }
    }
    if (acc_n > 0) out.push_back((uint8_t)acc);
    *table_out = table; *bits_out = bits; *stream_out = std::move(out);
#endif
}

static PackedFile pack_file(const std::string& path, const GpuConfig& cfg, std::string* backend_out) {
    PackedFile e;
    e.name = basename_of(path);
    auto data = read_all(path);
    pack_choose_best(data, &cfg, backend_out, e);
    return e;
}

struct DecNode { int child[2] = {-1,-1}; int sym = -1; };
static std::vector<DecNode> build_decode_tree(const std::array<HuffCode,256>& table) {
    std::vector<DecNode> tree(1);
    for (int s = 0; s < 256; s++) {
        if (!table[s].len) continue;
        if (table[s].len > 32) throw std::runtime_error("buzilgan Huffman jadval (len>32)");
        int node = 0;
        for (int k = 0; k < table[s].len; k++) {
            int bit = (table[s].bits >> k) & 1;
            if (tree[node].child[bit] == -1) {
                tree[node].child[bit] = (int)tree.size();
                tree.push_back(DecNode{});
            }
            node = tree[node].child[bit];
        }
        tree[node].sym = s;
    }
    return tree;
}

struct BitWalker {
    const uint8_t* p; size_t nbytes;
    uint64_t pos = 0;
    int get() {
        size_t i = (size_t)(pos >> 3);
        // Kesilgan oqimda jim "0" qaytarish o'rniga aniq xato (aks holda sessiz korrupsiya).
        if (i >= nbytes) throw std::runtime_error("buzilgan lz oqimi (kesilgan lit_stream)");
        int b = (p[i] >> (pos & 7)) & 1;
        pos++;
        return b;
    }
};

// LZ77+Huffman chunk dekoder (overlap-safe nusxalash bilan)
static std::vector<uint8_t> decode_lz_entry(const PackedFile& e) {
    if (e.orig > GCF_MAX_ORIG_SINGLE)
        throw std::runtime_error("buzilgan arxiv (orig juda katta)");
    std::vector<uint8_t> out;
    if (e.orig) {
        try { out.reserve((size_t)e.orig); }
        catch (const std::bad_alloc&) { throw std::runtime_error("buzilgan arxiv (xotira yetmadi)"); }
    }
    for (auto& ch : e.lz) {
        // Har chunk validatsiyasi (flags OOB heap read himoyasi)
        if ((uint64_t)ch.flags.size() * 8 < ch.nTokens)
            throw std::runtime_error("buzilgan arxiv (lz flags kichik)");
        if (ch.lit_bits > (uint64_t)ch.lit_stream.size() * 8)
            throw std::runtime_error("buzilgan arxiv (lz lit_bits)");
        if (ch.nTokens > e.orig + 16)
            throw std::runtime_error("buzilgan arxiv (lz nTokens)");
        auto tree = build_decode_tree(ch.lit_table);
        BitWalker lit{ch.lit_stream.data(), ch.lit_stream.size()};
        size_t mpos = 0;
        for (uint32_t t = 0; t < ch.nTokens; t++) {
            if (out.size() > e.orig)
                throw std::runtime_error("buzilgan arxiv (lz o'lcham)");
            int flag = (ch.flags[t >> 3] >> (t & 7)) & 1;
            if (!flag) {
                int node = 0;
                for (;;) {
                    node = tree[node].child[lit.get()];
                    if (node < 0) throw std::runtime_error("buzilgan lz oqimi");
                    if (tree[node].sym >= 0) { out.push_back((uint8_t)tree[node].sym); break; }
                }
            } else {
                if (mpos + 3 > ch.matches.size()) throw std::runtime_error("buzilgan lz match");
                uint8_t b0 = ch.matches[mpos], b1 = ch.matches[mpos + 1], b2 = ch.matches[mpos + 2];
                mpos += 3;
                uint32_t off = (uint32_t)(b0 | ((b1 & 0x7F) << 8)) + 1;
                uint32_t len = (uint32_t)(((b1 >> 7) & 1) | (b2 << 1)) + 3;
                if (off == 0 || off > out.size()) throw std::runtime_error("buzilgan lz offset");
                // Overlap-safe: push_back realloc qilsa reference invalid bo'ladi,
                // shuning uchun har baytni joriy oxiridan o'qiymiz.
                for (uint32_t k = 0; k < len; k++) {
                    if (out.size() >= e.orig + 1)
                        throw std::runtime_error("buzilgan arxiv (lz o'lcham)");
                    out.push_back(out[out.size() - off]);
                }
            }
        }
        // lit_stream to'liq iste'mol qilinganini tekshirish (kesilgan/ortiqcha oqim himoyasi)
        // Harfiy tenglik shart emas (padding bitlar bo'lishi mumkin), lekin lit_bits dan
        // ortiq o'qilgan bo'lsa BitWalker allaqachon throw qilgan bo'ladi.
    }
    if (out.size() != e.orig) throw std::runtime_error("decompress: o'lcham mos kelmadi (lz)");
    return out;
}

static std::vector<uint8_t> decode_entry(const PackedFile& e) {
    if (e.method == 1) { // raw saqlangan — nusxalash kifoya
        if (e.stream.size() != e.orig) throw std::runtime_error("buzilgan arxiv (raw)");
        return e.stream;
    }
    if (e.method == 2) return decode_lz_entry(e);
    if (e.method != 0) throw std::runtime_error("buzilgan arxiv (method)");
    const auto& table = e.table;
    const auto& comp = e.stream;
    uint64_t total_bits = e.total_bits, orig = e.orig;
    if (orig > GCF_MAX_ORIG_SINGLE)
        throw std::runtime_error("buzilgan arxiv (orig juda katta)");
    if (total_bits > (uint64_t)comp.size() * 8)
        throw std::runtime_error("buzilgan arxiv (total_bits)");
    auto tree = build_decode_tree(table);
    std::vector<uint8_t> data;
    try { data.reserve((size_t)orig); }
    catch (const std::bad_alloc&) { throw std::runtime_error("buzilgan arxiv (xotira yetmadi)"); }
    size_t byte_pos = 0; int bit_pos = 0; uint64_t read = 0; int node = 0;
    while (data.size() < orig && read < total_bits) {
        if (byte_pos >= comp.size()) break;
        int bit = (comp[byte_pos] >> bit_pos) & 1;
        bit_pos++; read++;
        if (bit_pos == 8) { bit_pos = 0; byte_pos++; }
        node = tree[node].child[bit];
        if (node < 0) throw std::runtime_error("buzilgan bit oqimi");
        if (tree[node].sym >= 0) { data.push_back((uint8_t)tree[node].sym); node = 0; }
    }
    if (data.size() != orig) throw std::runtime_error("decompress: o'lcham mos kelmadi");
    return data;
}

static void write_table(std::ofstream& f, const std::array<HuffCode,256>& t) {
    for (int i = 0; i < 256; i++) {
        f.write((char*)&t[i].bits, 4);
        f.write((char*)&t[i].len, 1);
        char pad[3] = {0,0,0}; f.write(pad, 3);
    }
    if (!f) throw std::runtime_error("arxiv yozilmadi (disk to'la?)");
}
static void read_table(std::ifstream& f, std::array<HuffCode,256>& t) {
    for (int i = 0; i < 256; i++) {
        f.read((char*)&t[i].bits, 4);
        f.read((char*)&t[i].len, 1);
        f.ignore(3);
        if (!f) throw std::runtime_error("buzilgan arxiv (jadval)");
        if (t[i].len > 32) throw std::runtime_error("buzilgan arxiv (jadval len>32)");
    }
}

static void write_archive(const std::string& path, const std::vector<PackedFile>& entries) {
    if (entries.size() > GCF_MAX_ENTRIES)
        throw std::runtime_error("juda ko'p fayl (limit 10000)");
    // Atomik yozish: avval tmp ga, keyin rename (crash/disk-to'lganda asl arxiv saqlanadi).
    std::string tmp = path + ".tmp";
    {
        std::ofstream f = open_out_bin(tmp);
        if (!f) throw std::runtime_error("chiqish fayli ochilmadi: " + tmp);
        f.write(MAGIC3, 4);
        uint32_t n = (uint32_t)entries.size();
        f.write((char*)&n, 4);
    for (auto& e : entries) {
        if (e.name.size() > 65535) throw std::runtime_error("fayl nomi juda uzun: " + e.name);
        uint16_t nl = (uint16_t)e.name.size();
        f.write((char*)&nl, 2);
        f.write(e.name.data(), nl);
        f.write((char*)&e.orig, 8);
        f.write((char*)&e.method, 1);
        if (e.method == 2) {
            uint32_t nch = (uint32_t)e.lz.size();
            f.write((char*)&nch, 4);
            for (auto& ch : e.lz) {
                uint32_t fb = (uint32_t)ch.flags.size();
                uint64_t lb = ch.lit_stream.size();
                uint32_t mb = (uint32_t)ch.matches.size();
                f.write((char*)&ch.nTokens, 4);
                f.write((char*)&fb, 4);
                f.write((char*)&ch.lit_bits, 8);
                f.write((char*)&lb, 8);
                f.write((char*)&mb, 4);
                write_table(f, ch.lit_table);
            }
            for (auto& ch : e.lz) {
                if (!ch.flags.empty()) f.write((char*)ch.flags.data(), ch.flags.size());
                if (!ch.lit_stream.empty()) f.write((char*)ch.lit_stream.data(), ch.lit_stream.size());
                if (!ch.matches.empty()) f.write((char*)ch.matches.data(), ch.matches.size());
            }
            continue;
        }
        uint64_t sb = e.stream.size();
        f.write((char*)&e.total_bits, 8);
        f.write((char*)&sb, 8);
        if (e.method == 0) write_table(f, e.table); // raw da jadval kerak emas
        if (!f) throw std::runtime_error("arxiv yozilmadi (disk to'la?)");
    }
    for (auto& e : entries) {
        if (e.method == 2) continue; // yuqorida yozildi
        if (!e.stream.empty()) f.write((char*)e.stream.data(), e.stream.size());
        if (!f) throw std::runtime_error("arxiv yozilmadi (disk to'la?)");
    }
        f.flush();
        if (!f) throw std::runtime_error("arxiv yozilmadi (disk to'la?)");
        f.close();
        if (!f) throw std::runtime_error("arxiv yozilmadi (yopishda xato)");
    }
    // tmp -> asl nom (atomik almashtirish)
    {
        std::error_code ec;
        fs::rename(fs_path(tmp), fs_path(path), ec);
        if (ec) {
            // Windows da rename ustiga yozmasa: avval o'chirib qayta urinamiz
            fs::remove(fs_path(path), ec);
            fs::rename(fs_path(tmp), fs_path(path), ec);
            if (ec) throw std::runtime_error("arxiv almashtirilmadi: " + ec.message());
        }
    }
}

// v1 ni ham o'qiydi (nomsiz bitta yozuvga aylantiradi)
static std::vector<PackedFile> read_archive(const std::string& path) {
    // Fayl hajmini oldindan bilib olamiz — e'lon qilingan o'lchamlar shunga tekshiriladi (OOM himoyasi)
    uint64_t arch_sz = 0;
    { std::error_code ec0; uintmax_t s0 = fs::file_size(fs_path(path), ec0);
      if (!ec0) arch_sz = (uint64_t)s0; }
    std::ifstream f = open_in_bin(path);
    if (!f) throw std::runtime_error("gcf fayl ochilmadi: " + path);
    char magic[4]; f.read(magic, 4);
    if (!f) throw std::runtime_error("buzilgan arxiv (juda kichik)");
    std::vector<PackedFile> entries;
    if (std::memcmp(magic, MAGIC1, 4) == 0) {
        PackedFile e; e.name = "file";
        f.read((char*)&e.orig, 8);
        if (!f) throw std::runtime_error("buzilgan arxiv (v1 sarlavha)");
        if (e.orig > GCF_MAX_ORIG_SINGLE)
            throw std::runtime_error("buzilgan arxiv (orig juda katta)");
        read_table(f, e.table);
        f.read((char*)&e.total_bits, 8);
        if (!f) throw std::runtime_error("buzilgan arxiv (v1 sarlavha)");
        if (e.total_bits > GCF_MAX_STREAM * 8)
            throw std::runtime_error("buzilgan arxiv (v1 bits)");
        e.stream = std::vector<uint8_t>((std::istreambuf_iterator<char>(f)),
                                         std::istreambuf_iterator<char>());
        if (e.total_bits > (uint64_t)e.stream.size() * 8)
            throw std::runtime_error("buzilgan arxiv (v1 oqim kesilgan)");
        e.name = "file";
        e.method = 0;
        entries.push_back(std::move(e));
    } else if (std::memcmp(magic, MAGIC2, 4) == 0 || std::memcmp(magic, MAGIC3, 4) == 0) {
        bool v3 = std::memcmp(magic, MAGIC3, 4) == 0;
        uint32_t n = 0; f.read((char*)&n, 4);
        if (!f || n == 0 || n > GCF_MAX_ENTRIES) throw std::runtime_error("buzilgan arxiv");
        entries.reserve(n);
        // Avval hamma sarlavha, keyin hamma oqim (yozish tartibi bilan bir xil)
        for (uint32_t i = 0; i < n; i++) {
            PackedFile e;
            uint16_t nl = 0; f.read((char*)&nl, 2);
            if (!f) throw std::runtime_error("buzilgan arxiv (nom)");
            if (nl == 0) throw std::runtime_error("buzilgan arxiv (bo'sh nom)");
            e.name.resize(nl);
            if (nl) f.read(e.name.data(), nl);
            if (!f) throw std::runtime_error("buzilgan arxiv (nom kesilgan)");
            if (is_unsafe_entry_name(e.name))
                throw std::runtime_error("buzilgan arxiv (xavfli nom): " + e.name);
            uint64_t sb = 0;
            f.read((char*)&e.orig, 8);
            if (!f) throw std::runtime_error("buzilgan arxiv (sarlavha)");
            if (e.orig > GCF_MAX_ORIG_SINGLE)
                throw std::runtime_error("buzilgan arxiv (orig juda katta)");
            if (v3) f.read((char*)&e.method, 1);
            else e.method = 0;
            if (!f) throw std::runtime_error("buzilgan arxiv (sarlavha)");
            if (e.method > 2) throw std::runtime_error("buzilgan arxiv (method)");
            if (e.method == 2) {
                uint32_t nch = 0; f.read((char*)&nch, 4);
                if (!f || nch == 0 || nch > GCF_MAX_CHUNKS) throw std::runtime_error("buzilgan arxiv (lz)");
                e.lz.resize(nch);
                for (auto& ch : e.lz) {
                    uint32_t fb = 0, mb = 0; uint64_t lb = 0;
                    f.read((char*)&ch.nTokens, 4);
                    f.read((char*)&fb, 4);
                    f.read((char*)&ch.lit_bits, 8);
                    f.read((char*)&lb, 8);
                    f.read((char*)&mb, 4);
                    if (!f) throw std::runtime_error("buzilgan arxiv (lz meta kesilgan)");
                    // Qattiq limitlar + fayl qoldig'i bilan kross-tekshiruv (OOM himoyasi)
                    if (fb > (1u << 24) || mb > (1u << 28) || lb > GCF_MAX_STREAM)
                        throw std::runtime_error("buzilgan arxiv (lz meta)");
                    if (arch_sz && (uint64_t)fb + lb + mb > arch_sz + (1ULL << 20))
                        throw std::runtime_error("buzilgan arxiv (lz hajm fayldan katta)");
                    if ((uint64_t)fb * 8 < ch.nTokens)
                        throw std::runtime_error("buzilgan arxiv (lz flags)");
                    if (ch.lit_bits > lb * 8)
                        throw std::runtime_error("buzilgan arxiv (lz lit_bits)");
                    if (lb != 0 && lb != (ch.lit_bits + 7) / 8)
                        throw std::runtime_error("buzilgan arxiv (lz lit o'lcham)");
                    if (ch.nTokens > e.orig + 16)
                        throw std::runtime_error("buzilgan arxiv (lz nTokens)");
                    read_table(f, ch.lit_table);
                    try {
                        ch.flags.resize(fb); ch.lit_stream.resize((size_t)lb); ch.matches.resize(mb);
                    } catch (const std::bad_alloc&) {
                        throw std::runtime_error("buzilgan arxiv (xotira yetmadi)");
                    }
                }
                for (auto& ch : e.lz) {
                    if (!ch.flags.empty()) f.read((char*)ch.flags.data(), ch.flags.size());
                    if (!ch.lit_stream.empty()) f.read((char*)ch.lit_stream.data(), ch.lit_stream.size());
                    if (!ch.matches.empty()) f.read((char*)ch.matches.data(), ch.matches.size());
                    if (!f) throw std::runtime_error("buzilgan arxiv (lz oqim)");
                }
                entries.push_back(std::move(e));
                continue;
            }
            f.read((char*)&e.total_bits, 8);
            f.read((char*)&sb, 8);
            if (!f) throw std::runtime_error("buzilgan arxiv (sarlavha kesilgan)");
            if (sb > GCF_MAX_STREAM) throw std::runtime_error("buzilgan arxiv (hajm)");
            if (arch_sz && sb > arch_sz + (1ULL << 20))
                throw std::runtime_error("buzilgan arxiv (oqim fayldan katta)");
            if (e.method == 0) {
                if (e.total_bits > sb * 8)
                    throw std::runtime_error("buzilgan arxiv (bits)");
                read_table(f, e.table);
            } else { // method==1 store
                if (sb != e.orig)
                    throw std::runtime_error("buzilgan arxiv (store o'lcham)");
                if (e.total_bits != 0)
                    throw std::runtime_error("buzilgan arxiv (store bits)");
            }
            try { e.stream.resize((size_t)sb); }
            catch (const std::bad_alloc&) { throw std::runtime_error("buzilgan arxiv (xotira yetmadi)"); }
            entries.push_back(std::move(e));
        }
        for (auto& e : entries) {
            if (e.method == 2) continue; // lz oqimlari yuqorida o'qildi
            if (!e.stream.empty()) f.read((char*)e.stream.data(), e.stream.size());
            if (!f) throw std::runtime_error("buzilgan arxiv (oqim)");
        }
    } else {
        throw std::runtime_error("format xato (GCF emas)");
    }
    return entries;
}

static size_t file_size(const std::string& path) {
    std::error_code ec;
    uintmax_t sz = fs::file_size(fs_path(path), ec);
    if (!ec) {
        if (sz > (uintmax_t)SIZE_MAX) return 0; // 32-bit truncate himoyasi
        return (size_t)sz;
    }
    std::ifstream f = open_in_bin_ate(path);
    if (!f) return 0;
    auto pos = f.tellg();
    if (pos == (std::streampos)-1) return 0;
    // tellg long long bo'lishi mumkin — manfiy/huge himoyasi
    long long v = (long long)pos;
    if (v < 0) return 0;
    return (size_t)v;
}

// ---------------- ochiq API ----------------

CompressStats gcf_compress(const std::string& in_path, const std::string& out_path,
                           const GpuConfig& cfg) {
    auto t0 = std::chrono::steady_clock::now();
    CompressStats s;
    PackedFile e = pack_file(in_path, cfg, &s.backend);
    write_archive(out_path, {e});
    auto t1 = std::chrono::steady_clock::now();
    s.in_bytes = (size_t)e.orig;
    s.out_bytes = file_size(out_path);
    s.seconds = std::chrono::duration<double>(t1 - t0).count();
    return s;
}

CompressStats gcf_compress_multi(const std::vector<std::string>& in_paths,
                                 const std::string& out_path, const GpuConfig& cfg) {
    auto t0 = std::chrono::steady_clock::now();
    CompressStats s; s.in_bytes = 0;
    std::vector<PackedFile> entries;
    // Bir xil basename ikki marta bo'lsa extract da jim overwrite bo'ladi — oldini olamiz
    {
        std::vector<std::string> names;
        for (auto& p : in_paths) names.push_back(basename_of(p));
        std::sort(names.begin(), names.end());
        for (size_t k = 1; k < names.size(); k++)
            if (names[k] == names[k-1])
                throw std::runtime_error("bir xil nomli fayllar (basename takror): " + names[k] +
                    " — papka arxivlang yoki nomlarni o'zgartiring");
    }
    for (auto& p : in_paths) {
        std::string be;
        entries.push_back(pack_file(p, cfg, &be));
        s.in_bytes += (size_t)entries.back().orig;
        s.backend = be;
    }
    write_archive(out_path, entries);
    auto t1 = std::chrono::steady_clock::now();
    s.out_bytes = file_size(out_path);
    s.seconds = std::chrono::duration<double>(t1 - t0).count();
    return s;
}

CompressStats gcf_compress_folder(const std::string& in_dir, const std::string& out_path,
                                  const GpuConfig& cfg) {
    auto t0 = std::chrono::steady_clock::now();
    CompressStats s; s.in_bytes = 0;
    std::vector<PackedFile> entries;
    fs::path root = fs_path(in_dir);
    if (!fs::is_directory(root)) throw std::runtime_error("papka topilmadi: " + in_dir);
    // Symlink kuzatmaslik: tashqi maxfiy fayllar arxivga sizib ketmasligi uchun
    fs::directory_options dopt = fs::directory_options::skip_permission_denied;
    for (auto& it : fs::recursive_directory_iterator(root, dopt)) {
        // Symlink bo'lsa o'tkazib yuboramiz (tashqariga chiqish/sizish himoyasi)
        std::error_code sec;
        if (it.is_symlink(sec)) continue;
        if (!it.is_regular_file()) continue;
        // UTF-8 xavfsiz nisbiy nom (Windows da generic_string ANSI buzadi)
#ifdef _WIN32
        std::string rel;
        {
            fs::path rp = fs::relative(it.path(), root);
            std::wstring w = rp.generic_wstring();
            int len = WideCharToMultiByte(CP_UTF8, 0, w.c_str(), -1, nullptr, 0, nullptr, nullptr);
            if (len > 1) {
                std::string tmp((size_t)len, '\0');
                WideCharToMultiByte(CP_UTF8, 0, w.c_str(), -1, tmp.data(), len, nullptr, nullptr);
                tmp.resize((size_t)len - 1); // oxirgi '\0' ni olib tashlash
                rel = std::move(tmp);
            }
            for (char& c : rel) if (c == '\\') c = '/';
        }
#else
        std::string rel = fs::relative(it.path(), root).generic_string();
#endif
#ifdef _WIN32
        std::ifstream rf(it.path().wstring(), std::ios::binary);
#else
        std::ifstream rf(it.path(), std::ios::binary);
#endif
        if (!rf) throw std::runtime_error("fayl ochilmadi: " + it.path().string());
        std::vector<uint8_t> data((std::istreambuf_iterator<char>(rf)),
                                   std::istreambuf_iterator<char>());
        if (rel.empty() || is_unsafe_entry_name(rel))
            throw std::runtime_error("xavfli ichki nom (o'tkazib yuborildi): " + rel);
        PackedFile e;
        e.name = rel;
        std::string be;
        pack_choose_best(data, &cfg, &be, e);
        s.in_bytes += (size_t)e.orig;
        s.backend = be;
        entries.push_back(std::move(e));
    }
    if (entries.empty()) throw std::runtime_error("papka bo'sh: " + in_dir);
    write_archive(out_path, entries);
    auto t1 = std::chrono::steady_clock::now();
    s.out_bytes = file_size(out_path);
    s.seconds = std::chrono::duration<double>(t1 - t0).count();
    return s;
}

CompressStats gcf_decompress(const std::string& in_path, const std::string& out_path) {
    auto t0 = std::chrono::steady_clock::now();
    auto entries = read_archive(in_path);
    if (entries.size() != 1)
        throw std::runtime_error("Bu arxivda " + std::to_string(entries.size()) +
            " ta fayl bor — 'extract' bilan yoki arxiv oynasida oching");
    auto data = decode_entry(entries[0]);
    std::ofstream f = open_out_bin(out_path);
    if (!f) throw std::runtime_error("chiqish fayli ochilmadi: " + out_path);
    if (!data.empty()) f.write((char*)data.data(), data.size());
    f.flush(); f.close();
    if (!f) throw std::runtime_error("chiqish yozilmadi (disk to'la?): " + out_path);
    auto t1 = std::chrono::steady_clock::now();
    CompressStats s{file_size(in_path), data.size(),
                    std::chrono::duration<double>(t1 - t0).count(), "decode"};
    return s;
}

std::vector<GcfEntryInfo> gcf_list(const std::string& path) {
    auto entries = read_archive(path);
    std::vector<GcfEntryInfo> out;
    for (auto& e : entries) {
        uint64_t packed = packed_size_of(e);
        out.push_back({e.name, e.orig, packed,
                       e.method == 2 ? "lz" : (e.method == 1 ? "store" : "huff")});
    }
    return out;
}

std::vector<uint8_t> gcf_read_entry(const std::string& arch_path, int index) {
    auto entries = read_archive(arch_path);
    if (index < 0 || index >= (int)entries.size()) throw std::runtime_error("noto'g'ri indeks");
    return decode_entry(entries[index]);
}

std::string gcf_test(const std::string& arch_path) {
    auto entries = read_archive(arch_path);
    uint64_t total = 0;
    for (auto& e : entries) {
        auto data = decode_entry(e);
        total += data.size();
    }
    return "Tekshiruv OK: " + std::to_string(entries.size()) +
           " ta fayl, " + std::to_string(total) + " B";
}

CompressStats gcf_extract(const std::string& arch_path, const std::string& out_dir,
                          const std::vector<int>& indices) {
    auto t0 = std::chrono::steady_clock::now();
    auto entries = read_archive(arch_path);
    std::vector<int> sel = indices;
    if (sel.empty()) { sel.resize(entries.size()); for (size_t i = 0; i < sel.size(); i++) sel[i] = (int)i; }
    CompressStats s;
    fs::path base_dir = fs_path(out_dir);
    // base_dir ni bir marta canonical qilamiz (symlink + traversal yakuniy tekshiruvi uchun)
    {
        std::error_code bec;
        fs::create_directories(base_dir, bec);
    }
    std::error_code bcec;
    fs::path canon_base = fs::weakly_canonical(base_dir, bcec);
    for (int i : sel) {
        if (i < 0 || i >= (int)entries.size()) throw std::runtime_error("noto'g'ri indeks");
        auto& e = entries[i];
        auto data = decode_entry(e);
        if (is_unsafe_entry_name(e.name))
            throw std::runtime_error("xavfli yo'l arxivda: " + e.name);
        fs::path out = base_dir / fs_path(e.name);
        // Yakuniy himoya: normalizatsiyadan keyin ham base ichida bo'lishi shart
        {
            std::error_code cec;
            fs::path nrm = out.lexically_normal();
            // ".." qoldig'i bo'lsa rad et
            for (auto& part : nrm) if (part == "..") throw std::runtime_error("xavfli yo'l arxivda: " + e.name);
            if (!bcec) {
                fs::path canon_out = fs::weakly_canonical(nrm, cec);
                if (!cec) {
                    // canon_out canon_base bilan boshlanishi shart
                    auto mm = std::mismatch(canon_base.begin(), canon_base.end(), canon_out.begin());
                    if (mm.first != canon_base.end())
                        throw std::runtime_error("xavfli yo'l arxivda (tashqariga chiqish): " + e.name);
                }
            }
            // Ota papkada symlink bo'lsa kuzatib ketmaslik uchun: symlink bo'lsa rad et
            fs::path parent = nrm.parent_path();
            if (!parent.empty()) {
                std::error_code lec;
                if (fs::is_symlink(parent, lec) && !lec)
                    throw std::runtime_error("xavfli yo'l (symlink ota): " + e.name);
            }
        }
        if (e.name == "." || e.name.empty())
            throw std::runtime_error("xavfli yo'l arxivda: " + e.name);
        {
            std::error_code dec;
            fs::create_directories(out.parent_path(), dec);
            if (dec) throw std::runtime_error("papka yaratilmadi: " + out.parent_path().string());
        }
#ifdef _WIN32
        std::ofstream f(out.wstring(), std::ios::binary);
#else
        std::ofstream f(out, std::ios::binary);
#endif
        if (!f) throw std::runtime_error("yozilmadi: " + out.string());
        if (!data.empty()) f.write((char*)data.data(), data.size());
        f.flush(); f.close();
        if (!f) throw std::runtime_error("yozilmadi (disk to'la?): " + out.string());
        s.in_bytes += (size_t)packed_size_of(e);
        s.out_bytes += data.size();
    }
    auto t1 = std::chrono::steady_clock::now();
    s.seconds = std::chrono::duration<double>(t1 - t0).count();
    s.backend = "extract";
    return s;
}

void gcf_add(const std::string& arch_path, const std::vector<std::string>& files,
             const GpuConfig& cfg) {
    std::vector<PackedFile> entries;
    { std::ifstream probe = open_in_bin(arch_path);
      if (probe.good()) { probe.close(); entries = read_archive(arch_path); } }
    for (auto& p : files) {
        std::string be;
        entries.push_back(pack_file(p, cfg, &be));
    }
    write_archive(arch_path, entries);
}

void gcf_remove(const std::string& arch_path, const std::vector<int>& indices) {
    auto entries = read_archive(arch_path);
    std::vector<int> sel = indices;
    std::sort(sel.begin(), sel.end(), std::greater<int>());
    // Dublikat indeks ("remove arch 1 1") qo'shimcha yozuv o'chirmasligi uchun
    sel.erase(std::unique(sel.begin(), sel.end()), sel.end());
    for (int i : sel) {
        if (i < 0 || i >= (int)entries.size()) throw std::runtime_error("noto'g'ri indeks");
        entries.erase(entries.begin() + i);
    }
    write_archive(arch_path, entries);
}

void gcf_info(const std::string& path) {
    size_t sz = file_size(path);
    std::ifstream fin = open_in_bin(path);
    if (!fin) throw std::runtime_error("fayl ochilmadi");
    char magic[4]; fin.read(magic, 4);
    std::cout << "Fayl: " << path << " (" << sz << " B)\n";
    if (sz >= 4 && (std::memcmp(magic, MAGIC1, 4) == 0 || std::memcmp(magic, MAGIC2, 4) == 0 || std::memcmp(magic, MAGIC3, 4) == 0)) {
        bool v1 = std::memcmp(magic, MAGIC1, 4) == 0;
        bool v2 = std::memcmp(magic, MAGIC2, 4) == 0;
        auto entries = read_archive(path);
        uint64_t orig = 0;
        for (auto& e : entries) orig += e.orig;
        std::cout << "  Format: GCF" << (v1 ? "1 (bitta fayl)" : (v2 ? "2 (arxiv)" : "3 (arxiv, store/lz)")) << "\n";
        std::cout << "  Fayllar: " << entries.size() << "\n";
        for (size_t i = 0; i < entries.size(); i++) {
            uint64_t pk = packed_size_of(entries[i]);
            const char* mname = entries[i].method == 2 ? "lz" :
                                (entries[i].method == 1 ? "store" : "huff");
            std::cout << "  [" << i << "] " << entries[i].name << "  "
                      << entries[i].orig << " B -> " << pk << " B [" << mname << "]\n";
        }
        std::cout << "  Jami: " << orig << " -> " << sz << " B, ratio="
                  << (orig ? (double)sz / orig : 0) << "\n";
    } else {
        std::cout << "  Format: oddiy fayl (siqilmagan)\n";
    }
}
