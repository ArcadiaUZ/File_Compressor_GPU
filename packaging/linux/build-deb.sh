#!/bin/bash
# Ubuntu da .deb + .tar.gz yasash (bitta buyruq)
# Ishlatish:  ./packaging/linux/build-deb.sh   (Ubuntu ichida)
# Natija: gcf-1.0.0-ubuntu-amd64.deb  va  .tar.gz
set -e
cd "$(dirname "$0")/../.."

echo "=== GCF Linux build (.deb) ==="

# Kerakli paketlar
if command -v apt-get >/dev/null 2>&1; then
  sudo apt-get update
  sudo apt-get install -y cmake g++ dpkg-dev 2>/dev/null || true
  # CUDA Toolkit ixtiyoriy (bo'lsa GPU rejim, bo'lmasa CPU-fallback):
  # sudo apt-get install -y nvidia-cuda-toolkit  # xohlasangiz
fi

rm -rf build-linux
cmake -S . -B build-linux -DCMAKE_BUILD_TYPE=Release
cmake --build build-linux --config Release -j"$(nproc)"

echo "--- test ---"
./build-linux/gcf gpus || true
echo "hello gcf linux test" > /tmp/gcf_test.txt
./build-linux/gcf compress /tmp/gcf_test.txt /tmp/gcf_test.txt.gcf
./build-linux/gcf decompress /tmp/gcf_test.txt.gcf /tmp/gcf_restored.txt
cmp /tmp/gcf_test.txt /tmp/gcf_restored.txt && echo "[OK] roundtrip MATCH"

echo "--- paketlash ---"
(cd build-linux && cpack -G "DEB;TGZ")
cp build-linux/*.deb . 2>/dev/null || true
cp build-linux/*.tar.gz . 2>/dev/null || true
ls -la *.deb *.tar.gz 2>/dev/null || true
echo "[OK] Tayyor. O'rnatish: sudo ./packaging/linux/install.sh"
