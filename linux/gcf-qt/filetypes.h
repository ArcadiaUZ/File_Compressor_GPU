#pragma once
// Fayl turidan nom + emoji-belgi (WPF dagi FileTypes.TypeOf/IconOf analogi).
#include <QString>

namespace FileTypes {

inline QString typeOf(const QString &name) {
    if (name == "file") return "Fayl (eski arxiv)";
    QString ext;
    int dot = name.lastIndexOf('.');
    if (dot >= 0) ext = name.mid(dot).toLower();
    if (ext == ".txt" || ext == ".log" || ext == ".md") return "Matn hujjati";
    if (ext == ".jpg" || ext == ".jpeg" || ext == ".png" || ext == ".gif" ||
        ext == ".bmp" || ext == ".webp" || ext == ".svg") return "Rasm";
    if (ext == ".mp3" || ext == ".wav" || ext == ".flac" || ext == ".ogg") return "Audio";
    if (ext == ".mp4" || ext == ".avi" || ext == ".mkv" || ext == ".mov") return "Video";
    if (ext == ".zip" || ext == ".rar" || ext == ".7z" || ext == ".gcf" || ext == ".gz") return "Arxiv";
    if (ext == ".exe" || ext == ".msi") return "Dastur";
    if (ext == ".pdf") return "PDF hujjat";
    if (ext == ".doc" || ext == ".docx") return "Word hujjat";
    if (ext == ".xls" || ext == ".xlsx") return "Excel jadval";
    if (ext == ".json" || ext == ".xml" || ext == ".yml" || ext == ".yaml") return "Ma'lumot fayli";
    if (ext == ".dll" || ext == ".sys" || ext == ".so" || ext == ".deb") return "Tizim fayli";
    if (ext == ".py" || ext == ".cs" || ext == ".cpp" || ext == ".c" ||
        ext == ".h" || ext == ".js" || ext == ".html" || ext == ".sh") return "Kod fayli";
    if (ext.isEmpty()) return "Fayl";
    return ext.mid(1).toUpper() + " fayl";
}

inline QString iconOf(const QString &name) {
    QString ext;
    int dot = name.lastIndexOf('.');
    if (dot >= 0) ext = name.mid(dot).toLower();
    if (ext == ".jpg" || ext == ".jpeg" || ext == ".png" || ext == ".gif" ||
        ext == ".bmp" || ext == ".webp" || ext == ".svg") return QString::fromUtf8("🖼");
    if (ext == ".mp3" || ext == ".wav" || ext == ".flac" || ext == ".ogg") return QString::fromUtf8("🎵");
    if (ext == ".mp4" || ext == ".avi" || ext == ".mkv" || ext == ".mov") return QString::fromUtf8("🎬");
    if (ext == ".zip" || ext == ".rar" || ext == ".7z" || ext == ".gcf" || ext == ".gz") return QString::fromUtf8("📦");
    if (ext == ".exe" || ext == ".msi") return QString::fromUtf8("⚙");
    if (ext == ".pdf") return QString::fromUtf8("📕");
    if (ext == ".doc" || ext == ".docx") return QString::fromUtf8("📝");
    if (ext == ".xls" || ext == ".xlsx") return QString::fromUtf8("📊");
    if (ext == ".py" || ext == ".cs" || ext == ".cpp" || ext == ".c" ||
        ext == ".h" || ext == ".js" || ext == ".html" || ext == ".sh") return QString::fromUtf8("💻");
    return QString::fromUtf8("📄");
}

// 110000 -> "107.4 KB" (WPF dagi FormatBytes analogi)
inline QString formatBytes(qint64 b) {
    if (b < 1024) return QString("%1 B").arg(b);
    if (b < 1048576) return QString("%1 KB").arg(b / 1024.0, 0, 'f', 1);
    if (b < 1073741824) return QString("%1 MB").arg(b / 1048576.0, 0, 'f', 1);
    return QString("%1 GB").arg(b / 1073741824.0, 0, 'f', 2);
}

} // namespace FileTypes
