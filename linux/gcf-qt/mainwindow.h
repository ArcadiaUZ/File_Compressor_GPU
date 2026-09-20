#pragma once
// Asosiy oyna — WPF dagi MainWindow.xaml analogi:
// GPU badge, drag&drop, Siqish/Ochish/Arxiv, jurnal.
#include <QMainWindow>

class QLabel;
class QProgressBar;
class QTextEdit;
class QPushButton;

class MainWindow : public QMainWindow {
    Q_OBJECT
public:
    explicit MainWindow(QWidget *parent = nullptr);

protected:
    void dragEnterEvent(QDragEnterEvent *e) override;
    void dropEvent(QDropEvent *e) override;

private slots:
    void pickFile();
    void pickFolder();
    void compress();
    void decompress();
    void openArchive();
    void clearLog();

private:
    void initBackend();
    void selectPath(const QString &path);
    void setWorking(bool w);
    void addLog(const QString &msg);
    bool needFile();
    QString gpuBadge(const QString &output);

    QString m_selected;
    QStringList m_log;
    QLabel *m_gpuBadge = nullptr;
    QLabel *m_fileLabel = nullptr;
    QTextEdit *m_logText = nullptr;
    QProgressBar *m_progress = nullptr;
    QPushButton *m_compressBtn = nullptr;
    QPushButton *m_decompressBtn = nullptr;
    QPushButton *m_pickBtn = nullptr;
    QPushButton *m_pickFolderBtn = nullptr;
    QPushButton *m_openArchBtn = nullptr;
    QPushButton *m_clearBtn = nullptr;
};
