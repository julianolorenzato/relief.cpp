#include "mainwindow.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QMenuBar>
#include <QMenu>
#include <QAction>
#include <QFileDialog>
#include <QMessageBox>
#include <QGroupBox>
#include <QPushButton>
#include <QCheckBox>
#include <QStatusBar>
#include <QSplitter>
#include <Qt>
#include <iostream>

MainWindow::MainWindow(QWidget* parent)
    : QMainWindow(parent) {
    setWindowTitle("QEM Mesh Simplifier");
    setGeometry(100, 100, 1600, 900);

    originalMesh = std::make_unique<QEMSimplifier>();
    simplifiedMesh = std::make_unique<QEMSimplifier>();

    setupUI();
    createMenuBar();
    updateStatusBar();
}

void MainWindow::setupUI() {
    // Widget central
    QWidget* central = new QWidget(this);
    setCentralWidget(central);

    // Layout principal
    QVBoxLayout* mainLayout = new QVBoxLayout(central);

    // ─── Viewports OpenGL ─────────────────────────────────────────────────
    QWidget* viewportsWidget = new QWidget();
    QHBoxLayout* viewportsLayout = new QHBoxLayout(viewportsWidget);

    // Viewport esquerdo - Original
    QGroupBox* originalGroup = new QGroupBox("Original Mesh", this);
    originalGroup->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    QVBoxLayout* leftLayout = new QVBoxLayout(originalGroup);
    leftLayout->setContentsMargins(4, 4, 4, 4);
    glWidgetOriginal = new GLWidget();
    glWidgetOriginal->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    leftLayout->addWidget(glWidgetOriginal);
    originalStatsLabel = new QLabel("Faces: 0 | Vertices: 0");
    originalStatsLabel->setFixedHeight(20);
    leftLayout->addWidget(originalStatsLabel);
    viewportsLayout->addWidget(originalGroup);

    // Viewport direito - Simplificado
    QGroupBox* simplifiedGroup = new QGroupBox("Simplified Mesh", this);
    simplifiedGroup->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    QVBoxLayout* rightLayout = new QVBoxLayout(simplifiedGroup);
    rightLayout->setContentsMargins(4, 4, 4, 4);
    glWidgetSimplified = new GLWidget();
    glWidgetSimplified->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    rightLayout->addWidget(glWidgetSimplified);
    simplifiedStatsLabel = new QLabel("Faces: 0 | Vertices: 0");
    simplifiedStatsLabel->setFixedHeight(20);
    rightLayout->addWidget(simplifiedStatsLabel);
    viewportsLayout->addWidget(simplifiedGroup);

    viewportsWidget->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    mainLayout->addWidget(viewportsWidget, 1);

    // ─── Controles de simplificação ───────────────────────────────────────
    QGroupBox* controlsGroup = new QGroupBox("Simplification Controls", this);
    QHBoxLayout* controlsLayout = new QHBoxLayout(controlsGroup);

    controlsLayout->addWidget(new QLabel("Target Faces:"));

    targetFacesSpinBox = new QSpinBox();
    targetFacesSpinBox->setMinimum(4);
    targetFacesSpinBox->setMaximum(1000000);
    targetFacesSpinBox->setValue(1000);
    controlsLayout->addWidget(targetFacesSpinBox);

    simplificationSlider = new QSlider(Qt::Horizontal);
    simplificationSlider->setMinimum(1);
    simplificationSlider->setMaximum(100);
    simplificationSlider->setValue(50);
    controlsLayout->addWidget(simplificationSlider);

    QPushButton* simplifyBtn = new QPushButton("Simplify");
    connect(simplifyBtn, &QPushButton::clicked, this, &MainWindow::onSimplify);
    controlsLayout->addWidget(simplifyBtn);

    QPushButton* resetCamBtn = new QPushButton("Reset Cameras");
    connect(resetCamBtn, &QPushButton::clicked, this, &MainWindow::onResetCameras);
    controlsLayout->addWidget(resetCamBtn);

    wireframeCheck = new QCheckBox("Wireframe");
    connect(wireframeCheck, &QCheckBox::toggled, glWidgetOriginal,  &GLWidget::setWireframe);
    connect(wireframeCheck, &QCheckBox::toggled, glWidgetSimplified, &GLWidget::setWireframe);
    controlsLayout->addWidget(wireframeCheck);

    cullFaceCheck = new QCheckBox("Backface Culling");
    cullFaceCheck->setChecked(true);
    connect(cullFaceCheck, &QCheckBox::toggled, glWidgetOriginal,  &GLWidget::setCullFace);
    connect(cullFaceCheck, &QCheckBox::toggled, glWidgetSimplified, &GLWidget::setCullFace);
    controlsLayout->addWidget(cullFaceCheck);

    texturedCheck = new QCheckBox("Textured");
    texturedCheck->setEnabled(false);
    connect(texturedCheck, &QCheckBox::toggled, glWidgetOriginal,   &GLWidget::setTextured);
    connect(texturedCheck, &QCheckBox::toggled, glWidgetSimplified, &GLWidget::setTextured);
    controlsLayout->addWidget(texturedCheck);

    uvViewCheck = new QCheckBox("UV View");
    uvViewCheck->setEnabled(false);
    connect(uvViewCheck, &QCheckBox::toggled, glWidgetOriginal,   &GLWidget::setUVMode);
    connect(uvViewCheck, &QCheckBox::toggled, glWidgetSimplified, &GLWidget::setUVMode);
    controlsLayout->addWidget(uvViewCheck);

    boundaryConstraintCheck = new QCheckBox("Boundary Constraints");
    boundaryConstraintCheck->setChecked(true);
    controlsLayout->addWidget(boundaryConstraintCheck);

    controlsGroup->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    mainLayout->addWidget(controlsGroup);

    // ─── Status bar ───────────────────────────────────────────────────────
    statusLabel = new QLabel("Ready");
    statusBar()->addWidget(statusLabel);

    // Conectar sinais
    connect(targetFacesSpinBox, QOverload<int>::of(&QSpinBox::valueChanged),
            this, &MainWindow::onTargetFacesChanged);
    connect(simplificationSlider, &QSlider::valueChanged, this, [this](int val) {
        int targetFaces = std::max(4, (int)(originalFaceCount * val / 100.0));
        targetFacesSpinBox->blockSignals(true);
        targetFacesSpinBox->setValue(targetFaces);
        targetFacesSpinBox->blockSignals(false);
    });

    // Sincronização de câmeras: cada viewport espelha a outra
    connect(glWidgetOriginal,   &GLWidget::cameraChanged,
            glWidgetSimplified, &GLWidget::syncCamera);
    connect(glWidgetSimplified, &GLWidget::cameraChanged,
            glWidgetOriginal,   &GLWidget::syncCamera);
}

