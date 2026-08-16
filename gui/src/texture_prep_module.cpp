/**
 * @file texture_prep_module.cpp
 * @brief TexturePrepModule implementation: bakes and previews the
 *        color/relief/normal mip pyramids and the Offset_Map.
 */
#include "gui/texture_prep_module.h"
#include "relief/textures.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QGroupBox>
#include <QScrollArea>
#include <QSplitter>
#include <QFileDialog>
#include <QMessageBox>
#include <QPixmap>
#include <algorithm>
#include <cmath>

// ─── Static helpers ───────────────────────────────────────────────────────────

/// Wraps a raw RGBA8 buffer (e.g. a mesh's embedded texture) in a detached QImage copy.
static QImage rgbaTextureToQImage(const std::vector<uint8_t>& data, int w, int h)
{
    if (data.empty() || w <= 0 || h <= 0)
        return QImage();
    QImage img(data.data(), w, h, w * 4, QImage::Format_RGBA8888);
    return img.copy(); // detach from the mesh's buffer
}

// ─── Constructor ─────────────────────────────────────────────────────────────

TexturePrepModule::TexturePrepModule(QWidget* parent)
    : QWidget(parent)
{
    buildUI();
}

// ─── buildUI ─────────────────────────────────────────────────────────────────

void TexturePrepModule::buildUI()
{
    QHBoxLayout* outerLayout = new QHBoxLayout(this);
    outerLayout->setContentsMargins(0, 0, 0, 0);

    QSplitter* splitter = new QSplitter(Qt::Horizontal, this);
    outerLayout->addWidget(splitter);

    // ── Left: 4 preview panels ────────────────────────────────────────────────
    static const char* previewTitles[4] = {
        "Color Map",
        "Relief Map  (R=min G=max(mip-bound) B=offset mask A=—)",
        "Normal Map",
        "Offset Map  (atlas leap mask)"
    };

    QWidget* panelsWidget = new QWidget();
    QHBoxLayout* panelsLayout = new QHBoxLayout(panelsWidget);
    panelsLayout->setSpacing(12);

    for (int i = 0; i < 4; i++)
    {
        QGroupBox* panel = new QGroupBox(previewTitles[i]);
        QVBoxLayout* pLayout = new QVBoxLayout(panel);
        panel->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);

        tpPreview_[i] = new QLabel();
        tpPreview_[i]->setAlignment(Qt::AlignCenter);
        tpPreview_[i]->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
        tpPreview_[i]->setMinimumSize(180, 180);
        tpPreview_[i]->setStyleSheet("background-color:#1e1e1e;border:1px solid #555;");
        tpPreview_[i]->setText("(not generated)");
        pLayout->addWidget(tpPreview_[i], 1);

        tpInfoLabel_[i] = new QLabel("—");
        tpInfoLabel_[i]->setFixedHeight(18);
        tpInfoLabel_[i]->setAlignment(Qt::AlignCenter);
        pLayout->addWidget(tpInfoLabel_[i]);

        if (i < 3)
        {
            static const char* chanLabel[4] = {"R", "G", "B", "A"};
            static const char* chanTooltip[3][4] = {
                {"Red", "Green", "Blue", "Alpha"},
                {"Min depth (mip bound)", "Max depth (mip bound)", "Offset/seam mask", "Reserved (always 0)"},
                {"X", "Y", "Z", ""},
            };
            QHBoxLayout* chanRow = new QHBoxLayout();
            chanRow->addWidget(new QLabel("Channels:"));
            for (int c = 0; c < 4; c++)
            {
                tpChannelCheck_[i][c] = new QCheckBox(chanLabel[c]);
                tpChannelCheck_[i][c]->setChecked(true);
                tpChannelCheck_[i][c]->setToolTip(chanTooltip[i][c]);
                connect(tpChannelCheck_[i][c], &QCheckBox::toggled, this, [this, idx = i](bool) {
                    updatePreview(idx);
                });
                chanRow->addWidget(tpChannelCheck_[i][c]);
            }
            if (i == 2)
            {
                tpChannelCheck_[i][3]->setChecked(false);
                tpChannelCheck_[i][3]->setEnabled(false);
            }
            chanRow->addStretch();
            pLayout->addLayout(chanRow);
        }

        QHBoxLayout* btnRow = new QHBoxLayout();
        btnRow->addWidget(new QLabel("Mip:"));
        tpMipSpin_[i] = new QSpinBox();
        tpMipSpin_[i]->setRange(0, 0);
        tpMipSpin_[i]->setEnabled(false);
        int idx = i;
        connect(tpMipSpin_[i], QOverload<int>::of(&QSpinBox::valueChanged), this,
                [this, idx](int) { updatePreview(idx); });
        btnRow->addWidget(tpMipSpin_[i]);

        tpSaveBtn_[i] = new QPushButton("Save");
        tpSaveBtn_[i]->setEnabled(false);
        connect(tpSaveBtn_[i], &QPushButton::clicked, this, [this, idx]() { onTpSave(idx); });
        btnRow->addWidget(tpSaveBtn_[i]);

        pLayout->addLayout(btnRow);
        panelsLayout->addWidget(panel);
    }

    panelsWidget->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    splitter->addWidget(panelsWidget);

    // ── Right: controls in a QScrollArea ─────────────────────────────────────
    QWidget* controlsWidget = new QWidget();
    QVBoxLayout* mainLayout = new QVBoxLayout(controlsWidget);

    QGroupBox* ctrlGroup = new QGroupBox("Input Textures && Baking Controls");
    QVBoxLayout* ctrlOuter = new QVBoxLayout(ctrlGroup);

    // Input texture thumbnails
    static const char* thumbCaptions[3] = {"Color", "Depth", "Normal"};
    QHBoxLayout* thumbRow = new QHBoxLayout();
    for (int i = 0; i < 3; i++)
    {
        QVBoxLayout* col = new QVBoxLayout();
        col->setSpacing(2);
        tpThumb_[i] = new QLabel();
        tpThumb_[i]->setFixedSize(56, 56);
        tpThumb_[i]->setAlignment(Qt::AlignCenter);
        tpThumb_[i]->setStyleSheet("background-color:#1e1e1e;border:1px solid #555;");
        tpThumb_[i]->setText("—");
        col->addWidget(tpThumb_[i]);
        QLabel* caption = new QLabel(thumbCaptions[i]);
        caption->setAlignment(Qt::AlignCenter);
        caption->setStyleSheet("font-size: 10px;");
        col->addWidget(caption);
        thumbRow->addLayout(col);
    }
    ctrlOuter->addLayout(thumbRow);

    QHBoxLayout* resRow = new QHBoxLayout();
    resRow->addWidget(new QLabel("Resolution:"));
    tpResCombo_ = new QComboBox();
    tpResCombo_->addItem("128 × 128",   128);
    tpResCombo_->addItem("256 × 256",   256);
    tpResCombo_->addItem("512 × 512",   512);
    tpResCombo_->addItem("1024 × 1024", 1024);
    tpResCombo_->addItem("2048 × 2048", 2048);
    tpResCombo_->setCurrentIndex(2);
    resRow->addWidget(tpResCombo_, 1);
    ctrlOuter->addLayout(resRow);

    QHBoxLayout* seamRow = new QHBoxLayout();
    seamRow->addWidget(new QLabel("Seam Band:"));
    tpSeamBandSpin_ = new QSpinBox();
    tpSeamBandSpin_->setRange(1, 32);
    tpSeamBandSpin_->setValue(4);
    tpSeamBandSpin_->setToolTip(
        "Width (in texels) of the atlas-leap band baked around UV seams.\n"
        "Wider bands tolerate longer relief-mapping rays crossing islands.");
    seamRow->addWidget(tpSeamBandSpin_, 1);
    ctrlOuter->addLayout(seamRow);

    tpGenerateBtn_ = new QPushButton("Generate");
    tpGenerateBtn_->setEnabled(false);
    connect(tpGenerateBtn_, &QPushButton::clicked, this, &TexturePrepModule::onTpGenerate);
    ctrlOuter->addWidget(tpGenerateBtn_);

    QHBoxLayout* progressRow = new QHBoxLayout();
    tpProgressBar_ = new QProgressBar();
    tpProgressBar_->setRange(0, 100);
    tpProgressBar_->setValue(0);
    tpProgressBar_->setTextVisible(true);
    tpProgressBar_->setFixedHeight(18);
    progressRow->addWidget(tpProgressBar_, 1);

    tpProgressLabel_ = new QLabel("Ready");
    tpProgressLabel_->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    progressRow->addWidget(tpProgressLabel_);
    ctrlOuter->addLayout(progressRow);

    mainLayout->addWidget(ctrlGroup);
    mainLayout->addStretch();

    QScrollArea* scrollArea = new QScrollArea();
    scrollArea->setWidget(controlsWidget);
    scrollArea->setWidgetResizable(true);
    scrollArea->setMinimumWidth(220);
    scrollArea->setMaximumWidth(360);
    scrollArea->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);

    splitter->addWidget(scrollArea);
}

