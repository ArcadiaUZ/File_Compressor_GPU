#include "gpu_compressor.h"
#include "gpu_info.h"
#include <iostream>
#include <string>
#include <vector>
#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <shellapi.h>
// Windows konsol UTF-8: kirill/emoji yo'llar va xabarlar buzilmasligi uchun.
static void win32_init_utf8() {
    SetConsoleOutputCP(CP_UTF8);
    SetConsoleCP(CP_UTF8);
}
static std::string utf8_from_wide_win(const std::wstring& w) {
    if (w.empty()) return {};
    int n = WideCharToMultiByte(CP_UTF8, 0, w.c_str(), -1, nullptr, 0, nullptr, nullptr);
    if (n <= 0) return {};
    std::string s((size_t)n - 1, '\0');
    WideCharToMultiByte(CP_UTF8, 0, w.c_str(), -1, s.data(), n, nullptr, nullptr);
    return s;
}
// Linker main() ni tanlasa ham Unicode buzilmasligi uchun wide CLI dan o'qiymiz.
// (wmain() bo'lmasa ham ishlaydi — MinGW -municode siz ham.)
static std::vector<std::string> win32_wide_args() {
    int nArgs = 0;
    LPWSTR* sz = CommandLineToArgvW(GetCommandLineW(), &nArgs);
    std::vector<std::string> out;
    if (!sz) return out;
    out.reserve((size_t)nArgs);
    for (int i = 0; i < nArgs; i++) out.push_back(utf8_from_wide_win(sz[i]));
    LocalFree(sz);
    return out;
}
#endif

static void usage() {
    std::cout <<
    "GCF - GPU File Compressor backend v2.0 (arxiv)\n"
    "  gcf info <fayl>                          - fayl haqida\n"
    "  gcf gpus                                 - GPU larni ro'yxatlash\n"
    "  gcf compress <in> <out.gcf>              - bitta faylni arxivlash\n"
    "  gcf compress-multi <out.gcf> <f1> [f2..] - kup faylli arxiv\n"
    "  gcf compress-folder <papka> <out.gcf>    - papkani rekursiv arxivlash\n"
    "  gcf decompress <in.gcf> <out>            - bitta yozuvli arxivni ochish\n"
    "  gcf list <arch.gcf>                      - arxiv ichidagilar (idx|nom|orig|packed)\n"
    "  gcf extract <arch.gcf> <papka> [idx..]   - chiqarish (idx siz hammasi)\n"
    "  gcf add <arch.gcf> <f1> [f2..]           - arxivga qo'shish\n"
    "  gcf remove <arch.gcf> <idx..>            - arxivdan o'chirish\n"
    "  gcf test <arch.gcf>                      - butunlikni tekshirish\n";
}

