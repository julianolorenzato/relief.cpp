/**
 * @file relief_sandbox_module.h
 * @brief Standalone context for testing relief mapping in isolation, without
 *        going through the simplification/texture-prep pipeline.
 */
#pragma once
#include <QImage>
#include <QLabel>
#include <QWidget>
#include <memory>

#include "gui/relief_view.h"
#include "relief/qem.h"
#include "relief/textures.h"
#include "relief/uv_atlas.h"

class ReliefSandboxModule;

/**
 * Builds the "Mesh" group box, appends it to `outerControls`'s layout, and
 * wires its button to `self`'s slots. Holds only the controls read back
 * afterwards.
 */
struct MeshControls {
    QLabel *statusLbl = nullptr;

    MeshControls() = default;
    MeshControls(QWidget *outerControls, ReliefSandboxModule *self);
};

/**
 * Builds the "Input Textures" group box, appends it to `outerControls`'s
 * layout, and wires its buttons to `self`'s slots. Holds only the controls
 * read back afterwards.
 */
struct TextureControls {
    QLabel *thumbColor = nullptr;
    QLabel *thumbDepth = nullptr;
    QLabel *thumbNormal = nullptr;

    TextureControls() = default;
    TextureControls(QWidget *outerControls, ReliefSandboxModule *self);
};

/**
 * Builds the "Pixel Pick" group box and appends it to `outerControls`'s
 * layout. Holds only the controls read back afterwards.
 */
struct PixelPickControls {
    QLabel *infoLbl = nullptr;
    QLabel *previewLbl = nullptr;

    PixelPickControls() = default;
    explicit PixelPickControls(QWidget *outerControls);
};

/**
 * @brief Standalone context for testing relief mapping in isolation.
 *        Loads a mesh and three textures (color, depth, normal) via file
 *        dialogs, bakes synchronously, and inspects the result in a single
 *        ReliefView.
 */
class ReliefSandboxModule : public QWidget {
    Q_OBJECT

    // Struct constructors wire buttons directly to our private slots.
    friend struct MeshControls;
    friend struct TextureControls;

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
     * Builds the "Relief Mapping Parameters" group box and appends it to
     * `outerControls`'s layout. None of its controls are read back
     * afterwards (they push their state straight into `reliefView` via
     * signal/slot connections), so unlike the other groups it needs no
     * accompanying struct.
     */
    void buildReliefParamsGroup(QWidget *outerControls);

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
    MipPyramid colorMap, reliefMap, normalMap, offsetMap;

    MeshControls meshControls;
    TextureControls textureControls;
    PixelPickControls pixelPickControls;
};
