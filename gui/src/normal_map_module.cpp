/**
 * @file normal_map_module.cpp
 * @brief NormalMapModule implementation: loads a height image, derives a
 *        normal-map mip pyramid from it, and previews/saves the result.
 */
#include "gui/normal_map_module.h"

#include <QFileDialog>
#include <QFileInfo>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QMessageBox>
#include <QPixmap>
#include <QScrollArea>
#include <QSplitter>
#include <QVBoxLayout>
#include <algorithm>

using namespace textures;

// ─── Static helpers ──────────────────────────────────────────────────────────

/// Converts one mip level's raw float data into a displayable QImage.
/// Signed [-1,1] components are remapped to [0,1]. `showChannels` optionally
/// masks out individual X/Y/Z channels.
/// (Deliberately mirrors TexturePrepModule::mipLevelToQImage, which is private
/// to that widget; duplicated here rather than refactoring the pipeline module.)
static QImage mipLevelToQImage(const std::vector<float> &data, int w, int h,
                               int channels, const bool *showChannels) {
    QImage img(w, h, QImage::Format_RGB888);
    for (int y = 0; y < h; y++) {
        for (int x = 0; x < w; x++) {
            size_t i = ((size_t)y * w + x) * channels;
            auto chan = [&](int c) {
                return showChannels[c]
                           ? std::clamp(data[i + c] * 0.5f + 0.5f, 0.0f, 1.0f)
                           : 0.0f;
            };
            img.setPixelColor(x, y, QColor::fromRgbF(chan(0), chan(1), chan(2)));
        }
    }
    return img;
}

// ─── Constructor ─────────────────────────────────────────────────────────────

NormalMapModule::NormalMapModule(QWidget *parent) : QWidget(parent) {
    buildUI();
}

// ─── buildUI ─────────────────────────────────────────────────────────────────

