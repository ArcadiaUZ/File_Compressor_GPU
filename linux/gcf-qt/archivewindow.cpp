#include "archivewindow.h"
#include "backend.h"
#include "filetypes.h"

#include <QApplication>
#include <QDesktopServices>
#include <QDir>
#include <QFileDialog>
#include <QFileInfo>
#include <QHeaderView>
#include <QLabel>
#include <QMessageBox>
#include <QPushButton>
#include <QToolBar>
#include <QTreeWidget>
#include <QUrl>
#include <QVBoxLayout>

ArchiveWindow::ArchiveWindow(const QString &archPath, QWidget *parent)
    : QMainWindow(parent), m_arch(archPath) {
    setWindowTitle(QFileInfo(archPath).fileName() + " — GCF Arxiv");
    resize(900, 640);

    auto *central = new QWidget(this);
    auto *lay = new QVBoxLayout(central);
    lay->setContentsMargins(0, 0, 0, 0);
    lay->setSpacing(0);

    auto *tb = new QToolBar(this);
    tb->setToolButtonStyle(Qt::ToolButtonTextUnderIcon);
    tb->setMovable(false);
    auto *aAdd = tb->addAction(QString::fromUtf8("➕\nQo'shish"));
    auto *aExt = tb->addAction(QString::fromUtf8("📤\nChiqarish"));
    auto *aTest = tb->addAction(QString::fromUtf8("✅\nTekshirish"));
    auto *aView = tb->addAction(QString::fromUtf8("👁\nKo'rish"));
    auto *aDel = tb->addAction(QString::fromUtf8("🗑\nO'chirish"));
    auto *aInfo = tb->addAction(QString::fromUtf8("ℹ\nMa'lumot"));
    connect(aAdd, &QAction::triggered, this, &ArchiveWindow::addFiles);
    connect(aExt, &QAction::triggered, this, &ArchiveWindow::extract);
    connect(aTest, &QAction::triggered, this, &ArchiveWindow::testArchive);
    connect(aView, &QAction::triggered, this, &ArchiveWindow::viewSelected);
    connect(aDel, &QAction::triggered, this, &ArchiveWindow::deleteSelected);
    connect(aInfo, &QAction::triggered, this, &ArchiveWindow::showInfo);
    lay->addWidget(tb);

    auto *infoBox = new QWidget(this);
    infoBox->setObjectName("infobar");
    auto *infoLay = new QVBoxLayout(infoBox);
    m_pathLabel = new QLabel(archPath, this);
    m_pathLabel->setObjectName("fileLabel");
    m_summaryLabel = new QLabel("", this);
    m_summaryLabel->setObjectName("sectionTitle");
    infoLay->addWidget(m_pathLabel);
    infoLay->addWidget(m_summaryLabel);
    lay->addWidget(infoBox);

    m_list = new QTreeWidget(this);
    m_list->setColumnCount(4);
    m_list->setHeaderLabels(QStringList() << "Nomi" << "Hajm" << "Siqilgan" << "Turi");
    m_list->setSelectionMode(QAbstractItemView::ExtendedSelection);
    m_list->header()->setStretchLastSection(false);
    m_list->header()->setSectionResizeMode(0, QHeaderView::Stretch);
    connect(m_list, &QTreeWidget::itemDoubleClicked, this, &ArchiveWindow::openDoubleClicked);
    lay->addWidget(m_list, 1);

    m_status = new QLabel("Tayyor.", this);
    m_status->setObjectName("statusbar");
    lay->addWidget(m_status);

    setCentralWidget(central);
    reload();
}
