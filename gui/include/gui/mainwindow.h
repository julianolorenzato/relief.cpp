#pragma once
#include <QMainWindow>
#include <QLabel>
#include <QToolBar>
#include <QStackedWidget>
#include "gui/simplifier_module.h"
#include "gui/heightmap_module.h"
#include "gui/texture_prep_module.h"
#include "gui/relief_module.h"

class MainWindow : public QMainWindow {
    Q_OBJECT

public:
    explicit MainWindow(QWidget* parent = nullptr);
    ~MainWindow() override = default;

private slots:
    void onLoadModel();
    void onSaveSimplified();

private:
    void setupUI();
    void createMenuBar();
    void switchContext(int index);

    QToolBar*       contextToolBar = nullptr;
    QStackedWidget* viewportStack  = nullptr;
    QLabel*         statusLabel    = nullptr;

    SimplifierModule*  simplifier_  = nullptr;
    HeightmapModule*   heightmap_   = nullptr;
    TexturePrepModule* texturePrep_ = nullptr;
    ReliefModule*      relief_      = nullptr;
};
