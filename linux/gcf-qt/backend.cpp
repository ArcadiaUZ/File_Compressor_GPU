#include "backend.h"
#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QProcess>
#include <QRegularExpression>
#include <QStandardPaths>

static QString g_exe;

QString Backend::exePath() { return g_exe; }

bool Backend::init(QString *error) {
    if (!g_exe.isEmpty() && QFileInfo::exists(g_exe)) return true;
    QStringList candidates;
    // 1. GUI yonidagi gcf (portable tar.gz: gcf + gcf-qt bitta papkada)
    candidates << QCoreApplication::applicationDirPath() + "/gcf";
    // 2. Tizimga o'rnatilgan (/usr/bin/gcf — .deb dan keyin)
    candidates << "/usr/bin/gcf" << "/usr/local/bin/gcf";
    // 3. PATH dan qidirish
    QString inPath = QStandardPaths::findExecutable("gcf");
    if (!inPath.isEmpty()) candidates << inPath;
    // 4. Repo ichidan yig'ilgan build (dev rejim): ../../build-linux/gcf
    QDir appDir(QCoreApplication::applicationDirPath());
    candidates << appDir.absoluteFilePath("../../build-linux/gcf")
               << appDir.absoluteFilePath("../gcf");
    for (const QString &c : candidates) {
        QFileInfo fi(c);
        // Papka nomli "gcf" bo'lsa exists true qaytaradi — bajariladiganligini tekshiramiz
        if (fi.isFile() && fi.isExecutable()) { g_exe = c; return true; }
    }
    if (error) *error = "gcf backend topilmadi. Avval o'rnating:\n"
                        "  sudo dpkg -i gcf-*-ubuntu-amd64.deb\n"
                        "yoki gcf ni PATH ga qo'shing.";
    return false;
}

Backend::Result Backend::run(const QStringList &args, int timeoutMs) {
    Result r;
    if (g_exe.isEmpty() && !init()) {
        r.output = "gcf backend topilmadi";
        return r;
    }
    QProcess p;
    p.start(g_exe, args);
    if (!p.waitForStarted(5000)) {
        r.output = "backend ishga tushmadi: " + g_exe;
        return r;
    }
    if (!p.waitForFinished(timeoutMs)) {
        p.kill();
        r.output = "backend vaqtida tugamadi (timeout)";
        return r;
    }
    r.ok = (p.exitCode() == 0);
    r.output = QString::fromUtf8(p.readAllStandardOutput()) +
               QString::fromUtf8(p.readAllStandardError());
    return r;
}

Backend::Result Backend::run(const QString &argLine, int timeoutMs) {
    // Oddiy split (yo'llarda bo'sh joy bo'lsa ham ishlashi uchun pastda
    // chaqiruvchilar QStringList ishlatadi; bu faqat test/info kabi sodda holga).
    return run(QProcess::splitCommand(argLine), timeoutMs);
}

QString Backend::lastLine(const QString &output) {
    QStringList lines;
    for (const QString &l : output.split('\n')) {
        QString t = l.trimmed();
        if (!t.isEmpty()) lines << t;
    }
    if (lines.isEmpty()) return "(bo'sh javob)";
    static const char *keys[] = {"Siqildi:", "Arxivlandi", "Ochildi:", "Chiqarildi:",
                                 "Qo'shildi:", "O'chirildi:", "Papka arxivlandi:", "XATO:"};
    for (int i = lines.size() - 1; i >= 0; --i)
        for (const char *k : keys)
            if (lines[i].startsWith(QLatin1String(k))) return lines[i];
    return lines.last();
}

QString Backend::shortSummary(const QString &line) {
    // "Siqildi: 110000 -> 54568 B (ratio 0.49) 0.8s [CPU-...]"
    static QRegularExpression re(
        R"((Siqildi|Ochildi|Arxivlandi|Papka arxivlandi):\s*(\d+)\s*->\s*(\d+)\s*B.*?([\d.]+)s?\s*(\[.*\])?)",
        QRegularExpression::CaseInsensitiveOption);
    auto m = re.match(line);
    if (!m.hasMatch()) return line;
    auto bytes = [](qint64 b) -> QString {
        if (b < 1024) return QString("%1 B").arg(b);
        if (b < 1048576) return QString("%1 KB").arg(b / 1024.0, 0, 'f', 1);
        if (b < 1073741824) return QString("%1 MB").arg(b / 1048576.0, 0, 'f', 1);
        return QString("%1 GB").arg(b / 1073741824.0, 0, 'f', 2);
    };
    qint64 a = m.captured(2).toLongLong(), b = m.captured(3).toLongLong();
    QString pct = a > 0 ? QString(" (%1%)").arg((double)b / a * 100.0, 0, 'f', 0) : "";
    QString verb = m.captured(1).startsWith("Ochildi") ? "Ochildi" : "Siqildi";
    return QString("%1: %2 → %3%4 • %5s %6")
        .arg(verb, bytes(a), bytes(b), pct, m.captured(4), m.captured(5));
}
