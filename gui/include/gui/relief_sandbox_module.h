/**
 * @file relief_sandbox_module.h
 * @brief Standalone context for testing relief mapping in isolation, without
 *        going through the simplification/texture-prep pipeline.
 */
#pragma once
#include <QCheckBox>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QImage>
#include <QLabel>
#include <QPushButton>
#include <QSpinBox>
#include <QWidget>
#include <memory>

#include "gui/relief_view.h"
#include "relief/qem.h"
#include "relief/textures.h"
#include "relief/uv_atlas.h"

/**
 * @brief Standalone context for testing relief mapping in isolation.
 *        Loads a mesh and three textures (color, depth, normal) via file
 *        dialogs, bakes synchronously, and inspects the result in a single
 *        ReliefView.
 */
class ReliefSandboxModule : public QWidget {
    Q_OBJECT

   public:
    explicit ReliefSandboxModule(QWidget *parent = nullptr);

   private slots:
    /** Loads a mesh (OBJ/GLTF) via file dialog and rebuilds the view. */
    void onLoadMesh();

    /** Loads the color texture via file dialog. */
    void onLoadColor();

    /** Loads the depth/height texture via file dialog. */
    void onLoadDepth();

    /** Loads the normal texture via file dialog. */
    void onLoadNormal();

    /** Opens the TextureInspectorDialog over the currently baked pyramids. */
    void onInspectTextures();

    /**
     * Handles a pick result forwarded from ReliefView, updating the pick
     * preview panel.
     */
    void onPixelPicked(QPointF uv, bool hit);

   private:
    QWidget *buildControls();

    /**
     * Rebuilds the mip pyramids (color/relief/normal/offset) from the
     * loaded images and pushes them to the view.
     */
    void recomputeDepthTextures();

    /**
     * Mesh used to render with Relief Mapping technique.
     *
     * It is supposed to already be simplified.
     */
    std::unique_ptr<QEMSimplifier> mesh;

    /**
     * The widget that renders the mesh
     * using the Relief Mapping technique.
     */
    ReliefView *reliefView = nullptr;

    /** Images used as input. */
    QImage colorImg, depthImg, normalImg;

    /**
     * Baked pyramids fed to ReliefView,
     * kept around for the texture inspector.
     */
    MipPyramid colorMapData, reliefMapData, normalMapData;
    OffsetMapResult offsetMapData;

    // ── Load controls ---
    QPushButton *loadMeshBtn = nullptr;
    QLabel *meshStatusLbl = nullptr;
    QPushButton *loadColorBtn = nullptr;
    QPushButton *loadDepthBtn = nullptr;
    QPushButton *loadNormalBtn = nullptr;
    QLabel *thumbColor = nullptr;
    QLabel *thumbDepth = nullptr;
    QLabel *thumbNormal = nullptr;
    QPushButton *inspectTexturesBtn = nullptr;
    QLabel *pickInfoLbl = nullptr;
    QLabel *pickPreviewLbl = nullptr;

    // ── Relief controls ---
    QCheckBox *reliefEnabledCheck = nullptr;
    QSpinBox *stepsSpin = nullptr;
    QDoubleSpinBox *depthScaleSpin = nullptr;
    QCheckBox *useAtlasCheck = nullptr;
    QComboBox *textureTypeCombo = nullptr;
    QComboBox *debugViewCombo = nullptr;
    QCheckBox *wireframeCheck = nullptr;
    QCheckBox *cullFaceCheck = nullptr;
    QPushButton *resetCamBtn = nullptr;
};
