#!/bin/bash
# Qt GUI ni alohida yig'ish (Ubuntu ichida)
# Ishlatish: ./build-gui.sh
set -e
cd "$(dirname "$0")"
sudo apt-get update
sudo apt-get install -y cmake g++ qtbase5-dev 2>/dev/null || \
sudo apt-get install -y cmake g++ qt6-base-dev 2>/dev/null || true
rm -rf build
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j"$(nproc)"
echo "[OK] build/gcf-qt tayyor"
./build/gcf-qt --help 2>&1 || true
