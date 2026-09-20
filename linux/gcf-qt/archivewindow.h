#pragma once
// Arxiv oynasi — WPF dagi ArchiveWindow analogi (WinRAR uslubi):
// toolbar, fayllar jadvali, chiqarish/qo'shish/test/ko'rish/o'chirish/info.
#include <QMainWindow>

class QLabel;
class QTreeWidget;

class ArchiveWindow : public QMainWindow {
    Q_OBJECT
public:
    explicit ArchiveWindow(const QString &archPath, QWidget *parent = nullptr);

private slots:
    void reload();
    void extract();
    void addFiles();
    void testArchive();
    void viewSelected();
    void deleteSelected();
    void showInfo();
    void openDoubleClicked();

private:
    QList<int> selectedIndices();
    void setBusy(bool b);

    QString m_arch;
    QLabel *m_pathLabel = nullptr;
    QLabel *m_summaryLabel = nullptr;
    QLabel *m_status = nullptr;
    QTreeWidget *m_list = nullptr;
};
