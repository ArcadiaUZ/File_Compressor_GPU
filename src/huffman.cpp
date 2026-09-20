#include "huffman.h"
#include <queue>
#include <stdexcept>

std::array<uint64_t,256> histogram_cpu(const uint8_t* data, size_t n) {
    std::array<uint64_t,256> f{}; f.fill(0);
    for (size_t i = 0; i < n; i++) f[data[i]]++;
    return f;
}

struct Node { uint64_t f; int sym; int l=-1,r=-1; };

std::array<HuffCode,256> build_huffman_table(const std::array<uint64_t,256>& freq) {
    std::array<HuffCode,256> t{};
    // Faqat bitta unikal bayt bo'lsa: 1 bitli kod
    int alive = 0, only = 0;
    for (int i = 0; i < 256; i++) if (freq[i]) { alive++; only = i; }
    if (alive == 0) return t;
    if (alive == 1) { t[only] = {0, 1}; return t; }

    std::vector<Node> pool;
    for (int i = 0; i < 256; i++) if (freq[i]) pool.push_back({freq[i], i});
    auto cmp = [&](int a, int b){ return pool[a].f > pool[b].f; };
    std::priority_queue<int, std::vector<int>, decltype(cmp)> q(cmp);
    for (int i = 0; i < (int)pool.size(); i++) q.push(i);
    while (q.size() > 1) {
        int a = q.top(); q.pop(); int b = q.top(); q.pop();
        int id = (int)pool.size();
        pool.push_back({pool[a].f + pool[b].f, -1, a, b});
        q.push(id);
    }
    int root = q.top();
    // DFS: kodlarni chiqarish (max 32 bit — HuffCode.bits uint32_t bo'lgani uchun).
    // 32 bitdan chuqur daraxt amalda 256 simvolda ham deyarli uchramaydi,
    // lekin uchrasa jim korrupsiya o'rniga aniq xato beramiz (caller raw ga tushadi).
    // iterative: (node, code, len)
    struct Fr { int id; uint32_t code; uint8_t len; };
    std::vector<Fr> st{{root, 0, 0}};
    while (!st.empty()) {
        Fr fr = st.back(); st.pop_back();
        const Node& nd = pool[fr.id];
        if (nd.sym >= 0) { t[nd.sym] = {fr.code, (uint8_t)(fr.len ? fr.len : 1)}; continue; }
        if (fr.len >= 32) throw std::runtime_error("Huffman daraxti juda chuqur (>32 bit)");
        st.push_back({nd.r, (uint32_t)(fr.code | (1u << fr.len)), (uint8_t)(fr.len + 1)});
        st.push_back({nd.l, fr.code, (uint8_t)(fr.len + 1)});
    }
    // Validatsiya: chastotasi bor har bir simvol kod olgan bo'lishi shart
    for (int i = 0; i < 256; i++) {
        if (freq[i] && t[i].len == 0)
            throw std::runtime_error("Huffman jadval xato (len==0)");
        if (t[i].len > 32)
            throw std::runtime_error("Huffman kod juda uzun (>32 bit)");
    }
    return t;
}