// ─── Public slots ─────────────────────────────────────────────────────────────

void TexturePrepModule::onModelLoaded(QEMSimplifier* simplified)
{
    simplifiedMesh_ = simplified;
    hmResult_ = HeightmapResult{};
    colorMapData_  = MipPyramid{};
    reliefMapData_ = MipPyramid{};
    normalMapData_ = MipPyramid{};
    offsetMapData_ = MipPyramid{};

    for (int i = 0; i < 4; i++)
    {
        tpPreview_[i]->setText("(not generated)");
        tpPreview_[i]->setPixmap(QPixmap());
        tpInfoLabel_[i]->setText("—");
        tpSaveBtn_[i]->setEnabled(false);
        tpMipSpin_[i]->setEnabled(false);
        tpMipSpin_[i]->setRange(0, 0);
    }

    updateThumbnails();
    updateGenerateEnabled();
}

void TexturePrepModule::onMeshUpdated(QEMSimplifier* simplified)
{
    simplifiedMesh_ = simplified;
    updateThumbnails();
    updateGenerateEnabled();
}

void TexturePrepModule::onHeightmapReady(const HeightmapResult& result)
{
    hmResult_ = result;
    updateThumbnails();
    updateGenerateEnabled();
}

// ─── Private slots ────────────────────────────────────────────────────────────

