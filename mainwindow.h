#pragma once
#include <QMainWindow>
#include <QSlider>
#include <QSpinBox>
#include <QLabel>
#include <QCheckBox>
#include <memory>
#include "qem.h"
#include "glwidget.h"

class MainWindow : public QMainWindow {
    Q_OBJECT

public:
    explicit MainWindow(QWidget* parent = nullptr);
    ~MainWindow() override = default;

private slots:
    void onLoadModel();
    void onSaveSimplified();
    void onSimplify();
    void onTargetFacesChanged(int value);
    void onResetCameras();

private:
    void setupUI();
    void createMenuBar();
    void updateStatusBar();
    void computeAutoTarget();

    // Dados
    std::unique_ptr<QEMSimplifier> originalMesh;
    std::unique_ptr<QEMSimplifier> simplifiedMesh;

    // UI
    GLWidget* glWidgetOriginal = nullptr;
    GLWidget* glWidgetSimplified = nullptr;
    QSlider* simplificationSlider = nullptr;
    QSpinBox* targetFacesSpinBox = nullptr;
    QLabel* statusLabel = nullptr;
    QLabel* originalStatsLabel = nullptr;
    QLabel* simplifiedStatsLabel = nullptr;

    QCheckBox* wireframeCheck          = nullptr;
    QCheckBox* cullFaceCheck           = nullptr;
    QCheckBox* texturedCheck           = nullptr;
    QCheckBox* uvViewCheck             = nullptr;
    QCheckBox* boundaryConstraintCheck = nullptr;

    // Controle
    QString currentFilePath;
    int originalFaceCount = 0;
    int targetFaceCount = 0;
};
