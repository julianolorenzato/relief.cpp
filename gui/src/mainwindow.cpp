/**
 * @file mainwindow.cpp
 * @brief MainWindow implementation: builds the module widgets, wires their
 *        signals into the pipeline, and sets up the toolbar/menu.
 */
#include "gui/mainwindow.h"

#include <QAction>
#include <QActionGroup>
#include <QFileDialog>
#include <QMenu>
#include <QMenuBar>
#include <QMessageBox>
#include <QStatusBar>

using namespace mesh;

// ─── Constructor ─────────────────────────────────────────────────────────────

MainWindow::MainWindow(QWidget *parent) : QMainWindow(parent) {
    setWindowTitle("Relief Standalone Viewer");
    setGeometry(100, 100, 1600, 900);

    setupUI();
    createMenuBar();
}

// ─── Setup ───────────────────────────────────────────────────────────────────

void MainWindow::setupUI() {
    this->globalContext = new GlobalContext(this);

    this->modules = {
        {"Editor", createModule<EditorModule>(this->globalContext, this)},
        {"Heightmap", createModule<HeightmapModule>(this->globalContext, this)},
        {"Texture Prep", createModule<TexturePrepModule>(this->globalContext, this)},
        {"Relief", createModule<ReliefModule>(this->globalContext, this)},
        {"Relief Sandbox", createModule<ReliefSandboxModule>(this->globalContext, this)},
        {"Normal Map", createModule<NormalMapModule>(this->globalContext, this)},
        {"Bounding", createModule<BoundingModule>(this->globalContext, this)},
    };

    auto *heightmap = static_cast<HeightmapModule *>(this->modules[1].second);
    auto *texturePrep = static_cast<TexturePrepModule *>(this->modules[2].second);
    auto *relief = static_cast<ReliefModule *>(this->modules[3].second);

    // ── Context toolbar ────────────────────────────────────────────────────
    this->contextToolBar = addToolBar("Contexts");
    this->contextToolBar->setMovable(false);
    this->contextToolBar->setStyleSheet(R"(
        QToolBar { background: #2d2d2d; border: none; spacing: 2px; padding: 2px 6px; }
        QToolButton { background: transparent; color: #ccc; border: none;
                      border-radius: 3px; padding: 5px 14px; font-weight: bold; }
        QToolButton:checked { background: #4a7abf; color: white; }
        QToolButton:hover:!checked { background: #3d3d3d; }
    )");
    // ── Viewport stack (central widget) ──────────────────────────────────────
    this->viewportStack = new QStackedWidget();

    auto *group = new QActionGroup(this);
    group->setExclusive(true);
    int i = 0;
    for (auto &[label, module] : this->modules) {
        auto *act = new QAction(label, this);
        act->setCheckable(true);
        group->addAction(act);
        this->contextToolBar->addAction(act);
        connect(act, &QAction::triggered, this,
                [this, i](bool) { this->viewportStack->setCurrentIndex(i); });

        this->viewportStack->addWidget(module);
        ++i;
    }
    group->actions().first()->setChecked(true);

    setCentralWidget(this->viewportStack);

    // ── Status bar ───────────────────────────────────────────────────────────
    this->statusLabel = new QLabel("Ready");
    statusBar()->addWidget(this->statusLabel);

    // ── Signal wiring ────────────────────────────────────────────────────────

    // GlobalContext → every module's onMeshLoaded(original, simplified)/onMeshUpdated() is
    // wired by the Module base class itself, so no explicit connects are needed here for the
    // initial load or for a re-simplify (EditorModule emits notifyMeshUpdate()).

    // heightmap → texture prep
    connect(heightmap, &HeightmapModule::bakeReady, texturePrep,
            &TexturePrepModule::onHeightmapReady);

    // texture prep → relief
    connect(texturePrep, &TexturePrepModule::texturesReady, this,
            [relief, texturePrep]() { relief->onTexturesReady(texturePrep); });

    // status messages
    for (auto &[label, module] : this->modules) {
        connect(module, &Module::statusMessage, this->statusLabel, &QLabel::setText);
    }
}

// ─── Menu
// ─────────────────────────────────────────────────────────────────────

void MainWindow::createMenuBar() {
    QMenuBar *menuBar = new QMenuBar(this);
    setMenuBar(menuBar);

    QMenu *fileMenu = menuBar->addMenu("&File");

    QAction *loadAction = fileMenu->addAction("&Load Model...");
    connect(loadAction, &QAction::triggered, this, &MainWindow::onLoadModel);

    QAction *saveAction = fileMenu->addAction("&Save Simplified...");
    connect(saveAction, &QAction::triggered, this, &MainWindow::onSaveSimplified);

    fileMenu->addSeparator();

    QAction *exitAction = fileMenu->addAction("E&xit");
    connect(exitAction, &QAction::triggered, this, &QWidget::close);

    QMenu *helpMenu = menuBar->addMenu("&Help");
    QAction *aboutAction = helpMenu->addAction("&About");
    connect(aboutAction, &QAction::triggered, this, [this]() {
        QMessageBox::about(this, "About QEM Simplifier",
                           "QEM Mesh Simplifier\n\n"
                           "Quadric Error Metrics simplification with Qt GUI\n"
                           "Mouse: Drag to rotate, Scroll to zoom\n"
                           "Formats: OBJ, GLTF\n\n"
                           "Heightmap tab: bakes displacement between simplified "
                           "and original mesh\n"
                           "via shared UV correspondence.");
    });
}

// ─── Slots ───────────────────────────────────────────────────────────────────

void MainWindow::onLoadModel() {
    QString fileName = QFileDialog::getOpenFileName(
        this, "Open Mesh File", "",
        "Model Files (*.obj *.gltf *.glb);;OBJ Files (*.obj);;GLTF Files (*.gltf "
        "*.glb);;All Files (*)");

    if (fileName.isEmpty()) return;

    if (!globalContext->loadModel(fileName))
        QMessageBox::critical(this, "Error", "Failed to load mesh file!");
}

void MainWindow::onSaveSimplified() {
    QString fileName = QFileDialog::getSaveFileName(
        this, "Save Simplified Mesh", "", "OBJ Files (*.obj);;GLTF Files (*.gltf);;All Files (*)");

    if (fileName.isEmpty()) return;

    auto *editor = static_cast<EditorModule *>(modules[0].second);
    if (!editor->saveSimplified(fileName))
        QMessageBox::critical(this, "Error", "Failed to save mesh!");
    else {
        statusLabel->setText("Saved: " + fileName);
        QMessageBox::information(this, "Success", "Mesh saved successfully!");
    }
}
