#include "mainwindow.h"
#include "archivewindow.h"
#include "backend.h"
#include <QApplication>
#include <QDragEnterEvent>
#include <QDropEvent>
#include <QFileDialog>
#include <QFileInfo>
#include <QLabel>
#include <QMimeData>
#include <QProgressBar>
#include <QPushButton>
#include <QScrollBar>
#include <QTextEdit>
void MainWindow::initBackend() {
    QString err;
    if (!Backend::init(&err)) {
        m_gpuBadge->setText(QString::fromUtf8("⚠️ ") + err);
        addLog(QString::fromUtf8("⚠️ Backend topilmadi: ") + err);
        return;
    }
    auto r = Backend::run(QStringList() << "gpus");
    m_gpuBadge->setText(r.ok ? gpuBadge(r.output) : QString::fromUtf8("⚠️ Backend ishlamadi"));
    addLog("GCF tayyor. Fayl tanlang yoki tashlang.");
    QStringList args = QApplication::arguments();
    if (args.size() > 1 && args[1].endsWith(".gcf", Qt::CaseInsensitive)) {
        auto *w = new ArchiveWindow(args[1]);
        w->setAttribute(Qt::WA_DeleteOnClose);
        w->show();
    }
}

QString MainWindow::gpuBadge(const QString &output) {
    QString name, cuda;
    for (const QString &l : output.split('\n')) {
        QString t = l.trimmed();
        if (t.startsWith("GPU:")) name = t.mid(4).trimmed();
        if (t.startsWith("CUDA:")) cuda = t.mid(5).trimmed();
    }
    if (name.isEmpty()) name = "Noma'lum GPU";
    if (name.length() > 34) name = name.left(34) + "…";
    return cuda.startsWith("HA") ? QString::fromUtf8("🟢 %1 • CUDA").arg(name)
                                 : QString::fromUtf8("🟡 %1 • CPU rejim").arg(name);
}

void MainWindow::addLog(const QString &msg) {
    m_log << msg;
    while (m_log.size() > 60) m_log.removeFirst();
    m_logText->setPlainText(m_log.join('\n'));
    m_logText->verticalScrollBar()->setValue(m_logText->verticalScrollBar()->maximum());
}

void MainWindow::clearLog() { m_log.clear(); m_logText->setPlainText("Jurnal tozalandi."); }

void MainWindow::setWorking(bool w) {
    m_compressBtn->setEnabled(!w);
    m_decompressBtn->setEnabled(!w);
    m_pickBtn->setEnabled(!w);
    m_pickFolderBtn->setEnabled(!w);
    m_openArchBtn->setEnabled(!w);
    m_clearBtn->setEnabled(!w);
    m_progress->setVisible(w);
}

void MainWindow::selectPath(const QString &path) {
    m_selected = path;
    QFileInfo fi(path);
    m_fileLabel->setText((fi.isDir() ? QString::fromUtf8("📁 ") : QString::fromUtf8("📄 ")) + path);
    if (path.endsWith(".gcf", Qt::CaseInsensitive) && !Backend::exePath().isEmpty()) {
        auto *w = new ArchiveWindow(path);
        w->setAttribute(Qt::WA_DeleteOnClose);
        w->show();
    }
}

void MainWindow::dragEnterEvent(QDragEnterEvent *e) {
    if (e->mimeData()->hasUrls()) e->acceptProposedAction();
}

void MainWindow::dropEvent(QDropEvent *e) {
    auto urls = e->mimeData()->urls();
    if (!urls.isEmpty()) selectPath(urls.first().toLocalFile());
}

void MainWindow::pickFile() {
    QString f = QFileDialog::getOpenFileName(this, "Fayl tanlang");
    if (!f.isEmpty()) selectPath(f);
}

void MainWindow::pickFolder() {
    QString d = QFileDialog::getExistingDirectory(this, "Siqiladigan papkani tanlang");
    if (!d.isEmpty()) selectPath(d);
}

bool MainWindow::needFile() {
    if (m_selected.isEmpty() || !QFileInfo::exists(m_selected)) {
        addLog(QString::fromUtf8("⚠️ Avval fayl yoki papka tanlang (tashlang yoki tugmani bosing)."));
        return false;
    }
    return true;
}
