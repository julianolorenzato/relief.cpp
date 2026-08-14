/**
 * @file texture_prep_module.h
 * @brief Pipeline stage that bakes the color/relief/normal mip pyramids and
 *        the cross-seam Offset_Map used by relief mapping.
 */
#pragma once
#include <QWidget>
#include <QLabel>
#include <QComboBox>
#include <QSpinBox>
#include <QPushButton>
#include <QProgressBar>
#include <QCheckBox>
#include <QImage>
#include "relief/heightmap.h"
#include "relief/qem.h"
#include "relief/textures.h"
#include "relief/uv_atlas.h"

/// @brief Widget that generates the baked texture set (color, relief/height,
///        normal mip pyramids, plus the UV-atlas Offset_Map) from the
///        simplified mesh and its heightmap bake, with preview/save controls
///        for each output.
class TexturePrepModule : public QWidget {
    Q_OBJECT

public:
    explicit TexturePrepModule(QWidget* parent = nullptr);

public slots:
    /// Called when a new model is loaded: sets the mesh pointer and does a full reset.
    void onModelLoaded(QEMSimplifier* simplified);
    /// Called when the mesh is updated after simplification: sets mesh + refreshes thumbnails/button.
    void onMeshUpdated(QEMSimplifier* simplified);
    /// Called when a heightmap bake finishes: stores the result and refreshes.
    void onHeightmapReady(const HeightmapResult& result);

    /// @return The baked color mip pyramid (valid once hasTextures() is true).
    const MipPyramid& colorMap() const { return colorMapData_; }
    /// @return The baked relief (min/max/seam-mask) mip pyramid.
    const MipPyramid& reliefMap() const { return reliefMapData_; }
    /// @return The baked normal mip pyramid.
    const MipPyramid& normalMap() const { return normalMapData_; }
    /// @return The baked cross-seam Offset_Map.
    const OffsetMapResult& offsetMap() const { return offsetMapData_; }
    /// @return true once all four baked outputs above are populated.
    bool hasTextures() const {
        return colorMapData_.levelCount() > 0 && reliefMapData_.levelCount() > 0 &&
               normalMapData_.levelCount() > 0 && offsetMapData_.width > 0;
    }

signals:
    /// Emitted once colorMap()/reliefMap()/normalMap()/offsetMap() are all freshly baked.
    void texturesReady();
    void statusMessage(const QString& msg);

private slots:
    /// Launches the (background-threaded) texture generation for the current mesh/settings.
    void onTpGenerate();
    /// Updates the progress bar/label from the worker thread.
    void onTpProgress(int overall, const QString& text);
    /// Called when the worker thread finishes: stores results and refreshes previews.
    void onTpDone();
    /// Saves the output panel `idx` (0=Color, 1=Relief, 2=Normal, 3=Offset) to a file.
    void onTpSave(int idx);

private:
    void buildUI();
    /// Refreshes the three input thumbnails (color/depth/normal) from the mesh and heightmap result.
    void updateThumbnails();
    /// Enables/disables the Generate button based on whether the required inputs are present.
    void updateGenerateEnabled();
    /// Re-renders output preview panel `idx` from the currently baked data.
    void updatePreview(int idx);
    /// Converts one mip level's raw float data into a displayable QImage.
    /// @param data Row-major float data, `channels` floats per texel.
    /// @param w,h Mip level dimensions.
    /// @param channels Number of channels stored per texel.
    /// @param remapSigned If true, remaps [-1,1] to [0,1] before display (for signed data like normals).
    /// @param showChannels Optional per-channel R/G/B/A visibility mask.
    QImage mipLevelToQImage(const std::vector<float>& data, int w, int h, int channels,
                            bool remapSigned, const bool* showChannels = nullptr) const;
    /// Renders the offset map's validity flag (w channel) as a black/white mask image.
    QImage offsetMapMaskImage() const;

    // ── Input thumbnails (0=Color, 1=Depth, 2=Normal) ────────────────────────
    QLabel* tpThumb_[3] = {};

    // ── Controls ──────────────────────────────────────────────────────────────
    QComboBox*    tpResCombo_      = nullptr;
    QSpinBox*     tpSeamBandSpin_  = nullptr;
    QPushButton*  tpGenerateBtn_   = nullptr;
    QProgressBar* tpProgressBar_   = nullptr;
    QLabel*       tpProgressLabel_ = nullptr;

    // ── Output preview panels (0=Color, 1=Relief, 2=Normal, 3=Offset) ────────
    QLabel*      tpPreview_[4]     = {};
    QLabel*      tpInfoLabel_[4]   = {};
    QSpinBox*    tpMipSpin_[4]     = {};
    QPushButton* tpSaveBtn_[4]     = {};
    // Per-panel R/G/B/A preview toggles (panels 0-2 only; panel 3 has no toggles)
    QCheckBox*   tpChannelCheck_[3][4] = {};

    // ── State ─────────────────────────────────────────────────────────────────
    MipPyramid      colorMapData_, reliefMapData_, normalMapData_;
    OffsetMapResult offsetMapData_;

    // Stored heightmap result (copy)
    HeightmapResult hmResult_;

    // Non-owned mesh pointer
    QEMSimplifier* simplifiedMesh_ = nullptr;
};
