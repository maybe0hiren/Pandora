#include "ui/main_window.h"
#include <QApplication>

int main(int argc, char *argv[]) {
    QApplication app(argc, argv);
    QCoreApplication::setOrganizationName("PandoraProject");
    QCoreApplication::setApplicationName("Pandora");
    pandora::MainWindow window;
    window.show();
    return app.exec();
}
