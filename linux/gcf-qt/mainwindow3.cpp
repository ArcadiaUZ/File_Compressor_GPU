#include "mainwindow.h"
#include "archivewindow.h"
#include "backend.h"
#include <QApplication>
#include <QFileDialog>
#include <QFileInfo>
void MainWindow::compress() {
    if (!needFile()) return;
    bool isDir = QFileInfo(m_selected).isDir();
    QString defName = QFileInfo(m_selected).fileName() + ".gcf";
    QString out = QFileDialog::getSaveFileName(this, "Siqilgan faylni saqlash", defName, "GCF fayl (*.gcf)");
    if (out.isEmpty()) return;
    setWorking(true);
    QApplication::setOverrideCursor(Qt::WaitCursor);
    addLog(QString::fromUtf8(isDir ? "📁 Papka siqilmoqda: " : "⬇ Fayl siqilmoqda: ") +
           QFileInfo(m_selected).fileName() + " ...");
    Backend::Result r = isDir ? Backend::run(QStringList() << "compress-folder" << m_selected << out)
                              : Backend::run(QStringList() << "compress" << m_selected << out);
    QApplication::restoreOverrideCursor();
    setWorking(false);
    addLog(r.ok ? QString::fromUtf8("✅ ") + Backend::shortSummary(Backend::lastLine(r.output))
                : QString::fromUtf8("❌ ") + Backend::lastLine(r.output));
}

void MainWindow::decompress() {
    if (!needFile()) return;
    QString defName = m_selected.endsWith(".gcf", Qt::CaseInsensitive)
        ? QFileInfo(m_selected).fileName().chopped(4)
        : QFileInfo(m_selected).fileName() + ".out";
    QString out = QFileDialog::getSaveFileName(this, "Ochilgan faylni saqlash", defName);
    if (out.isEmpty()) return;
    setWorking(true);
    QApplication::setOverrideCursor(Qt::WaitCursor);
    addLog(QString::fromUtf8("⬆ Ochilmoqda: ") + QFileInfo(m_selected).fileName() + " ...");
    Backend::Result r = Backend::run(QStringList() << "decompress" << m_selected << out);
    QApplication::restoreOverrideCursor();
    setWorking(false);
    if (!r.ok && r.output.contains("arxivda")) {
        addLog(QString::fromUtf8("📂 Bu ko'p faylli arxiv — arxiv oynasida ochildi."));
        auto *w = new ArchiveWindow(m_selected);
        w->setAttribute(Qt::WA_DeleteOnClose);
        w->show();
        return;
    }
    addLog(r.ok ? QString::fromUtf8("✅ ") + Backend::shortSummary(Backend::lastLine(r.output))
                : QString::fromUtf8("❌ ") + Backend::lastLine(r.output));
}

void MainWindow::openArchive() {
    QString f = QFileDialog::getOpenFileName(this, "GCF arxivni ochish", QString(), "GCF arxiv (*.gcf)");
    if (!f.isEmpty()) {
        auto *w = new ArchiveWindow(f);
        w->setAttribute(Qt::WA_DeleteOnClose);
        w->show();
    }
}
