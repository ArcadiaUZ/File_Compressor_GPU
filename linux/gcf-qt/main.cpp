#include <QApplication>
#include "mainwindow.h"
#include "style.h"

int main(int argc, char *argv[]) {
    QApplication app(argc, argv);
    app.setApplicationName("GCF");
    app.setOrganizationName("GCF");
    app.setStyleSheet(gcfStyle());
    MainWindow w;
    w.show();
    return app.exec();
}
