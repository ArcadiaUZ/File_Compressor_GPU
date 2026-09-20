#!/bin/bash
# GCF - Ubuntu/Debian installer
# Ishlatish:  sudo ./install.sh   (yoki: sudo dpkg -i gcf-1.0.0-ubuntu-amd64.deb)
set -e
cd "$(dirname "$0")"

echo "=== GCF installer (Ubuntu/Debian) ==="

if [ "$EUID" -ne 0 ]; then
  echo "Iltimos sudo bilan ishga tushiring: sudo ./install.sh"
  exit 1
fi

# 1. Bog'liqliklar (build uchun kerak bo'lsa)
apt-get update
apt-get install -y cmake g++ 2>/dev/null || true

# 2. .deb bo'lsa shuni o'rnatamiz (eng toza yo'l)
DEB=$(ls gcf-*-ubuntu-amd64.deb 2>/dev/null | head -1 || true)
if [ -n "$DEB" ]; then
  echo "[*] $DEB o'rnatilmoqda..."
  dpkg -i "$DEB" || (apt-get install -f -y && dpkg -i "$DEB")
else
  # 3. .deb yo'q bo'lsa - portable tar.gz dan qo'lda o'rnatish
  TGZ=$(ls gcf-*.tar.gz 2>/dev/null | head -1 || true)
  if [ -n "$TGZ" ]; then
    echo "[*] $TGZ dan o'rnatilmoqda..."
    tar xzf "$TGZ" -C /tmp/gcf-inst --strip-components=1 2>/dev/null || (mkdir -p /tmp/gcf-inst && tar xzf "$TGZ" -C /tmp/gcf-inst)
    BIN=$(find /tmp/gcf-inst -name gcf -type f | head -1)
    cp "$BIN" /usr/bin/gcf
    chmod +x /usr/bin/gcf
    rm -rf /tmp/gcf-inst
  else
    echo "[!] .deb yoki .tar.gz topilmadi. Avval Ubuntu da yig'ing:"
    echo "    ./packaging/linux/build-deb.sh"
    exit 1
  fi
fi

# 4. Tekshirish
echo "---"
gcf gpus || true
echo "---"
echo "[OK] GCF o'rnatildi. Sinash:  gcf info <fayl>  |  gcf compress in out.gcf"