void NormalMapModule::buildUI() {
    QHBoxLayout *outerLayout = new QHBoxLayout(this);
    outerLayout->setContentsMargins(0, 0, 0, 0);

    QSplitter *splitter = new QSplitter(Qt::Horizontal, this);
    outerLayout->addWidget(splitter);

    // ── Left: source + result preview panels ─────────────────────────────
    QWidget *panelsWidget = new QWidget();
    QHBoxLayout *panelsLayout = new QHBoxLayout(panelsWidget);
    panelsLayout->setSpacing(12);

    auto makePreviewLabel = [](const char *emptyText) {
        QLabel *lbl = new QLabel(emptyText);
        lbl->setAlignment(Qt::AlignCenter);
        lbl->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
        lbl->setMinimumSize(180, 180);
        lbl->setStyleSheet("background-color:#1e1e1e;border:1px solid #555;");
        return lbl;
    };

    QGroupBox *srcPanel = new QGroupBox("Source Height Map");
    QVBoxLayout *srcLayout = new QVBoxLayout(srcPanel);
    srcPanel->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    this->heightPreview = makePreviewLabel("(no image loaded)");
    srcLayout->addWidget(this->heightPreview, 1);
    panelsLayout->addWidget(srcPanel);

    QGroupBox *outPanel = new QGroupBox("Generated Normal Map");
    QVBoxLayout *outLayout = new QVBoxLayout(outPanel);
    outPanel->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    this->normalPreview = makePreviewLabel("(not generated)");
    outLayout->addWidget(this->normalPreview, 1);

    this->normalInfoLbl = new QLabel("—");
    this->normalInfoLbl->setFixedHeight(18);
    this->normalInfoLbl->setAlignment(Qt::AlignCenter);
    outLayout->addWidget(this->normalInfoLbl);

    static const char *chanLabel[3] = {"R", "G", "B"};
    static const char *chanTooltip[3] = {"X", "Y", "Z"};
    QHBoxLayout *chanRow = new QHBoxLayout();
    chanRow->addWidget(new QLabel("Channels:"));
    for (int c = 0; c < 3; c++) {
        this->channelCheck[c] = new QCheckBox(chanLabel[c]);
        this->channelCheck[c]->setChecked(true);
        this->channelCheck[c]->setToolTip(chanTooltip[c]);
        connect(this->channelCheck[c], &QCheckBox::toggled, this,
                [this](bool) { updatePreview(); });
        chanRow->addWidget(this->channelCheck[c]);
    }
    chanRow->addStretch();
    outLayout->addLayout(chanRow);

    QHBoxLayout *btnRow = new QHBoxLayout();
    btnRow->addWidget(new QLabel("Mip:"));
    this->mipSpin = new QSpinBox();
    this->mipSpin->setRange(0, 0);
    this->mipSpin->setEnabled(false);
    connect(this->mipSpin, QOverload<int>::of(&QSpinBox::valueChanged), this,
            [this](int) { updatePreview(); });
    btnRow->addWidget(this->mipSpin);

    this->saveBtn = new QPushButton("Save");
    this->saveBtn->setEnabled(false);
    connect(this->saveBtn, &QPushButton::clicked, this,
            &NormalMapModule::onSave);
    btnRow->addWidget(this->saveBtn);

    outLayout->addLayout(btnRow);
    panelsLayout->addWidget(outPanel);

    panelsWidget->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    splitter->addWidget(panelsWidget);

    // ── Right: controls in a QScrollArea ─────────────────────────────────
    QWidget *controlsWidget = new QWidget();
    QVBoxLayout *mainLayout = new QVBoxLayout(controlsWidget);

    QGroupBox *ctrlGroup = new QGroupBox("Input && Generation Controls");
    QVBoxLayout *ctrlOuter = new QVBoxLayout(ctrlGroup);

    QHBoxLayout *loadRow = new QHBoxLayout();
    this->heightThumb = new QLabel("—");
    this->heightThumb->setFixedSize(56, 56);
    this->heightThumb->setAlignment(Qt::AlignCenter);
    this->heightThumb->setStyleSheet(
        "background-color:#1e1e1e;border:1px solid #555;");
    loadRow->addWidget(this->heightThumb);

    QPushButton *loadBtn = new QPushButton("Load Height Image...");
    connect(loadBtn, &QPushButton::clicked, this,
            &NormalMapModule::onLoadHeight);
    loadRow->addWidget(loadBtn, 1);
    ctrlOuter->addLayout(loadRow);

    QHBoxLayout *resRow = new QHBoxLayout();
    resRow->addWidget(new QLabel("Resolution:"));
    this->resCombo = new QComboBox();
    this->resCombo->addItem("128 × 128", 128);
    this->resCombo->addItem("256 × 256", 256);
    this->resCombo->addItem("512 × 512", 512);
    this->resCombo->addItem("1024 × 1024", 1024);
    this->resCombo->addItem("2048 × 2048", 2048);
    this->resCombo->setCurrentIndex(2);
    resRow->addWidget(this->resCombo, 1);
    ctrlOuter->addLayout(resRow);

    this->generateBtn = new QPushButton("Generate");
    this->generateBtn->setEnabled(false);
    connect(this->generateBtn, &QPushButton::clicked, this,
            &NormalMapModule::onGenerate);
    ctrlOuter->addWidget(this->generateBtn);

    mainLayout->addWidget(ctrlGroup);
    mainLayout->addStretch();

    QScrollArea *scrollArea = new QScrollArea();
    scrollArea->setWidget(controlsWidget);
    scrollArea->setWidgetResizable(true);
    scrollArea->setMinimumWidth(220);
    scrollArea->setMaximumWidth(360);
    scrollArea->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);

    splitter->addWidget(scrollArea);
}

// ─── Private slots ───────────────────────────────────────────────────────────

