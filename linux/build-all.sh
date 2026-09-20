#!/bin/bash
# Backend (gcf) + Qt GUI (gcf-qt) + .deb — hammasi birdan
# Ishlatish (Ubuntu ichida, repo ildizidan yoki shu papkadan):
#   ./linux/build-all.sh
set -e
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
cd "$ROOT"

echo "=== [1/3] Backend (gcf) ==="
sudo apt-get update
sudo apt-get install -y cmake g++ dpkg-dev qtbase5-dev 2>/dev/null || \
sudo apt-get install -y cmake g++ dpkg-dev qt6-base-dev 2>/dev/null || true

rm -rf build-linux
cmake -S . -B build-linux -DCMAKE_BUILD_TYPE=Release
cmake --build build-linux -j"$(nproc)"
./build-linux/gcf gpus || true

echo "=== [2/3] Qt GUI (gcf-qt) ==="
rm -rf linux/gcf-qt/build
cmake -S linux/gcf-qt -B linux/gcf-qt/build -DCMAKE_BUILD_TYPE=Release
cmake --build linux/gcf-qt/build -j"$(nproc)"

echo "=== [3/3] .deb paket ==="
# .deb ichiga ikkala binary kirishi uchun staging
rm -rf /tmp/gcf-deb-stage
mkdir -p /tmp/gcf-deb-stage/usr/bin /tmp/gcf-deb-stage/usr/share/applications \
         /tmp/gcf-deb-stage/usr/share/doc/gcf /tmp/gcf-deb-stage/usr/share/man/man1
cp build-linux/gcf /tmp/gcf-deb-stage/usr/bin/gcf
cp linux/gcf-qt/build/gcf-qt /tmp/gcf-deb-stage/usr/bin/gcf-qt
cp README.md /tmp/gcf-deb-stage/usr/share/doc/gcf/ 2>/dev/null || true
cp packaging/linux/gcf.1 /tmp/gcf-deb-stage/usr/share/man/man1/ 2>/dev/null || true
# GUI desktop shortcut (.deb ichida)
cat > /tmp/gcf-deb-stage/usr/share/applications/gcf-qt.desktop <<'EOF'
[Desktop Entry]
Name=GCF Compressor
Comment=GPU File Compressor - fayllarni siqish/ochish
Exec=gcf-qt
Icon=package-x-generic
Terminal=false
Type=Application
Categories=Utility;Archiving;
MimeType=application/x-gcf;
EOF

(cd build-linux && cpack -G "DEB;TGZ")
cp build-linux/*.deb . 2>/dev/null || true
cp build-linux/*.tar.gz . 2>/dev/null || true
# GUI ni ham portable tar.gz yoniga qo'shib qo'yamiz
cp linux/gcf-qt/build/gcf-qt . 2>/dev/null || true

echo "--- CLI roundtrip testi ---"
echo "hello gcf linux test" > /tmp/gcf_test.txt
./build-linux/gcf compress /tmp/gcf_test.txt /tmp/gcf_test.txt.gcf
./build-linux/gcf decompress /tmp/gcf_test.txt.gcf /tmp/gcf_restored.txt
cmp /tmp/gcf_test.txt /tmp/gcf_restored.txt && echo "[OK] roundtrip MATCH"

ls -la *.deb *.tar.gz gcf-qt 2>/dev/null || true
echo "[OK] Tayyor. O'rnatish: sudo dpkg -i gcf-*-ubuntu-amd64.deb && gcf-qt"
