/**
 * @file main.cpp
 * @brief Application entry point: creates the Qt application and the main window.
 */
#include <QApplication>
#include "gui/mainwindow.h"
#include <iostream>

int main(int argc, char* argv[]) {
    QApplication app(argc, argv);

    std::cout << "Starting GUI...\n\n";

    MainWindow window;
    window.show();

    return app.exec();
}
