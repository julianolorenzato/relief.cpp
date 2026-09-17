/**
 * @file validation_module.h
 * @brief Validation stage: builds and compares multiple Simplify/Inflate/
 *        Smooth pipelines against the original mesh, side by side, scored by
 *        rendered-image PSNR; can also auto-search for the pipeline that
 *        best preserves PSNR at a target face-count reduction.
 */
#pragma once
#include <QWidget>
#include <QComboBox>
#include <QSlider>
#include <QSpinBox>
#include <QDoubleSpinBox>
#include <QCheckBox>
#include <QLabel>
#include <QPushButton>
#include <QProgressBar>
#include <QStackedWidget>
#include <QTableWidget>
#include <QVBoxLayout>
#include <array>
#include <optional>
#include <vector>
#include "gui/module.h"
#include "gui/orbital3dview.h"
#include "gui/offscreen_mesh_renderer.h"
#include "relief/pipeline.h"
#include "relief/pipeline_search.h"

/// @brief Widget that runs several configurable Simplify/Inflate/Smooth
///        pipelines against copies of the loaded mesh, shows each result in
///        its own viewport with a PSNR score, and can auto-search for the
///        best-preserving pipeline at a given target reduction.
class ValidationModule : public Module {
    Q_OBJECT

public:
    using Module::Module;

public slots:
    void onMeshLoaded(mesh::Mesh* original, mesh::Mesh* simplified) override;

private:
    void buildUI() override;

    static constexpr int kMaxSlots = 4;
    static constexpr int kStepsPerSlot = 3;

    /// One pipeline step's UI: an op-type combo plus its (mutually exclusive,
    /// shown-by-selection) parameter widgets.
    struct StepUI {
        QComboBox* opCombo = nullptr;
        QStackedWidget* paramsStack = nullptr; ///< pages: None, Simplify, Inflate, Smooth

        QSlider* simplifySlider = nullptr;
        QLabel* simplifyLabel = nullptr;
        QDoubleSpinBox* inflateSpin = nullptr;
        QSpinBox* smoothIterSpin = nullptr;
        QDoubleSpinBox* smoothLambdaSpin = nullptr;
    };

    /// One candidate viewport/pipeline slot.
    struct Slot {
        Orbital3DView* view = nullptr;
        QCheckBox* visibleCheck = nullptr;
        QLabel* psnrLabel = nullptr;
        std::array<StepUI, kStepsPerSlot> steps;
        mesh::Mesh resultMesh;
    };

    void buildSlotUI(QVBoxLayout* manualTabLayout, int slotIndex);
    void buildStepUI(QVBoxLayout* slotLayout, int slotIndex, int stepIndex);
    /// @return The op the given step is currently configured as, or nullopt if "None".
    std::optional<pipeline::Op> stepOp(int slotIndex, int stepIndex) const;
    pipeline::Pipeline buildSlotPipeline(int slotIndex) const;
    void runSlot(int slotIndex);
    /// Renders `source`/candidate (after applying `p` to a copy of `source`) and
    /// returns the averaged multi-view PSNR between them.
    double scorePipeline(const mesh::Mesh& source, const pipeline::Pipeline& p);

    void buildAutoOptimizeUI(QVBoxLayout* layout);
    void onRunAutoOptimize();
    void applySearchResultToSlot(int resultRow, int slotIndex);

    std::array<Slot, kMaxSlots> slots_;
    Orbital3DView* originalView_ = nullptr;
    OffscreenMeshRenderer offscreenRenderer_{192};

    mesh::Mesh sourceMesh_;
    int originalFaceCount_ = 0;
    double inflateScale_ = 1.0;

    // Auto-optimize tab
    QCheckBox* includeSimplifyCheck_ = nullptr;
    QCheckBox* includeInflateCheck_ = nullptr;
    QCheckBox* includeSmoothCheck_ = nullptr;
    QSlider* targetReductionSlider_ = nullptr;
    QLabel* targetReductionLabel_ = nullptr;
    QProgressBar* searchProgress_ = nullptr;
    QPushButton* runSearchBtn_ = nullptr;
    QPushButton* cancelSearchBtn_ = nullptr;
    QTableWidget* resultsTable_ = nullptr;
    bool searchCancelRequested_ = false;
    std::vector<pipeline::Pipeline> lastSearchResults_;
};