void NormalMapModule::onLoadHeight() {
    QString path = QFileDialog::getOpenFileName(
        this, "Open Height (Depth) Image", "",
        "Images (*.png *.jpg *.jpeg *.bmp *.tga);;All Files (*)");
    if (path.isEmpty()) return;

    QImage img;
    img.load(path);
    if (img.isNull()) {
        QMessageBox::critical(this, "Error", "Failed to load height image.");
        return;
    }
    this->heightImg = img;

    QPixmap pix = QPixmap::fromImage(this->heightImg);
    this->heightThumb->setPixmap(pix.scaled(this->heightThumb->size(),
                                            Qt::KeepAspectRatio,
                                            Qt::SmoothTransformation));
    this->heightPreview->setPixmap(pix.scaled(this->heightPreview->size(),
                                              Qt::KeepAspectRatio,
                                              Qt::SmoothTransformation));

    // Preselect the resolution entry nearest the source's next power of two.
    int srcRes = textures::nextPowerOfTwo(
        std::max(this->heightImg.width(), this->heightImg.height()));
    int bestIdx = 0;
    for (int i = 0; i < this->resCombo->count(); i++) {
        if (std::abs(this->resCombo->itemData(i).toInt() - srcRes) <
            std::abs(this->resCombo->itemData(bestIdx).toInt() - srcRes))
            bestIdx = i;
    }
    this->resCombo->setCurrentIndex(bestIdx);

    this->generateBtn->setEnabled(true);
    emit statusMessage("Loaded height image: " + QFileInfo(path).fileName());
}

void NormalMapModule::onGenerate() {
    if (this->heightImg.isNull()) return;

    int res = this->resCombo->currentData().toInt();

    QImage d = this->heightImg.convertToFormat(QImage::Format_Grayscale8);
    RawImage raw{d.constBits(), d.width(), d.height(), 1};
    this->normalMap = textures::buildNormalMapFromHeight(raw, res, res,
                                                         kStrength, kSmoothing);

    int levels = this->normalMap.levelCount();
    this->mipSpin->setEnabled(levels > 0);
    this->mipSpin->setRange(0, std::max(0, levels - 1));
    this->mipSpin->setValue(0);
    this->saveBtn->setEnabled(levels > 0);

    updatePreview();
    emit statusMessage(QString("Normal map generated (%1 × %1, %2 mips).")
                           .arg(res)
                           .arg(levels));
}

void NormalMapModule::onSave() {
    if (this->normalMap.mips.empty()) return;

    QString path = QFileDialog::getSaveFileName(this, "Save Normal Map", "",
                                                "PNG Image (*.png)");
    if (path.isEmpty()) return;

    static const bool kAllShown[3] = {true, true, true};
    QImage img = mipLevelToQImage(this->normalMap.mips[0], this->normalMap.width,
                                  this->normalMap.height,
                                  this->normalMap.channels, kAllShown);

    if (img.isNull() || !img.save(path))
        QMessageBox::critical(this, "Error", "Failed to save image.");
}

// ─── Private methods ─────────────────────────────────────────────────────────

void NormalMapModule::updatePreview() {
    if (this->normalMap.mips.empty()) {
        this->normalPreview->setText("(not generated)");
        this->normalPreview->setPixmap(QPixmap());
        this->normalInfoLbl->setText("—");
        return;
    }

    int mip = std::clamp(this->mipSpin->value(), 0,
                         this->normalMap.levelCount() - 1);
    int w = std::max(1, this->normalMap.width >> mip);
    int h = std::max(1, this->normalMap.height >> mip);
    bool showChannels[3] = {this->channelCheck[0]->isChecked(),
                            this->channelCheck[1]->isChecked(),
                            this->channelCheck[2]->isChecked()};

    QImage img = mipLevelToQImage(this->normalMap.mips[mip], w, h,
                                  this->normalMap.channels, showChannels);
    this->normalPreview->setPixmap(
        QPixmap::fromImage(img).scaled(this->normalPreview->size(),
                                       Qt::KeepAspectRatio,
                                       Qt::SmoothTransformation));
    this->normalInfoLbl->setText(QString("%1 × %2  ·  mip %3/%4")
                                     .arg(w)
                                     .arg(h)
                                     .arg(mip)
                                     .arg(this->normalMap.levelCount() - 1));
}
