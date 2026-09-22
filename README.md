# GPU File Compressor — Backend v1.0 (C++ + CUDA)

Har qanday faylni GPU da siqadigan backend. Hozir **RTX 3060 tekshirildi va ishladi**,
lekin CUDA Toolkit o'rnatish kerak build qilishdan oldin bolmasam **CPU-fallback** rejimda ishlaydi.
CUDA Toolkit o'rnatilgach qayta yig'ish kifoya — kod avtomatik GPU ga o'tadi.

## Imkoniyatlar
- `gcf gpus` — barcha NVIDIA GPU ni ro'yxatlash + eng kuchlisini avtomatik tanlash
- `gcf compress in out.gcf` — siqish (Huffman, lossless, har qanday fayl)
- `gcf decompress in.gcf out` — ochish (bitma-bit tiklanadi, testda MATCH)
- `gcf info fayl` — format/hajm/ratio
- Barcha yangi NVIDIA GPU lar uchun: `75;80;86;89;90;100;120` (Turing→Blackwell), RTX 3060 = sm_86
- Sinov o'tgan (RTX 3060, CUDA 13.4): `CUDA: HA, CC 8.6, 28 SM, 12287 MB`, backend `CUDA-NVIDIA GeForce RTX 3060`, `build-gpu\Release\gcf.exe` (0.38 MB) — Toolkit siz muhitda ham GPU ni topdi ✅
- GPU kuchiga qarab `block_size / streams / chunk_size` avtomatik tanlanadi

## Yig'ish (Windows)

```powershell
# Bir buyruq bilan hammasi (backend + GUI + test):
powershell -ExecutionPolicy Bypass -File build-windows.ps1

# Qo'lda:
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --config Release

# GPU rejim: https://developer.nvidia.com/cuda-downloads dan CUDA Toolkit 12+
# o'rnatib, Visual Studio 2022 bilan xuddi shu ikki buyruqni qayta ishga tushiring.
# CMake "GPU rejim YOQILDI" deb yozishi kerak.
# Eski Toolkit (<12.8) da sm_100/120 avtomatik o'chiriladi (nvcc xato bermasligi uchun).
```

GUI:
```powershell
dotnet publish gcf_gui\gcf_gui.csproj -c Release -o publish
# -> publish\GCF.exe (single-file, .NET ichida, native\gcf.exe gömülgan)
# Target: net8.0-windows LTS (net10 SDK ham yig'adi)
```

## Sinov (o'tgan)
```
./build/gcf.exe gpus
./build/gcf.exe compress test_input.bin test.gcf
./build/gcf.exe decompress test.gcf restored.bin
# 110000 B -> 54568 B (ratio 0.49), decompress MATCH
```

## Fayl formati (.gcf v3)
`GCF3` arxiv: `[magic][n][har fayl: name_len u16, name(UTF-8), orig u64, method u8, ...][oqimlar]`.
Usullar: `0=Huffman, 1=store(siqilmaydigan), 2=LZ77+Huffman`.
Eski `GCF1/GCF2` o'qishda tushuniladi.

## Portativlik — foydalanuvchi hech narsa o'rnatmaydi ✅
- `gcf.exe` **bitta fayl**, ichida hamma narsa bor:
  - CUDA runtime **static** link (`cudart_static`, `/MT`, `-static`) → `cudart64_XX.dll` ham, `Visual C++ Redist` ham kerak emas
  - GPU yadrolar exe ichiga embed (sm_60/75/86/89/90 + `compute_90` PTX) → alohida `.dll/.ptx` kerak emas
  - `nvidia-smi`-ga ham bog'liq emas (drayver bo'lmasa CPU-fallback ishlaydi)
- Foydalanuvchida bo'lishi kerak bo'lgan **yagona narsa — NVIDIA drayver** (o'yin o'ynaydiganlarda allaqachon bor). Drayver/GPU bo'lmasa ham dastur **o'z-o'zidan CPU da** ishlayveradi, xato bermaydi.
- Muhim farq: **CUDA Toolkit faqat BIZGA (yig'ish uchun) kerak**, foydalanuvchiga kerak emas. Biz bir marta Toolkit bilan yig'amiz → `gcf.exe` chiqadi → uni tarqatamiz, foydalanuvchi shunchaki ikki marta bosadi.

```powershell
# Tarqatish uchun zip yasash (Toolkit bilan yig'ilgandan keyin):
cmake --build build --config Release
cpack --config build/CPackConfig.cmake -G ZIP
# -> GCF-Portable-1.0-win64.zip ichida bitta gcf.exe, shuni bersangiz bo'ldi
```

## Keyingi qadam (GUI dan oldin)
1. CUDA Toolkit o'rnatib GPU rejimni yoqish
2. `hist_kernel` tayyor — keyingi: encode-kernel (offset + bit-packing GPU da)
3. Katta fayllar uchun chunk/stream parallelizm
4. Keyin GUI (Qt / C# WPF) — backend CLI sifatida chaqiriladi

## Windows moslik eslatmalari (2026-09-19 debug)
- Unicode yo'llar (`тест 😀`, bo'shliq): `wmain` + `GetCommandLineW` + `fs_path()` (wide API)
  + `packaging/windows/gcf.manifest` (UTF-8 activeCodePage, longPathAware) + `/utf-8`.
  Oldin kirill/emoji `"????"` bo'lib `fayl ochilmadi` berardi.
- Bo'sh fayl: `method=1(store)`, backend bo'sh `[]` emas.
- `my..file.txt` kabi nomlar endi bloklanmaydi — faqat `..` komponenti xavfli.
- `extract` statistikasi LZ uchun ham to'g'ri (`packed_size_of`).
- LZ candidate default CPU (deterministik): GPU racy kernel 12 MB da 597 KB/0.55s berardi,
  CPU 158 KB/0.08s. Ixtiyoriy `GCF_GPU_LZ=1` bilan eski GPU yo'l.
- CUDA xatolar `GCF_CUDA_CHECK` bilan exception ga aylanadi (jim buzilish yo'q).
- GUI: `StandardOutputEncoding=UTF8`, `Quote()` bilan xavfsiz argument, Temp exe har safar
  yangilanadi (stale nusxa muammosi yo'q), target `net8.0-windows` LTS.