void TexturePrepModule::onTpGenerate()
{
    if (!simplifiedMesh_ || simplifiedMesh_->faceCount() == 0)
        return;

    int kRes     = tpResCombo_->currentData().toInt();
    int seamBand = tpSeamBandSpin_->value();

    tpGenerateBtn_->setEnabled(false);
    onTpProgress(5, "Resampling color map...");
    RawImage rawColor{simplifiedMesh_->textureData.data(),
                       simplifiedMesh_->textureWidth, simplifiedMesh_->textureHeight, 4};
    colorMapData_ = Textures::buildColorMap(rawColor, kRes, kRes);

    onTpProgress(30, "Resampling normal map...");
    RawImage rawNormal{simplifiedMesh_->normalTextureData.data(),
                        simplifiedMesh_->normalTextureWidth, simplifiedMesh_->normalTextureHeight, 4};
    normalMapData_ = Textures::buildNormalMap(rawNormal, kRes, kRes);

    onTpProgress(70, "Baking UV-atlas offset map...");
    offsetMapData_ = UVAtlas::buildOffsetMap(*simplifiedMesh_, kRes, kRes, seamBand);

    onTpProgress(85, "Building relief map...");
    RawImage rawDepth{hmResult_.image.data(), hmResult_.width, hmResult_.height, 1};
    reliefMapData_ = Textures::buildReliefMap(rawDepth, kRes, kRes, offsetMapData_);

    onTpDone();
    emit texturesReady();
    emit statusMessage("Texture generation complete.");
}

