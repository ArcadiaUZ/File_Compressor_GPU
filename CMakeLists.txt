cmake_minimum_required(VERSION 3.18)
project(GPU_File_Compressor VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# --- CUDA ni avtomatik aniqlash ---
# CUDA Toolkit o'rnatilgan bo'lsa GPU yadrolar yoqiladi,
# bo'lmasa CPU-fallback rejimda yig'iladi (baribir ishlaydi).
include(CheckLanguage)
check_language(CUDA)
if(CMAKE_CUDA_COMPILER)
  enable_language(CUDA)
  set(CMAKE_CUDA_STANDARD 17)
  # MinGW host ni nvcc Windows da qo'llamaydi — darhol CPU ga tushamiz
  if(MINGW)
    message(WARNING "MinGW + CUDA qo'llanmaydi -> CPU rejim (MSVC yoki WSL/Linux da GPU yiging)")
    set(USE_CUDA OFF)
  else()
  find_package(CUDAToolkit QUIET)
  if(CUDAToolkit_FOUND)
    message(STATUS "CUDA Toolkit topildi: ${CUDAToolkit_VERSION} -> GPU rejim YOQILDI")
    set(USE_CUDA ON)
    # CUDA 13.x Kepler/Maxwell/Pascal ni tashladi -> minimum sm_75.
    # RTX 3060 = sm_86. Hopper/Blackwell uchun 90/100/120 + forward-compat PTX.
    # Eski Toolkit (12.0-12.5) sm_90/100/120 ni bilmaydi -> nvcc xato beradi,
    # shuning uchun versiyaga qarab arxitektura ro'yxatini qisqartiramiz.
    # Foydalanuvchi -DCMAKE_CUDA_ARCHITECTURES bersa uni ezmaymiz.
    if(NOT DEFINED CMAKE_CUDA_ARCHITECTURES)
      if(CUDAToolkit_VERSION VERSION_LESS 12.8)
        set(CMAKE_CUDA_ARCHITECTURES "75;80;86;89;90")
      elseif(CUDAToolkit_VERSION VERSION_LESS 13.0)
        set(CMAKE_CUDA_ARCHITECTURES "75;80;86;89;90;100;120")
      else()
        set(CMAKE_CUDA_ARCHITECTURES "75;80;86;89;90;100;120")
      endif()
    endif()
    message(STATUS "CUDA arch: ${CMAKE_CUDA_ARCHITECTURES}")
  else()
    message(WARNING "nvcc bor, lekin CUDAToolkit topilmadi -> CPU rejim")
    set(USE_CUDA OFF)
  endif()
  endif()
else()
  message(STATUS "nvcc topilmadi -> CPU-fallback rejim (kod baribir test bo'ladi). CUDA Toolkit o'rnatgach avtomatik GPU yoqiladi.")
  set(USE_CUDA OFF)
endif()

find_package(Threads REQUIRED)

set(SOURCES
  src/main.cpp
  src/gpu_info.cpp
  src/huffman.cpp
  src/gpu_compressor.cpp
)

if(USE_CUDA)
  list(APPEND SOURCES src/kernels.cu)
endif()

add_executable(gcf ${SOURCES})
target_include_directories(gcf PRIVATE include)
# Windows: manba UTF-8 (kirill/emoji izohlar buzilmasligi uchun) + konsol UTF-8 manifesti.
# /utf-8 bo'lmasa MSVC fayllarni ANSI deb o'qiydi (C4819 ogohlantirish/xato).
if(MSVC)
  target_compile_options(gcf PRIVATE $<$<COMPILE_LANGUAGE:CXX>:/utf-8>)
  target_compile_options(gcf PRIVATE $<$<COMPILE_LANGUAGE:CUDA>:-Xcompiler=/utf-8>)
endif()
if(WIN32)
  # UTF-8 activeCodePage manifesti — narrow argv/fopen ni UTF-8 qiladi.
  # wmain() baribir asosiy himoya, manifest ikkinchi qatlam.
  set(GCF_MANIFEST "${CMAKE_CURRENT_SOURCE_DIR}/packaging/windows/gcf.manifest")
  if(EXISTS "${GCF_MANIFEST}")
    target_sources(gcf PRIVATE "${GCF_MANIFEST}")
  endif()
endif()
if(USE_CUDA)
  target_compile_definitions(gcf PRIVATE USE_CUDA=1)
  target_include_directories(gcf PRIVATE ${CUDAToolkit_INCLUDE_DIRS})
  # --- PORTATIVLIK: foydalanuvchidan hech narsa talab qilinmasin ---
  # cudart ni STATIC link qilamiz -> Linuxda libcudart.so / Windowsda
  # cudart64_XX.dll ni yoniga qo'yish shart emas.
  # Yadrolar exe ichiga embed bo'ladi (SASS+PTX), alohida .ptx kerak emas.
  # Foydalanuvchida faqat NVIDIA drayver bo'lishi kifoya.
  # Drayver/GPU bo'lmasa dastur avtomatik CPU-fallback ga o'tadi.
  set(CUDA_RUNTIME_LIBRARY Static)
  target_link_libraries(gcf PRIVATE CUDA::cudart_static)
  set_target_properties(gcf PROPERTIES
    CUDA_SEPARABLE_COMPILATION ON
    CUDA_RUNTIME_LIBRARY Static)
  if(MSVC)
    set_target_properties(gcf PROPERTIES MSVC_RUNTIME_LIBRARY "MultiThreaded") # /MT -> VC Redist kerak emas
    # Faqat C++ fayllarga (/MT ni .cu ga bersa nvcc buziladi)
    target_compile_options(gcf PRIVATE $<$<COMPILE_LANGUAGE:CXX>:/MT$<$<CONFIG:Debug>:d>>)
    # thrust/scan uchun MSVC standart preprocessor shart (faqat Windows+MSVC da)
    target_compile_options(gcf PRIVATE $<$<COMPILE_LANGUAGE:CUDA>:-Xcompiler=/Zc:preprocessor>)
  else()
    # Linux/GCC: CUDA host compiler flaglari (MSVC flaglari YOX!)
    target_compile_options(gcf PRIVATE $<$<COMPILE_LANGUAGE:CUDA>:-Xcompiler=-fPIC>)
  endif()
  # Arxitekturalar CMAKE_CUDA_ARCHITECTURES orqali beriladi (yuqorida) —
  # bu yerga qo'lda -gencode yozilmaydi (nvcc dublikatda xato beradi).
else()
  if(MSVC)
    set_target_properties(gcf PROPERTIES MSVC_RUNTIME_LIBRARY "MultiThreaded")
  endif()
endif()

# MinGW bo'lsa ham bitta exe bo'lishi uchun libstdc++ ni ichiga gomamiz
# (yolg'iz -static olib tashlandi: winpthread/Threads bilan muammo va keraksiz kattalik berardi)
if(MINGW)
  target_link_options(gcf PRIVATE -static-libgcc -static-libstdc++ -municode)
endif()

# Linux/UNIX: pthread + filesystem. Ubuntu 20.04+ da std::filesystem uchun extra link shart emas,
# lekin eski GCC (<9) uchun stdc++fs kerak bo'lishi mumkin.
target_link_libraries(gcf PRIVATE Threads::Threads ${CMAKE_DL_LIBS})
if(UNIX AND CMAKE_CXX_COMPILER_ID STREQUAL "GNU" AND CMAKE_CXX_COMPILER_VERSION VERSION_LESS 9.0)
  target_link_libraries(gcf PRIVATE stdc++fs)
endif()

# Optimizatsiya: portable .deb uchun -march=native ISHLATILMAYDI
# (native build qilingan mashinadan boshqa CPU da "Illegal instruction" beradi).
# Faqat lokal tez test uchun GCF_NATIVE_OPT=ON berilsa yoqiladi.
option(GCF_NATIVE_OPT "Use -march=native (NOT portable, local machine only)" OFF)
if((UNIX OR MINGW) AND NOT MSVC)
  target_compile_options(gcf PRIVATE -O3)
  if(GCF_NATIVE_OPT)
    target_compile_options(gcf PRIVATE -march=native)
  endif()
endif()

# --- O'rnatish: Windows (portable zip) va Linux (DEB + TGZ) ---
# Linux: bin -> /usr/bin/gcf, doc -> /usr/share/doc/gcf
if(UNIX)
  include(GNUInstallDirs)
  install(TARGETS gcf DESTINATION ${CMAKE_INSTALL_BINDIR})
  install(FILES README.md DESTINATION ${CMAKE_INSTALL_DOCDIR})
  install(FILES packaging/linux/gcf.1 DESTINATION ${CMAKE_INSTALL_MANDIR}/man1 OPTIONAL)
  install(FILES packaging/linux/gcf.desktop DESTINATION ${CMAKE_INSTALL_DATAROOTDIR}/applications OPTIONAL)
else()
  install(TARGETS gcf DESTINATION .)
  install(FILES README.md DESTINATION .)
endif()

set(CPACK_PACKAGE_NAME "gcf")
set(CPACK_PACKAGE_VENDOR "GCF")
set(CPACK_PACKAGE_VERSION "${PROJECT_VERSION}")
set(CPACK_PACKAGE_DESCRIPTION_SUMMARY "GPU File Compressor - Huffman arxivlovchi (CUDA/CPU)")
set(CPACK_PACKAGE_CONTACT "gcf@example.com")
set(CPACK_RESOURCE_FILE_README "${CMAKE_CURRENT_SOURCE_DIR}/README.md")

if(UNIX)
  # .deb + portable tar.gz ikkalasi birdan chiqadi:
  #   cpack -G DEB   -> gcf_1.0.0_amd64.deb (Debian konvensiya: _ bilan)
  #   cpack -G TGZ   -> gcf-1.0.0-Linux.tar.gz
  set(CPACK_GENERATOR "DEB;TGZ")
  set(CPACK_DEBIAN_PACKAGE_NAME "gcf")
  set(CPACK_DEBIAN_PACKAGE_MAINTAINER "GCF <gcf@example.com>")
  set(CPACK_DEBIAN_PACKAGE_DESCRIPTION "GPU File Compressor - istalgan faylni GPU/CPU da siqadigan arxivlovchi (Huffman, lossless). CUDA bo'lmasa CPU-fallback ishlaydi.")
  set(CPACK_DEBIAN_PACKAGE_SECTION "utils")
  set(CPACK_DEBIAN_PACKAGE_PRIORITY "optional")
  # Faqat NVIDIA drayver ixtiyoriy tavsiya — majburiy DEPENDS yo'q (Intel/AMD da ham CPU rejim ishlaydi)
  set(CPACK_DEBIAN_PACKAGE_SUGGESTS "nvidia-driver-535 | nvidia-driver-550")
  # Debian konvensiya: name_version_arch.deb ; arch avtomatik (amd64/arm64)
  if(CMAKE_SYSTEM_PROCESSOR MATCHES "aarch64|ARM64")
    set(GCF_DEB_ARCH "arm64")
  else()
    set(GCF_DEB_ARCH "amd64")
  endif()
  set(CPACK_DEBIAN_FILE_NAME "gcf_${PROJECT_VERSION}_${GCF_DEB_ARCH}.deb")
  # TGZ portable nom
  set(CPACK_ARCHIVE_COMPONENT_INSTALL OFF)
else()
  set(CPACK_GENERATOR "ZIP")
  set(CPACK_PACKAGE_NAME "GCF-Portable")
  set(CPACK_PACKAGE_VERSION "${PROJECT_VERSION}")
endif()
include(CPack)

message(STATUS "=== GPU_File_Compressor v${PROJECT_VERSION} | USE_CUDA=${USE_CUDA} | ${CMAKE_SYSTEM_NAME} ===")