void MainWindow::createMenuBar() {
    QMenuBar* menuBar = new QMenuBar(this);
    setMenuBar(menuBar);

    // File menu
    QMenu* fileMenu = menuBar->addMenu("&File");

    QAction* loadAction = fileMenu->addAction("&Load Model...");
    connect(loadAction, &QAction::triggered, this, &MainWindow::onLoadModel);

    QAction* saveAction = fileMenu->addAction("&Save Simplified...");
    connect(saveAction, &QAction::triggered, this, &MainWindow::onSaveSimplified);

    fileMenu->addSeparator();

    QAction* exitAction = fileMenu->addAction("E&xit");
    connect(exitAction, &QAction::triggered, this, &QWidget::close);

    // Help menu
    QMenu* helpMenu = menuBar->addMenu("&Help");
    QAction* aboutAction = helpMenu->addAction("&About");
    connect(aboutAction, &QAction::triggered, this, [this]() {
        QMessageBox::about(this, "About QEM Simplifier",
            "QEM Mesh Simplifier v1.0\n\n"
            "Quadric Error Metrics simplification with Qt GUI\n"
            "Mouse: Drag to rotate, Scroll to zoom\n"
            "Formats: OBJ, GLTF");
    });
}

void MainWindow::onLoadModel() {
    QString fileName = QFileDialog::getOpenFileName(this,
        "Open Mesh File", "",
        "Model Files (*.obj *.gltf *.glb);;OBJ Files (*.obj);;GLTF Files (*.gltf *.glb);;All Files (*)");

    if (fileName.isEmpty()) return;

    originalMesh = std::make_unique<QEMSimplifier>();
    simplifiedMesh = std::make_unique<QEMSimplifier>();

    bool success = false;
    if (fileName.endsWith(".obj", Qt::CaseInsensitive)) {
        success = originalMesh->loadOBJ(fileName.toStdString());
    } else {
        success = originalMesh->loadGLTF(fileName.toStdString());
    }

    if (!success) {
        QMessageBox::critical(this, "Error", "Failed to load mesh file!");
        return;
    }

    currentFilePath = fileName;
    originalFaceCount = originalMesh->faceCount();
    targetFaceCount = std::max(4, originalFaceCount / 4);

    targetFacesSpinBox->blockSignals(true);
    targetFacesSpinBox->setMaximum(originalFaceCount);
    targetFacesSpinBox->setValue(targetFaceCount);
    simplificationSlider->setValue(75);
    targetFacesSpinBox->blockSignals(false);

    glWidgetOriginal->setMesh(originalMesh.get());
    glWidgetSimplified->setMesh(originalMesh.get());

    bool hasTexture = !originalMesh->textureData.empty();
    texturedCheck->setEnabled(hasTexture);
    if (!hasTexture) texturedCheck->setChecked(false);

    bool hasUVs = false;
    for (const auto& v : originalMesh->vertices)
        if (v.uv.squaredNorm() > 1e-12) { hasUVs = true; break; }
    uvViewCheck->setEnabled(hasUVs);
    if (!hasUVs) uvViewCheck->setChecked(false);

    updateStatusBar();
}