void TexturePrepModule::onTpProgress(int overall, const QString& text)
{
    tpProgressBar_->setValue(overall);
    tpProgressLabel_->setText(text);
}

void TexturePrepModule::onTpDone()
{
    onTpProgress(100, "Done");
    tpGenerateBtn_->setEnabled(true);

    const MipPyramid* const pyramids[3] = {&colorMapData_, &reliefMapData_, &normalMapData_};
    for (int i = 0; i < 3; i++)
    {
        int levels = pyramids[i]->levelCount();
        tpMipSpin_[i]->setEnabled(levels > 0);
        tpMipSpin_[i]->setRange(0, std::max(0, levels - 1));
        tpSaveBtn_[i]->setEnabled(levels > 0);
    }
    tpMipSpin_[3]->setEnabled(offsetMapData_.width > 0);
    tpMipSpin_[3]->setRange(0, 0);
    tpSaveBtn_[3]->setEnabled(offsetMapData_.width > 0);

    for (int i = 0; i < 4; i++)
        updatePreview(i);
}

void TexturePrepModule::onTpSave(int idx)
{
    QString path = QFileDialog::getSaveFileName(this, "Save Texture", "", "PNG Image (*.png)");
    if (path.isEmpty())
        return;

    QImage img;
    if (idx == 3)
    {
        img = offsetMapMaskImage();
    }
    else
    {
        const MipPyramid& pyr = idx == 0 ? colorMapData_ : idx == 1 ? reliefMapData_ : normalMapData_;
        if (!pyr.mips.empty())
            img = mipLevelToQImage(pyr.mips[0], pyr.width, pyr.height, pyr.channels, /*remapSigned=*/idx == 2);
    }

    if (img.isNull() || !img.save(path))
        QMessageBox::critical(this, "Error", "Failed to save image.");
}

// ─── Private methods ──────────────────────────────────────────────────────────

void TexturePrepModule::updateThumbnails()
{
    auto setThumb = [this](int idx, const QImage& img, const char* emptyText) {
        if (img.isNull())
        {
            tpThumb_[idx]->setPixmap(QPixmap());
            tpThumb_[idx]->setText(emptyText);
        }
        else
        {
            tpThumb_[idx]->setPixmap(QPixmap::fromImage(img)
                .scaled(tpThumb_[idx]->size(), Qt::KeepAspectRatio, Qt::SmoothTransformation));
        }
    };

    QImage colorImg, normalImg;
    if (simplifiedMesh_)
    {
        colorImg  = rgbaTextureToQImage(simplifiedMesh_->textureData,
                                        simplifiedMesh_->textureWidth, simplifiedMesh_->textureHeight);
        normalImg = rgbaTextureToQImage(simplifiedMesh_->normalTextureData,
                                        simplifiedMesh_->normalTextureWidth, simplifiedMesh_->normalTextureHeight);
    }
    setThumb(0, colorImg,  "(none)");
    setThumb(2, normalImg, "(none)");

    QImage depthImg;
    if (hmResult_.valid && !hmResult_.image.empty())
    {
        depthImg = QImage(hmResult_.image.data(), hmResult_.width, hmResult_.height,
                          hmResult_.width, QImage::Format_Grayscale8);
    }
    setThumb(1, depthImg, "(not baked)");
}

void TexturePrepModule::updateGenerateEnabled()
{
    bool hasMesh   = simplifiedMesh_ && simplifiedMesh_->faceCount() > 0;
    bool hasColor  = simplifiedMesh_ && !simplifiedMesh_->textureData.empty();
    bool hasNormal = simplifiedMesh_ && !simplifiedMesh_->normalTextureData.empty();
    bool hasDepth  = hmResult_.valid && !hmResult_.image.empty();
    tpGenerateBtn_->setEnabled(hasMesh && hasColor && hasNormal && hasDepth);
}

