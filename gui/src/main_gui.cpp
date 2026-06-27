#include <QApplication>
#include "gui/mainwindow.h"
#include <iostream>

int main(int argc, char* argv[]) {
    QApplication app(argc, argv);

    std::cout << "=== QEM Mesh Simplifier ===\n";
    std::cout << "Starting Qt interface...\n\n";

    MainWindow window;
    window.show();

    return app.exec();
}
