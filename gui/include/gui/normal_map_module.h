/**
 * @file normal_map_module.h
 * @brief Standalone context for deriving a tangent-space normal map from a
 *        height map, without going through the simplification/texture-prep
 *        pipeline.
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
    QDoubleSpinBox *strengthSpin = nullptr;
    QDoubleSpinBox *smoothingSpin = nullptr;
    QPushButton *generateBtn = nullptr;
};
