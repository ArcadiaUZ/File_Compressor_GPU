#pragma once
// WPF dark-purple mavzu (#1E1E2E + #7C5CFF) ning Qt StyleSheet analogi.
#include <QString>

inline QString gcfStyle() {
    return R"(
QMainWindow, QWidget#central { background: #1E1E2E; }
QWidget { color: #E8E8F0; font-size: 14px; }
QLabel#gpuBadge { background: #2A2A3E; border-radius: 10px; padding: 9px 12px;
    font-weight: bold; font-size: 15px; }
QLabel#dropZone { background: #252536; border: 2px dashed #7C5CFF; border-radius: 14px;
    padding: 20px; font-size: 16px; }
QLabel#fileLabel { color: #AAAAB8; font-size: 13px; }
QLabel#sectionTitle { color: #AAAAB8; font-weight: bold; font-size: 13px; }
QPushButton { background: #7C5CFF; color: white; font-size: 15px; font-weight: bold;
    border: 0; border-radius: 10px; padding: 12px; }
QPushButton:hover { background: #9378FF; }
QPushButton:disabled { background: #3A3A4E; color: #888898; }
QPushButton[class="secondary"] { background: #2A2A3E; font-size: 13px; }
QTextEdit { background: #2A2A3E; border-radius: 10px; padding: 8px; color: #C9C9D8; }
QProgressBar { background: transparent; border: 0; height: 6px; }
QProgressBar::chunk { background: #7C5CFF; }
QToolBar { background: #252536; spacing: 4px; padding: 4px; }
QToolBar QToolButton { background: transparent; color: #E8E8F0; padding: 6px 10px;
    border-radius: 8px; }
QToolBar QToolButton:hover { background: #2A2A3E; }
QWidget#infobar { background: #2A2A3E; }
QLabel#statusbar { background: #252536; color: #AAAAB8; padding: 6px 10px; }
QTreeWidget { background: #1E1E2E; color: #E8E8F0; border: 0; font-size: 13px; }
QTreeWidget::item:selected { background: #3D3D66; color: white; }
QTreeWidget::item:hover { background: #2A2A40; }
QHeaderView::section { background: #252536; color: #AAAAB8; padding: 6px 8px; border: 0;
    font-weight: bold; }
QFileDialog { background: #1E1E2E; }
QMessageBox { background: #1E1E2E; }
)";
}