void MainWindow::onSaveSimplified() {
    if (!simplifiedMesh || simplifiedMesh->faceCount() == 0) {
        QMessageBox::warning(this, "Warning", "No simplified mesh to save!\nRun simplification first.");
        return;
    }

    QString fileName = QFileDialog::getSaveFileName(this,
        "Save Simplified Mesh", "",
        "OBJ Files (*.obj);;GLTF Files (*.gltf);;All Files (*)");

    if (fileName.isEmpty()) return;

    bool success = false;
    if (fileName.endsWith(".obj", Qt::CaseInsensitive)) {
        success = simplifiedMesh->saveOBJ(fileName.toStdString());
    } else {
        success = simplifiedMesh->saveGLTF(fileName.toStdString());
    }

    if (success) {
        statusLabel->setText("Saved: " + fileName);
        QMessageBox::information(this, "Success", "Mesh saved successfully!");
    } else {
        QMessageBox::critical(this, "Error", "Failed to save mesh!");
    }
}

void MainWindow::onSimplify() {
    if (!originalMesh || originalMesh->faceCount() == 0) {
        QMessageBox::warning(this, "Warning", "No mesh loaded!");
        return;
    }

    int targetFaces = targetFacesSpinBox->value();

    // Copia mesh original para simplificação
    *simplifiedMesh = *originalMesh;
    simplifiedMesh->useBoundaryConstraints = boundaryConstraintCheck->isChecked();

    statusLabel->setText("Simplifying...");
    statusBar()->repaint();

    simplifiedMesh->simplify(targetFaces);

    glWidgetSimplified->setMesh(simplifiedMesh.get());
    updateStatusBar();
}

void MainWindow::onTargetFacesChanged(int value) {
    targetFaceCount = value;
}

void MainWindow::onResetCameras() {
    glWidgetOriginal->resetCamera();
    glWidgetSimplified->resetCamera();
}

void MainWindow::updateStatusBar() {
    QString originalStats = QString("Original: %1 faces, %2 vertices")
        .arg(originalMesh->faceCount())
        .arg(originalMesh->vertexCount());

    QString simplifiedStats = QString("Simplified: %1 faces, %2 vertices")
        .arg(simplifiedMesh->faceCount())
        .arg(simplifiedMesh->vertexCount());

    originalStatsLabel->setText(originalStats);
    simplifiedStatsLabel->setText(simplifiedStats);

    if (simplifiedMesh->faceCount() > 0) {
        double reduction = 100.0 * (1.0 - (double)simplifiedMesh->faceCount() / originalMesh->faceCount());
        statusLabel->setText(QString("Reduction: %.1f%").arg(reduction));
    } else {
        statusLabel->setText("Ready");
    }
}

void MainWindow::computeAutoTarget() {
    if (originalFaceCount > 0) {
        int target = std::max(4, (int)(originalFaceCount * simplificationSlider->value() / 100.0));
        targetFacesSpinBox->blockSignals(true);
        targetFacesSpinBox->setValue(target);
        targetFacesSpinBox->blockSignals(false);
    }
}
