#include "archivewindow.h"
#include "backend.h"
#include "filetypes.h"

#include <QApplication>
#include <QDesktopServices>
#include <QDir>
#include <QFileDialog>
#include <QMessageBox>
#include <QRegularExpression>
#include <QTreeWidget>
#include <QUrl>
#include <algorithm>

void ArchiveWindow::setBusy(bool b) {
    QApplication::setOverrideCursor(b ? Qt::WaitCursor : Qt::ArrowCursor);
    if (!b) QApplication::restoreOverrideCursor();
}

QList<int> ArchiveWindow::selectedIndices() {
    QList<int> out;
    for (auto *it : m_list->selectedItems()) out << it->data(0, Qt::UserRole).toInt();
    std::sort(out.begin(), out.end());
    return out;
}

void ArchiveWindow::reload() {
    auto r = Backend::run(QStringList() << "list" << m_arch);
    m_list->clear();
    if (!r.ok) { m_status->setText(QString::fromUtf8("❌ ") + Backend::lastLine(r.output)); return; }
    qint64 totalOrig = 0, totalPacked = 0;
    int n = 0;
    for (const QString &line : r.output.split('\n')) {
        QString t = line.trimmed();
        if (t.isEmpty()) continue;
        // Format: idx|nom|orig|packed|[method]. Nom ichida '|' bo'lishi mumkin —
        // boshidan idx, oxiridan orig/packed/method ajratamiz (WPF bilan bir xil).
        QStringList p = t.split('|');
        if (p.size() < 4) continue;
        bool ok0 = false;
        int idx = p[0].toInt(&ok0);
        if (!ok0) continue;
        QString method = "huff";
        QString last = p.last().trimmed();
        int numStart = p.size() - 3;
        if (last == "huff" || last == "store" || last == "lz") {
            method = last;
            numStart = p.size() - 4;
        }
        if (numStart < 1) continue;
        bool ok1 = false, ok2 = false;
        qint64 orig = p[numStart].toLongLong(&ok1);
        qint64 packed = p[numStart + 1].toLongLong(&ok2);
        if (!ok1 || !ok2 || orig < 0 || packed < 0) continue;
        QString name = p.mid(1, numStart - 1).join('|');
        if (name.isEmpty()) continue;
        auto *it = new QTreeWidgetItem(m_list);
        QString type = FileTypes::typeOf(name);
        if (method == "lz") type += " •LZ";
        else if (method == "store") type += " •RAW";
        it->setText(0, FileTypes::iconOf(name) + " " + name);
        it->setText(1, FileTypes::formatBytes(orig));
        it->setText(2, FileTypes::formatBytes(packed));
        it->setText(3, type);
        it->setData(0, Qt::UserRole, idx);
        it->setData(0, Qt::UserRole + 1, name); // xom nom (TOCTOU siz ochish uchun)
        totalOrig += orig; totalPacked += packed; n++;
    }
    m_summaryLabel->setText(QString("%1 ta fayl • %2 → %3").arg(n).arg(
        FileTypes::formatBytes(totalOrig), FileTypes::formatBytes(totalPacked)));
    m_status->setText(QString::fromUtf8("✅ %1 ta yozuv").arg(n));
}

void ArchiveWindow::extract() {
    QString dir = QFileDialog::getExistingDirectory(this, "Qayerga chiqarilsin?");
    if (dir.isEmpty()) return;
    setBusy(true);
    QStringList args;
    args << "extract" << m_arch << dir;
    for (int i : selectedIndices()) args << QString::number(i);
    auto r = Backend::run(args, 600000);
    setBusy(false);
    m_status->setText(r.ok ? QString::fromUtf8("✅ ") + Backend::lastLine(r.output) + " → " + dir
                           : QString::fromUtf8("❌ ") + Backend::lastLine(r.output));
}

void ArchiveWindow::addFiles() {
    QStringList files = QFileDialog::getOpenFileNames(this, "Arxivga qo'shish");
    if (files.isEmpty()) return;
    setBusy(true);
    QStringList args;
    args << "add" << m_arch;
    args += files;
    auto r = Backend::run(args, 600000);
    setBusy(false);
    m_status->setText(r.ok ? QString::fromUtf8("✅ ") + Backend::lastLine(r.output)
                           : QString::fromUtf8("❌ ") + Backend::lastLine(r.output));
    if (r.ok) reload();
}

