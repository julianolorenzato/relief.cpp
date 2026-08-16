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

// ─── Constructor ─────────────────────────────────────────────────────────────

MainWindow::MainWindow(QWidget *parent) : QMainWindow(parent) {
  setWindowTitle("Relief Standalone Viewer");
  setGeometry(100, 100, 1600, 900);

  setupUI();
  createMenuBar();
}

// ─── Setup ───────────────────────────────────────────────────────────────────

void MainWindow::setupUI() {
  // ── Create modules ───────────────────────────────────────────────────────
  this->simplifier = new SimplifierModule(this);
  this->heightmap = new HeightmapModule(this);
  this->texturePrep = new TexturePrepModule(this);
  this->relief = new ReliefModule(this);
  this->reliefTest = new ReliefTestModule(this);

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
  auto *group = new QActionGroup(this);
  group->setExclusive(true);
  const char *labels[] = {"Mesh", "Heightmap", "Textures", "Relief",
                          "Relief Test"};
  for (int i = 0; i < 5; ++i) {
    auto *act = new QAction(labels[i], this);
    act->setCheckable(true);
    group->addAction(act);
    this->contextToolBar->addAction(act);
    connect(act, &QAction::triggered, this,
            [this, i](bool) { switchContext(i); });
  }
  group->actions().first()->setChecked(true);

  // ── Viewport stack (central widget) ──────────────────────────────────────
  this->viewportStack = new QStackedWidget();
  this->viewportStack->addWidget(this->simplifier);
  this->viewportStack->addWidget(this->heightmap);
  this->viewportStack->addWidget(this->texturePrep);
  this->viewportStack->addWidget(this->relief);
  this->viewportStack->addWidget(this->reliefTest);
  setCentralWidget(this->viewportStack);

  // ── Status bar ───────────────────────────────────────────────────────────
  this->statusLabel = new QLabel("Ready");
  statusBar()->addWidget(this->statusLabel);

  // ── Signal wiring ────────────────────────────────────────────────────────

  // simplifier → downstream
  connect(this->simplifier, &SimplifierModule::modelLoaded, this->heightmap,
          &HeightmapModule::onModelLoaded);
  connect(this->simplifier, &SimplifierModule::simplificationDone,
          this->heightmap, &HeightmapModule::onMeshUpdated);
  connect(this->simplifier, &SimplifierModule::modelLoaded, this,
          [this](QEMSimplifier *, QEMSimplifier *s) {
            this->texturePrep->onModelLoaded(s);
          });
  connect(this->simplifier, &SimplifierModule::simplificationDone, this,
          [this](QEMSimplifier *, QEMSimplifier *s) {
            this->texturePrep->onMeshUpdated(s);
          });
  connect(this->simplifier, &SimplifierModule::modelLoaded, this->relief,
          &ReliefModule::setMeshes);
  connect(this->simplifier, &SimplifierModule::simplificationDone, this->relief,
          &ReliefModule::setMeshes);

  // heightmap → texture prep
  connect(this->heightmap, &HeightmapModule::bakeReady, this->texturePrep,
          &TexturePrepModule::onHeightmapReady);

  // texture prep → relief
  connect(this->texturePrep, &TexturePrepModule::texturesReady, this,
          [this]() { this->relief->onTexturesReady(this->texturePrep); });

  // status messages
  connect(this->simplifier, &SimplifierModule::statusMessage, this->statusLabel,
          &QLabel::setText);
  connect(this->heightmap, &HeightmapModule::statusMessage, this->statusLabel,
          &QLabel::setText);
  connect(this->texturePrep, &TexturePrepModule::statusMessage,
          this->statusLabel, &QLabel::setText);
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

void MainWindow::switchContext(int index) {
  viewportStack->setCurrentIndex(index);
  if (index == 3)
    relief->onActivated();
}

void MainWindow::onLoadModel() {
  QString fileName = QFileDialog::getOpenFileName(
      this, "Open Mesh File", "",
      "Model Files (*.obj *.gltf *.glb);;OBJ Files (*.obj);;GLTF Files (*.gltf "
      "*.glb);;All Files (*)");

  if (fileName.isEmpty())
    return;

  if (!simplifier->loadModel(fileName))
    QMessageBox::critical(this, "Error", "Failed to load mesh file!");
}

void MainWindow::onSaveSimplified() {
  QString fileName = QFileDialog::getSaveFileName(
      this, "Save Simplified Mesh", "",
      "OBJ Files (*.obj);;GLTF Files (*.gltf);;All Files (*)");

  if (fileName.isEmpty())
    return;

  if (!simplifier->saveSimplified(fileName))
    QMessageBox::critical(this, "Error", "Failed to save mesh!");
  else {
    statusLabel->setText("Saved: " + fileName);
    QMessageBox::information(this, "Success", "Mesh saved successfully!");
  }
}
