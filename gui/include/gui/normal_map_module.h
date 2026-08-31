/**
 * @file normal_map_module.h
 * @brief Standalone context for deriving a tangent-space normal map from a
 *        height map, without going through the simplification/texture-prep
 *        pipeline.
 */
#pragma once
#include <QCheckBox>
#include <QComboBox>
#include <QImage>
#include <QLabel>
#include <QPushButton>
#include <QSpinBox>
#include <QWidget>

#include "relief/textures.h"

/**
 * @brief Standalone tool tab that loads a height/depth image via a file
 *        dialog, derives a normal-map mip pyramid from it with
 *        textures::buildNormalMapFromHeight, and previews/saves the result.
 *        It is not wired into the pipeline.
 */
class NormalMapModule : public QWidget {
    Q_OBJECT

   public:
    explicit NormalMapModule(QWidget *parent = nullptr);

   signals:
    void statusMessage(const QString &msg);

   private slots:
    /** Loads the source height/depth image via file dialog. */
    void onLoadHeight();

    /** Derives the normal map from the loaded height image. */
    void onGenerate();

    /** Saves mip 0 of the generated normal map as a PNG. */
    void onSave();

   private:
    /// Gradient scale handed to textures::buildNormalMapFromHeight, in UV
    /// units: 1.0 treats the height map's [0,1] range as a 45-degree slope.
    static constexpr float kStrength = 1.0f;

    /// Gaussian sigma (in texels) used to smooth the height field before
    /// differentiating, so 8-bit quantization terraces don't collapse the
    /// slope into one-texel lines.
    static constexpr float kSmoothing = 2.0f;

    void buildUI();

    /** Redraws the generated-normal-map panel from `normalMap`. */
    void updatePreview();

    /** Source height image as loaded from disk. */
    QImage heightImg;

    /** Normal pyramid derived from `heightImg` by the last Generate. */
    MipPyramid normalMap;

    // ── Preview panels ───────────────────────────────────────────────────
    QLabel *heightPreview = nullptr;
    QLabel *normalPreview = nullptr;
    QLabel *normalInfoLbl = nullptr;
    QCheckBox *channelCheck[3] = {};
    QSpinBox *mipSpin = nullptr;
    QPushButton *saveBtn = nullptr;

    // ── Controls ─────────────────────────────────────────────────────────
    QLabel *heightThumb = nullptr;
    QComboBox *resCombo = nullptr;
    QPushButton *generateBtn = nullptr;
};