void TexturePrepModule::updatePreview(int idx)
{
    QImage img;
    QString info = "—";

    if (idx == 3)
    {
        if (offsetMapData_.width > 0)
        {
            img  = offsetMapMaskImage();
            info = QString("%1 × %2").arg(offsetMapData_.width).arg(offsetMapData_.height);
        }
    }
    else
    {
        const MipPyramid& pyr = idx == 0 ? colorMapData_ : idx == 1 ? reliefMapData_ : normalMapData_;
        if (!pyr.mips.empty())
        {
            int mip = std::clamp(tpMipSpin_[idx]->value(), 0, pyr.levelCount() - 1);
            int w = std::max(1, pyr.width  >> mip);
            int h = std::max(1, pyr.height >> mip);
            bool showChannels[4] = {
                tpChannelCheck_[idx][0]->isChecked(), tpChannelCheck_[idx][1]->isChecked(),
                tpChannelCheck_[idx][2]->isChecked(), tpChannelCheck_[idx][3]->isChecked()
            };
            img  = mipLevelToQImage(pyr.mips[mip], w, h, pyr.channels, /*remapSigned=*/idx == 2, showChannels);
            info = QString("%1 × %2  ·  mip %3/%4").arg(w).arg(h).arg(mip).arg(pyr.levelCount() - 1);
        }
    }

    if (img.isNull())
    {
        tpPreview_[idx]->setText("(not generated)");
        tpPreview_[idx]->setPixmap(QPixmap());
    }
    else
    {
        tpPreview_[idx]->setPixmap(QPixmap::fromImage(img)
            .scaled(tpPreview_[idx]->size(), Qt::KeepAspectRatio, Qt::SmoothTransformation));
    }
    tpInfoLabel_[idx]->setText(info);
}

QImage TexturePrepModule::mipLevelToQImage(const std::vector<float>& data, int w, int h,
                                            int channels, bool remapSigned,
                                            const bool* showChannels) const
{
    static const bool kAllShown[4] = {true, true, true, true};
    if (!showChannels)
        showChannels = kAllShown;

    QImage::Format fmt = (channels == 3) ? QImage::Format_RGB888 : QImage::Format_RGBA8888;
    QImage img(w, h, fmt);
    auto remap = [&](float v) { return remapSigned ? v * 0.5f + 0.5f : v; };
    for (int y = 0; y < h; y++)
    {
        for (int x = 0; x < w; x++)
        {
            size_t i = ((size_t)y * w + x) * channels;
            float r = showChannels[0] ? std::clamp(remap(data[i + 0]), 0.0f, 1.0f) : 0.0f;
            float g = showChannels[1] ? std::clamp(remap(data[i + 1]), 0.0f, 1.0f) : 0.0f;
            float b = showChannels[2] ? std::clamp(remap(data[i + 2]), 0.0f, 1.0f) : 0.0f;
            float a = (channels == 4) ? (showChannels[3] ? std::clamp(remap(data[i + 3]), 0.0f, 1.0f) : 1.0f) : 1.0f;
            img.setPixelColor(x, y, QColor::fromRgbF(r, g, b, a));
        }
    }
    return img;
}

QImage TexturePrepModule::offsetMapMaskImage() const
{
    if (offsetMapData_.width <= 0 || offsetMapData_.height <= 0)
        return QImage();

    QImage img(offsetMapData_.width, offsetMapData_.height, QImage::Format_Grayscale8);
    for (int y = 0; y < offsetMapData_.height; y++)
    {
        for (int x = 0; x < offsetMapData_.width; x++)
        {
            size_t i = ((size_t)y * offsetMapData_.width + x) * 4;
            int v = offsetMapData_.mips[0][i + 3] > 0.f ? 255 : 0;
            img.setPixelColor(x, y, QColor(v, v, v));
        }
    }
    return img;
}
