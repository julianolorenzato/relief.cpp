#include "gui/texture_inspector_dialog.h"
#include <algorithm>
#include <limits>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QComboBox>
#include <QSlider>
#include <QLabel>
#include <QScrollArea>

TextureInspectorDialog::TextureInspectorDialog(const MipPyramid *colorMap,
                                                const MipPyramid *reliefMap,
                                                const MipPyramid *normalMap,
                                                const OffsetMapResult *offsetMap,
                                                QWidget *parent)
    : QDialog(parent),
      colorMap_(colorMap), reliefMap_(reliefMap), normalMap_(normalMap), offsetMap_(offsetMap)
{
    setWindowTitle("Texture Inspector");
    resize(640, 720);

    rebuildMapList();

    QVBoxLayout *layout = new QVBoxLayout(this);

    QHBoxLayout *mapRow = new QHBoxLayout();
    mapRow->addWidget(new QLabel("Map:"));
    this->mapCombo = new QComboBox();
    for (const auto &m : this->maps)
        this->mapCombo->addItem(m.label);
    connect(this->mapCombo, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &TextureInspectorDialog::onMapChanged);
    mapRow->addWidget(this->mapCombo, 1);
    layout->addLayout(mapRow);

    QHBoxLayout *channelRow = new QHBoxLayout();
    channelRow->addWidget(new QLabel("Channel:"));
    this->channelCombo = new QComboBox();
    connect(this->channelCombo, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &TextureInspectorDialog::onChannelChanged);
    channelRow->addWidget(this->channelCombo, 1);
    layout->addLayout(channelRow);

    QHBoxLayout *mipRow = new QHBoxLayout();
    mipRow->addWidget(new QLabel("Mip Level:"));
    this->mipSlider = new QSlider(Qt::Horizontal);
    connect(this->mipSlider, &QSlider::valueChanged, this, &TextureInspectorDialog::onMipChanged);
    mipRow->addWidget(this->mipSlider, 1);
    this->mipLbl = new QLabel();
    this->mipLbl->setMinimumWidth(110);
    mipRow->addWidget(this->mipLbl);
    layout->addLayout(mipRow);

    QScrollArea *scroll = new QScrollArea();
    scroll->setWidgetResizable(true);
    scroll->setAlignment(Qt::AlignCenter);
    this->imageLbl = new QLabel();
    this->imageLbl->setAlignment(Qt::AlignCenter);
    this->imageLbl->setStyleSheet("background-color:#1e1e1e;");
    scroll->setWidget(this->imageLbl);
    layout->addWidget(scroll, 1);

    this->rangeLbl = new QLabel();
    this->rangeLbl->setStyleSheet("color:#aaa; font-size:11px;");
    layout->addWidget(this->rangeLbl);

    if (!this->maps.empty())
        onMapChanged(0);
}

void TextureInspectorDialog::rebuildMapList()
{
    this->maps.clear();

    if (this->colorMap_ && this->colorMap_->levelCount() > 0)
    {
        MapInfo m;
        m.label = "Color";
        m.externalMips = &this->colorMap_->mips;
        m.width = this->colorMap_->width;
        m.height = this->colorMap_->height;
        m.channels = this->colorMap_->channels;
        m.channelNames = {"RGB (combined)", "R", "G", "B", "A"};
        this->maps.push_back(std::move(m));
    }

    if (this->reliefMap_ && this->reliefMap_->levelCount() > 0)
    {
        MapInfo m;
        m.label = "Relief (Depth Min/Max/Seam)";
        m.externalMips = &this->reliefMap_->mips;
        m.width = this->reliefMap_->width;
        m.height = this->reliefMap_->height;
        m.channels = this->reliefMap_->channels;
        m.channelNames = {"RGB (combined)", "Min Depth", "Max Depth", "Seam Mask", "Unused"};
        this->maps.push_back(std::move(m));
    }

    if (this->normalMap_ && this->normalMap_->levelCount() > 0)
    {
        MapInfo m;
        m.label = "Normal";
        m.externalMips = &this->normalMap_->mips;
        m.width = this->normalMap_->width;
        m.height = this->normalMap_->height;
        m.channels = this->normalMap_->channels;
        m.channelNames = {"RGB (XYZ)", "X", "Y", "Z"};
        this->maps.push_back(std::move(m));
    }

    if (this->offsetMap_ && this->offsetMap_->width > 0 && this->offsetMap_->height > 0)
    {
        MapInfo m;
        m.label = "Offset (Atlas Leap)";
        m.localMips.push_back(this->offsetMap_->data); // single-level "pyramid"
        m.useLocalMips = true;
        m.width = this->offsetMap_->width;
        m.height = this->offsetMap_->height;
        m.channels = 4;
        m.channelNames = {"RGB (combined)", "Leap U", "Leap V", "Rotation", "Validity"};
        this->maps.push_back(std::move(m));
    }
}

void TextureInspectorDialog::onMapChanged(int)
{
    refreshChannelCombo();
}

