#include "gui/mainwindow.h"
#include <QMenuBar>
#include <QMenu>
#include <QAction>
#include <QActionGroup>
#include <QFileDialog>
#include <QMessageBox>
#include <QStatusBar>

// ─── Constructor ─────────────────────────────────────────────────────────────

MainWindow::MainWindow(QWidget* parent)
    : QMainWindow(parent)
{
    setWindowTitle("QEM Mesh Simplifier");
    setGeometry(100, 100, 1600, 900);

    setupUI();
    createMenuBar();
}

// ─── Setup ───────────────────────────────────────────────────────────────────

void MainWindow::setupUI()
{
    // ── Create modules ───────────────────────────────────────────────────────
    simplifier_  = new SimplifierModule(this);
    heightmap_   = new HeightmapModule(this);
    texturePrep_ = new TexturePrepModule(this);
    relief_      = new ReliefModule(this);

    // ── Context toolbar ────────────────────────────────────────────────────
    contextToolBar = addToolBar("Contexts");
    contextToolBar->setMovable(false);
    contextToolBar->setStyleSheet(R"(
        QToolBar { background: #2d2d2d; border: none; spacing: 2px; padding: 2px 6px; }
        QToolButton { background: transparent; color: #ccc; border: none;
                      border-radius: 3px; padding: 5px 14px; font-weight: bold; }
        QToolButton:checked { background: #4a7abf; color: white; }
        QToolButton:hover:!checked { background: #3d3d3d; }
    )");
    auto* group = new QActionGroup(this);
    group->setExclusive(true);
    const char* labels[] = {"Mesh", "Heightmap", "Textures", "Relief"};
    for (int i = 0; i < 4; ++i)
    {
        auto* act = new QAction(labels[i], this);
        act->setCheckable(true);
        group->addAction(act);
        contextToolBar->addAction(act);
        connect(act, &QAction::triggered, this, [this, i](bool) { switchContext(i); });
    }
    group->actions().first()->setChecked(true);

    // ── Viewport stack (central widget) ──────────────────────────────────────
    viewportStack = new QStackedWidget();
    viewportStack->addWidget(simplifier_);
    viewportStack->addWidget(heightmap_);
    viewportStack->addWidget(texturePrep_);
    viewportStack->addWidget(relief_);
    setCentralWidget(viewportStack);

    // ── Status bar ───────────────────────────────────────────────────────────
    statusLabel = new QLabel("Ready");
    statusBar()->addWidget(statusLabel);

    // ── Signal wiring ────────────────────────────────────────────────────────

    // simplifier → downstream
    connect(simplifier_, &SimplifierModule::modelLoaded,
            heightmap_,  &HeightmapModule::onModelLoaded);
    connect(simplifier_, &SimplifierModule::simplificationDone,
            heightmap_,  &HeightmapModule::onMeshUpdated);
    connect(simplifier_, &SimplifierModule::modelLoaded,
            this, [this](QEMSimplifier*, QEMSimplifier* s) { texturePrep_->onModelLoaded(s); });
    connect(simplifier_, &SimplifierModule::simplificationDone,
            this, [this](QEMSimplifier*, QEMSimplifier* s) { texturePrep_->onMeshUpdated(s); });
    connect(simplifier_, &SimplifierModule::modelLoaded,
            relief_,     &ReliefModule::setMeshes);
    connect(simplifier_, &SimplifierModule::simplificationDone,
            relief_,     &ReliefModule::setMeshes);

    // heightmap → texture prep
    connect(heightmap_,  &HeightmapModule::bakeReady,
            texturePrep_,&TexturePrepModule::onHeightmapReady);

    // texture prep → relief
    connect(texturePrep_,&TexturePrepModule::texturesReady,
            relief_,     &ReliefModule::onTexturesReady);

    // status messages
    connect(simplifier_, &SimplifierModule::statusMessage,  statusLabel, &QLabel::setText);
    connect(heightmap_,  &HeightmapModule::statusMessage,   statusLabel, &QLabel::setText);
    connect(texturePrep_,&TexturePrepModule::statusMessage, statusLabel, &QLabel::setText);
}

// ─── Menu ─────────────────────────────────────────────────────────────────────

void MainWindow::createMenuBar()
{
    QMenuBar* menuBar = new QMenuBar(this);
    setMenuBar(menuBar);

    QMenu* fileMenu = menuBar->addMenu("&File");

    QAction* loadAction = fileMenu->addAction("&Load Model...");
    connect(loadAction, &QAction::triggered, this, &MainWindow::onLoadModel);

    QAction* saveAction = fileMenu->addAction("&Save Simplified...");
    connect(saveAction, &QAction::triggered, this, &MainWindow::onSaveSimplified);

    fileMenu->addSeparator();

    QAction* exitAction = fileMenu->addAction("E&xit");
    connect(exitAction, &QAction::triggered, this, &QWidget::close);

    QMenu* helpMenu = menuBar->addMenu("&Help");
    QAction* aboutAction = helpMenu->addAction("&About");
    connect(aboutAction, &QAction::triggered, this, [this]() {
        QMessageBox::about(this, "About QEM Simplifier",
            "QEM Mesh Simplifier\n\n"
            "Quadric Error Metrics simplification with Qt GUI\n"
            "Mouse: Drag to rotate, Scroll to zoom\n"
            "Formats: OBJ, GLTF\n\n"
            "Heightmap tab: bakes displacement between simplified and original mesh\n"
            "via shared UV correspondence.");
    });
}

// ─── Slots ───────────────────────────────────────────────────────────────────

void MainWindow::switchContext(int index)
{
    viewportStack->setCurrentIndex(index);
    if (index == 3)
        relief_->onActivated();
}

void MainWindow::onLoadModel()
{
    QString fileName = QFileDialog::getOpenFileName(this,
        "Open Mesh File", "",
        "Model Files (*.obj *.gltf *.glb);;OBJ Files (*.obj);;GLTF Files (*.gltf *.glb);;All Files (*)");

    if (fileName.isEmpty())
        return;

    if (!simplifier_->loadModel(fileName))
        QMessageBox::critical(this, "Error", "Failed to load mesh file!");
}

void MainWindow::onSaveSimplified()
{
    QString fileName = QFileDialog::getSaveFileName(this,
        "Save Simplified Mesh", "",
        "OBJ Files (*.obj);;GLTF Files (*.gltf);;All Files (*)");

    if (fileName.isEmpty())
        return;

    if (!simplifier_->saveSimplified(fileName))
        QMessageBox::critical(this, "Error", "Failed to save mesh!");
    else
    {
        statusLabel->setText("Saved: " + fileName);
        QMessageBox::information(this, "Success", "Mesh saved successfully!");
    }
}
