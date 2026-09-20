# Linux papkasi — Ubuntu/Debian uchun hamma narsa shu yerda

```
linux/
  gcf-qt/        Qt Widgets GUI (C++, WPF analogi)
    backend.*       gcf CLI ni topib ishga tushiradi
    mainwindow.*    asosiy oyna (Siqish/Ochish/Arxiv/Jurnal)
    archivewindow.* arxiv oynasi (WinRAR uslubi)
    filetypes.h     kengaytma -> tur nomi + emoji
    style.h         dark-purple mavzu (WPF ranglari)
    main.cpp        kirish nuqtasi
    CMakeLists.txt  Qt5/Qt6 bilan yig'iladi
    build-gui.sh    faqat GUI ni yig'adi
  build-all.sh     backend + GUI + .deb ni birdan yasaydi
  README.md        to'liq yo'riqnoma
```

## Tez start (Ubuntu ichida)

```bash
./linux/build-all.sh
# -> gcf-1.0.0-ubuntu-amd64.deb (ichida: gcf + gcf-qt)
sudo dpkg -i gcf-1.0.0-ubuntu-amd64.deb
gcf gpus        # CLI
gcf-qt          # GUI
```

Alohida:
```bash
./packaging/linux/build-deb.sh  # faqat backend .deb
./linux/gcf-qt/build-gui.sh     # faqat GUI
```