void ArchiveWindow::testArchive() {
    setBusy(true);
    auto r = Backend::run(QStringList() << "test" << m_arch, 600000);
    setBusy(false);
    m_status->setText(r.ok ? QString::fromUtf8("✅ ") + Backend::lastLine(r.output)
                           : QString::fromUtf8("❌ ") + Backend::lastLine(r.output));
}

void ArchiveWindow::viewSelected() { openDoubleClicked(); }

void ArchiveWindow::openDoubleClicked() {
    auto *it = m_list->currentItem();
    if (!it) { m_status->setText(QString::fromUtf8("⚠️ Avval ro'yxatdan fayl tanlang.")); return; }
    int idx = it->data(0, Qt::UserRole).toInt();
    // Keshdagi xom nom (ikkinchi `list` TOCTOU siz): reload da UserRole+1 ga saqlangan
    QString name = it->data(0, Qt::UserRole + 1).toString();
    if (name.isEmpty()) { m_status->setText("⚠️ Nom topilmadi"); return; }
    // Traversal himoyasi (WPF bilan bir xil): .. / absolut / : taqiqlanadi
    if (name.startsWith('/') || name.contains(':') || name.contains("..")) {
        m_status->setText(QString::fromUtf8("⚠️ Xavfli fayl nomi bloklandi: ") + name);
        return;
    }
    for (const QString &comp : name.split(QRegularExpression("[/\\\\]"))) {
        if (comp == ".." || comp == "." || comp.isEmpty()) {
            m_status->setText(QString::fromUtf8("⚠️ Xavfli fayl nomi bloklandi: ") + name);
            return;
        }
    }
    // Bajariladigan kengaytmalar (.desktop including Exec= RCE) — tasdiq so'raymiz
    QString lower = name.toLower();
    bool exec = lower.endsWith(".desktop") || lower.endsWith(".sh") || lower.endsWith(".exe")
        || lower.endsWith(".bat") || lower.endsWith(".ps1") || lower.endsWith(".lnk");
    if (exec) {
        if (QMessageBox::question(this, "Xavfsizlik",
                QString("\"%1\" bajariladigan fayl bo'lishi mumkin. Ochilsinmi?").arg(name))
                != QMessageBox::Yes) return;
    }
    QString tmp = QDir::tempPath() + "/GCF/open";
    QDir().mkpath(tmp);
    setBusy(true);
    auto r = Backend::run(QStringList() << "extract" << m_arch << tmp << QString::number(idx));
    setBusy(false);
    if (!r.ok) { m_status->setText(QString::fromUtf8("❌ ") + Backend::lastLine(r.output)); return; }
    QString full = QDir(tmp).absoluteFilePath(name);
    // tmp prefiks check (canonical)
    QString canonTmp = QDir(tmp).canonicalPath();
    QFileInfo fi(full);
    QString canonFull = fi.absoluteFilePath();
    // Symlink/cleanPath tekshiruvi
    QString clean = QDir::cleanPath(canonFull);
    if (!clean.startsWith(canonTmp + "/") && clean != canonTmp) {
        m_status->setText(QString::fromUtf8("⚠️ Xavfli yo'l bloklandi"));
        return;
    }
    if (!QDesktopServices::openUrl(QUrl::fromLocalFile(full)))
        m_status->setText(QString::fromUtf8("⚠️ Ochilmadi: ") + full);
    else
        m_status->setText(QString::fromUtf8("📂 ") + name);
}

void ArchiveWindow::deleteSelected() {
    auto sel = selectedIndices();
    if (sel.isEmpty()) { m_status->setText(QString::fromUtf8("⚠️ Avval ro'yxatdan fayl tanlang.")); return; }
    if (QMessageBox::question(this, "Tasdiq",
            QString("%1 ta yozuv o'chirilsinmi?").arg(sel.size())) != QMessageBox::Yes) return;
    setBusy(true);
    QStringList args;
    args << "remove" << m_arch;
    for (int i : sel) args << QString::number(i);
    auto r = Backend::run(args);
    setBusy(false);
    m_status->setText(r.ok ? QString::fromUtf8("✅ ") + Backend::lastLine(r.output)
                           : QString::fromUtf8("❌ ") + Backend::lastLine(r.output));
    if (r.ok) reload();
}

void ArchiveWindow::showInfo() {
    auto r = Backend::run(QStringList() << "info" << m_arch);
    QMessageBox::information(this, "Arxiv ma'lumoti", r.ok ? r.output : Backend::lastLine(r.output));
}