int gcf_main_impl(const std::vector<std::string>& args) {
    int argc = (int)args.size();
    // args[0] = exe nomi, args[1] = cmd (eski main dagi argv bilan bir xil)
    auto get = [&](int i) -> const std::string& { return args[(size_t)i]; };
    // Qat'iy indeks parse: "1abc" ni stoi 1 deb qabul qiladi — bizda xato bo'lishi kerak.
    auto parse_idx = [](const std::string& s) -> int {
        if (s.empty() || s.size() > 6) throw std::runtime_error("noto'g'ri indeks: " + s);
        size_t start = 0;
        if (s[0] == '-' || s[0] == '+') {
            if (s.size() == 1) throw std::runtime_error("noto'g'ri indeks: " + s);
            start = 1;
        }
        for (size_t k = start; k < s.size(); k++)
            if (s[k] < '0' || s[k] > '9') throw std::runtime_error("noto'g'ri indeks: " + s);
        try { return std::stoi(s); }
        catch (...) { throw std::runtime_error("noto'g'ri indeks: " + s); }
    };
    try {
        if (argc < 2) { usage(); return 1; }
        std::string cmd = get(1);
        if (cmd == "gpus") {
            for (auto& g : list_gpus()) std::cout << "[" << g.id << "] " << g.name << "\n";
            detect_best_gpu().print();
            return 0;
        }
        if (cmd == "info" && argc == 3) { gcf_info(get(2)); return 0; }
        if (cmd == "compress" && argc == 4) {
            GpuConfig cfg = detect_best_gpu();
            cfg.print();
            auto s = gcf_compress(get(2), get(3), cfg);
            std::cout << "Siqildi: " << s.in_bytes << " -> " << s.out_bytes
                      << " B (ratio " << s.ratio() << ") "
                      << s.seconds << "s [" << s.backend << "]\n";
            return 0;
        }
        if (cmd == "compress-multi" && argc >= 4) {
            GpuConfig cfg = detect_best_gpu();
            cfg.print();
            std::vector<std::string> files;
            for (int i = 3; i < argc; i++) files.push_back(get(i));
            auto s = gcf_compress_multi(files, get(2), cfg);
            std::cout << "Arxivlandi (" << files.size() << " fayl): " << s.in_bytes
                      << " -> " << s.out_bytes << " B (ratio " << s.ratio() << ") "
                      << s.seconds << "s [" << s.backend << "]\n";
            return 0;
        }
        if (cmd == "compress-folder" && argc == 4) {
            GpuConfig cfg = detect_best_gpu();
            cfg.print();
            auto s = gcf_compress_folder(get(2), get(3), cfg);
            std::cout << "Papka arxivlandi: " << s.in_bytes
                      << " -> " << s.out_bytes << " B (ratio " << s.ratio() << ") "
                      << s.seconds << "s [" << s.backend << "]\n";
            return 0;
        }
        if (cmd == "decompress" && argc == 4) {
            auto s = gcf_decompress(get(2), get(3));
            std::cout << "Ochildi: " << s.in_bytes << " -> " << s.out_bytes
                      << " B " << s.seconds << "s\n";
            return 0;
        }
        if (cmd == "list" && argc == 3) {
            auto v = gcf_list(get(2));
            for (size_t i = 0; i < v.size(); i++)
                std::cout << i << "|" << v[i].name << "|"
                          << v[i].orig_size << "|" << v[i].packed_bytes
                          << "|" << v[i].method << "\n";
            return 0;
        }
        if (cmd == "extract" && argc >= 4) {
            std::vector<int> idx;
            for (int i = 4; i < argc; i++) idx.push_back(parse_idx(get(i)));
            auto s = gcf_extract(get(2), get(3), idx);
            std::cout << "Chiqarildi: " << s.out_bytes << " B " << s.seconds << "s\n";
            return 0;
        }
        if (cmd == "add" && argc >= 4) {
            GpuConfig cfg = detect_best_gpu();
            std::vector<std::string> files;
            for (int i = 3; i < argc; i++) files.push_back(get(i));
            gcf_add(get(2), files, cfg);
            std::cout << "Qo'shildi: " << files.size() << " fayl\n";
            return 0;
        }
        if (cmd == "remove" && argc >= 4) {
            std::vector<int> idx;
            for (int i = 3; i < argc; i++) idx.push_back(parse_idx(get(i)));
            gcf_remove(get(2), idx);
            std::cout << "O'chirildi: " << idx.size() << " yozuv\n";
            return 0;
        }
        if (cmd == "test" && argc == 3) {
            std::cout << gcf_test(get(2)) << "\n";
            return 0;
        }
        usage(); return 1;
    } catch (std::exception& e) {
        std::cerr << "XATO: " << e.what() << "\n";
        return 2;
    }
}

#ifdef _WIN32
// Windows da Unicode yo'llar uchun wide entry-point shart:
// narrow main() kirill/emoji ni "????" qiladi. wmain() dan UTF-8 ga o'tkazamiz,
// fayl ochish esa gpu_compressor.cpp dagi fs_path() orqali wide API da bo'ladi.
int wmain(int argc, wchar_t** wargv) {
    win32_init_utf8();
    std::vector<std::string> args;
    args.reserve((size_t)argc);
    for (int i = 0; i < argc; i++) args.push_back(utf8_from_wide_win(wargv[i]));
    return gcf_main_impl(args);
}
#endif

int main(int argc, char** argv) {
#ifdef _WIN32
    win32_init_utf8();
    // Narrow argv ANSI bo'lishi mumkin — wide dan qayta o'qiymiz (asosiy fix).
    // Muvaffaqiyatsiz bo'lsa eski argv ga tushamiz.
    {
        auto wargs = win32_wide_args();
        if (wargs.size() >= 1) return gcf_main_impl(wargs);
    }
#endif
    std::vector<std::string> args;
    args.reserve((size_t)argc);
    for (int i = 0; i < argc; i++) args.push_back(argv[i] ? argv[i] : "");
    return gcf_main_impl(args);
}