void TextureInspectorDialog::refreshChannelCombo()
{
    int idx = this->mapCombo->currentIndex();
    if (idx < 0 || idx >= (int)this->maps.size())
        return;

    this->channelCombo->blockSignals(true);
    this->channelCombo->clear();
    this->channelCombo->addItems(this->maps[idx].channelNames);
    this->channelCombo->blockSignals(false);

    const auto &m = this->maps[idx];
    int levels = (int)m.mips().size();
    this->mipSlider->blockSignals(true);
    this->mipSlider->setRange(0, std::max(0, levels - 1));
    this->mipSlider->setValue(0);
    this->mipSlider->setEnabled(levels > 1);
    this->mipSlider->blockSignals(false);

    refreshImage();
}

void TextureInspectorDialog::onChannelChanged(int)
{
    refreshImage();
}

void TextureInspectorDialog::onMipChanged(int)
{
    refreshImage();
}

QImage TextureInspectorDialog::renderChannel(int channelIndex) const
{
    int mapIdx = this->mapCombo->currentIndex();
    if (mapIdx < 0 || mapIdx >= (int)this->maps.size())
        return QImage();

    const MapInfo &m = this->maps[mapIdx];
    int lvl = std::clamp(this->mipSlider->value(), 0, (int)m.mips().size() - 1);
    const std::vector<float> &data = m.mips()[lvl];
    int w = std::max(1, m.width >> lvl);
    int h = std::max(1, m.height >> lvl);
    int c = m.channels;

    // channelIndex: 0 = combined RGB (first up to 3 channels), 1..c = single channel (index-1).
    int wantChannels = (channelIndex == 0) ? std::min(3, c) : 1;
    int channelOffset = (channelIndex == 0) ? 0 : channelIndex - 1;

    float lo[3] = {std::numeric_limits<float>::max(), std::numeric_limits<float>::max(), std::numeric_limits<float>::max()};
    float hi[3] = {std::numeric_limits<float>::lowest(), std::numeric_limits<float>::lowest(), std::numeric_limits<float>::lowest()};
    for (size_t i = 0; i < (size_t)w * h; i++)
    {
        for (int k = 0; k < wantChannels; k++)
        {
            int ch = (channelIndex == 0) ? k : channelOffset;
            float v = data[i * c + ch];
            lo[k] = std::min(lo[k], v);
            hi[k] = std::max(hi[k], v);
        }
    }

    QString rangeText;
    if (wantChannels == 1)
        rangeText = QString("Level %1 — %2x%3 — range [%4, %5]").arg(lvl).arg(w).arg(h).arg(lo[0], 0, 'f', 4).arg(hi[0], 0, 'f', 4);
    else
        rangeText = QString("Level %1 — %2x%3 — R[%4, %5] G[%6, %7] B[%8, %9]")
                        .arg(lvl).arg(w).arg(h)
                        .arg(lo[0], 0, 'f', 3).arg(hi[0], 0, 'f', 3)
                        .arg(lo[1], 0, 'f', 3).arg(hi[1], 0, 'f', 3)
                        .arg(lo[2], 0, 'f', 3).arg(hi[2], 0, 'f', 3);
    this->rangeLbl->setText(rangeText);

    QImage img(w, h, QImage::Format_RGB888);
    for (int y = 0; y < h; y++)
    {
        uchar *row = img.scanLine(y);
        for (int x = 0; x < w; x++)
        {
            size_t i = (size_t)y * w + x;
            uchar rgb[3] = {0, 0, 0};
            for (int k = 0; k < wantChannels; k++)
            {
                int ch = (channelIndex == 0) ? k : channelOffset;
                float v = data[i * c + ch];
                float range = hi[k] - lo[k];
                float n = (range > 1e-8f) ? (v - lo[k]) / range : 0.f;
                rgb[k] = (uchar)std::clamp((int)std::lround(n * 255.f), 0, 255);
            }
            if (wantChannels == 1)
                rgb[1] = rgb[2] = rgb[0];
            row[x * 3 + 0] = rgb[0];
            row[x * 3 + 1] = rgb[1];
            row[x * 3 + 2] = rgb[2];
        }
    }
    return img;
}

void TextureInspectorDialog::refreshImage()
{
    int mapIdx = this->mapCombo->currentIndex();
    if (mapIdx < 0 || mapIdx >= (int)this->maps.size())
        return;

    int lvl = this->mipSlider->value();
    int w = std::max(1, this->maps[mapIdx].width >> lvl);
    int h = std::max(1, this->maps[mapIdx].height >> lvl);
    this->mipLbl->setText(QString("%1 / %2  (%3x%4)").arg(lvl).arg(this->mipSlider->maximum()).arg(w).arg(h));

    QImage img = renderChannel(this->channelCombo->currentIndex());
    if (img.isNull())
        return;

    // Low mip levels are tiny (down to 1x1); upscale with nearest-neighbor so they're visible.
    constexpr int kMinDisplay = 400;
    QImage disp = img;
    if (img.width() < kMinDisplay && img.height() < kMinDisplay)
    {
        int factor = std::max(1, kMinDisplay / std::max(img.width(), img.height()));
        disp = img.scaled(img.width() * factor, img.height() * factor, Qt::KeepAspectRatio, Qt::FastTransformation);
    }
    this->imageLbl->setPixmap(QPixmap::fromImage(disp));
    this->imageLbl->resize(disp.size());
}
