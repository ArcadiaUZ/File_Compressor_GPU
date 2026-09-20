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
#include <QUrl>
#include <QVBoxLayout>

MainWindow::MainWindow(QWidget *parent) : QMainWindow(parent) {
    setWindowTitle("GCF — GPU Fayl Siqish");
    resize(680, 760);
    setAcceptDrops(true);

    auto *central = new QWidget(this);
    auto *lay = new QVBoxLayout(central);
    lay->setContentsMargins(22, 22, 22, 22);
    lay->setSpacing(10);

    m_gpuBadge = new QLabel(QString::fromUtf8("⏳ GPU tekshirilmoqda..."), this);
    m_gpuBadge->setObjectName("gpuBadge");
    lay->addWidget(m_gpuBadge);

    auto *drop = new QLabel(QString::fromUtf8("📦\nFaylni shu yerga tashlang\n(yoki pastdagi tugmalar)"), this);
    drop->setObjectName("dropZone");
    drop->setAlignment(Qt::AlignCenter);
    drop->setMinimumHeight(120);
    lay->addWidget(drop);

    auto *pickRow = new QHBoxLayout();
    m_pickBtn = new QPushButton("Fayl tanlash", this);
    m_pickFolderBtn = new QPushButton(QString::fromUtf8("📁 Papka"), this);
    m_pickFolderBtn->setProperty("class", "secondary");
    connect(m_pickBtn, &QPushButton::clicked, this, &MainWindow::pickFile);
    connect(m_pickFolderBtn, &QPushButton::clicked, this, &MainWindow::pickFolder);
    pickRow->addWidget(m_pickBtn);
    pickRow->addWidget(m_pickFolderBtn);
    lay->addLayout(pickRow);

    m_fileLabel = new QLabel("Hali fayl tanlanmagan", this);
    m_fileLabel->setObjectName("fileLabel");
    m_fileLabel->setWordWrap(true);
    lay->addWidget(m_fileLabel);

    auto *actRow = new QHBoxLayout();
    m_compressBtn = new QPushButton(QString::fromUtf8("⬇ Siqish"), this);
    m_decompressBtn = new QPushButton(QString::fromUtf8("⬆ Ochish"), this);
    connect(m_compressBtn, &QPushButton::clicked, this, &MainWindow::compress);
    connect(m_decompressBtn, &QPushButton::clicked, this, &MainWindow::decompress);
    actRow->addWidget(m_compressBtn);
    actRow->addWidget(m_decompressBtn);
    lay->addLayout(actRow);

    m_openArchBtn = new QPushButton(QString::fromUtf8("📂 Arxiv ochish"), this);
    m_openArchBtn->setProperty("class", "secondary");
    connect(m_openArchBtn, &QPushButton::clicked, this, &MainWindow::openArchive);
    lay->addWidget(m_openArchBtn);

    m_progress = new QProgressBar(this);
    m_progress->setRange(0, 0);
    m_progress->setTextVisible(false);
    m_progress->setFixedHeight(6);
    m_progress->hide();
    lay->addWidget(m_progress);

    auto *logHead = new QHBoxLayout();
    auto *logTitle = new QLabel(QString::fromUtf8("📋 Jurnal"), this);
    logTitle->setObjectName("sectionTitle");
    m_clearBtn = new QPushButton("Tozalash", this);
    m_clearBtn->setProperty("class", "secondary");
    connect(m_clearBtn, &QPushButton::clicked, this, &MainWindow::clearLog);
    logHead->addWidget(logTitle);
    logHead->addStretch();
    logHead->addWidget(m_clearBtn);
    lay->addLayout(logHead);

    m_logText = new QTextEdit(this);
    m_logText->setReadOnly(true);
    m_logText->setText("GCF tayyor.");
    lay->addWidget(m_logText, 1);

    setCentralWidget(central);
    initBackend();
}
