/**
 * @file textures_module.cpp
 * @brief TexturesModule implementation: map/channel/mip selection and the
 *        zoomable image viewer for the original mesh's textures.
 */
#include "gui/modules/textures_module.h"
#include <QApplication>
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <limits>
#include <QEvent>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QPixmap>
#include <QShowEvent>
#include <QTimer>
#include <QVBoxLayout>
#include <QWheelEvent>

namespace {

/// Selectable side lengths, in texels, of the baked planes maps.
constexpr int kBakeResolutions[] = {128, 256, 512, 1024, 2048};
/// Index into kBakeResolutions selected by default.
constexpr int kDefaultBakeResolution = 2;

}  // namespace

// ─── Public slots ─────────────────────────────────────────────────────────────

void TexturesModule::onMeshLoaded(mesh::Mesh* original, mesh::Mesh* simplified) {
    Module::onMeshLoaded(original, simplified);
    if (bakeBtn_)
        bakeBtn_->setEnabled(originalMesh_ != nullptr);
    rebuildMaps();
}

void TexturesModule::onMeshUpdated() {
    rebuildMaps();
}

// ─── UI construction ──────────────────────────────────────────────────────────

QWidget* TexturesModule::buildContent() {
    scene_ = new QGraphicsScene(this);
    pixmapItem_ = scene_->addPixmap(QPixmap());
    // Zoomed-in texels stay crisp, so tiny mip levels remain readable.
    pixmapItem_->setTransformationMode(Qt::FastTransformation);

    view_ = new QGraphicsView(scene_);
    view_->setBackgroundBrush(QColor(0x1e, 0x1e, 0x1e));
    view_->setFrameShape(QFrame::NoFrame);
    view_->setDragMode(QGraphicsView::ScrollHandDrag);
    view_->setTransformationAnchor(QGraphicsView::AnchorUnderMouse);
    view_->setResizeAnchor(QGraphicsView::AnchorViewCenter);
    view_->viewport()->installEventFilter(this);
    return view_;
}

QWidget* TexturesModule::buildControls() {
    auto* outer = new QWidget();
    auto* layout = new QVBoxLayout(outer);

    auto* bakeRow = new QHBoxLayout();
    bakeRow->addWidget(new QLabel("Resolution:"));
    bakeResCombo_ = new QComboBox();
    for (int res : kBakeResolutions)
        bakeResCombo_->addItem(QString("%1 x %1").arg(res), res);
    bakeResCombo_->setCurrentIndex(kDefaultBakeResolution);
    bakeRow->addWidget(bakeResCombo_, 1);
    layout->addLayout(bakeRow);

    bakeBtn_ = new QPushButton("Bake");
    bakeBtn_->setMinimumHeight(40);
    bakeBtn_->setEnabled(originalMesh_ != nullptr);
    connect(bakeBtn_, &QPushButton::clicked, this, &TexturesModule::onBake);
    layout->addWidget(bakeBtn_);

    auto* group = new QGroupBox("Texture");
    auto* form = new QVBoxLayout(group);

    form->addWidget(new QLabel("Map:"));
    mapCombo_ = new QComboBox();
    connect(mapCombo_, QOverload<int>::of(&QComboBox::currentIndexChanged), this,
            &TexturesModule::onMapChanged);
    form->addWidget(mapCombo_);

    form->addWidget(new QLabel("Channel:"));
    channelCombo_ = new QComboBox();
    connect(channelCombo_, QOverload<int>::of(&QComboBox::currentIndexChanged), this,
            &TexturesModule::refreshImage);
    form->addWidget(channelCombo_);

    form->addWidget(new QLabel("Mip level:"));
    mipSlider_ = new QSlider(Qt::Horizontal);
    mipSlider_->setRange(0, 0);
    connect(mipSlider_, &QSlider::valueChanged, this, &TexturesModule::refreshImage);
    form->addWidget(mipSlider_);
    mipLabel_ = new QLabel();
    form->addWidget(mipLabel_);

    stretchCheck_ = new QCheckBox("Stretch contrast");
    connect(stretchCheck_, &QCheckBox::toggled, this, &TexturesModule::refreshImage);
    form->addWidget(stretchCheck_);

    auto* fitBtn = new QPushButton("Fit to view");
    connect(fitBtn, &QPushButton::clicked, this, &TexturesModule::fitView);
    form->addWidget(fitBtn);

    layout->addWidget(group);

    infoLabel_ = new QLabel();
    infoLabel_->setWordWrap(true);
    infoLabel_->setStyleSheet("color:#aaa; font-size:11px;");
    layout->addWidget(infoLabel_);
    layout->addStretch();

    rebuildMaps();
    return outer;
}

// ─── Slots ────────────────────────────────────────────────────────────────────

void TexturesModule::onBake() {
    if (!originalMesh_)
        return;

    QApplication::setOverrideCursor(Qt::WaitCursor);
    originalMesh_->bake(bakeResCombo_->currentData().toInt());
    QApplication::restoreOverrideCursor();

    // The original mesh now carries the planes maps; this also refreshes our own list.
    emit notifyMeshUpdate();
    emit statusMessage("Baked planes maps");
}

// ─── Events ───────────────────────────────────────────────────────────────────

bool TexturesModule::eventFilter(QObject* watched, QEvent* event) {
    if (view_ && watched == view_->viewport()) {
        if (event->type() == QEvent::Wheel) {
            auto* wheel = static_cast<QWheelEvent*>(event);
            double factor = std::pow(1.0015, wheel->angleDelta().y());
            view_->scale(factor, factor);
            return true;
        }
        if (event->type() == QEvent::MouseButtonDblClick) {
            fitView();
            return true;
        }
    }
    return Module::eventFilter(watched, event);
}

void TexturesModule::showEvent(QShowEvent* event) {
    Module::showEvent(event);
    if (fitPending_)
        QTimer::singleShot(0, this, &TexturesModule::fitView);
}

// ─── Map list ─────────────────────────────────────────────────────────────────

void TexturesModule::rebuildMaps() {
    if (!mapCombo_)
        return;

    QString prevLabel = mapCombo_->currentText();
    int prevChannel = channelCombo_->currentIndex();
    int prevMip = mipSlider_->value();

    maps_.clear();

    // RGB, then the single channels.
    auto standardViews = [] {
        std::vector<ChannelView> v;
        v.push_back({"RGB", {0, 1, 2}, false});
        v.push_back({"R", {0, -1, -1}, true});
        v.push_back({"G", {1, -1, -1}, true});
        v.push_back({"B", {2, -1, -1}, true});
        v.push_back({"A", {3, -1, -1}, true});
        return v;
    };
    auto planeViews = [] {
        std::vector<ChannelView> v;
        v.push_back({"UV (RG)", {0, 1, -1}, false});
        v.push_back({"U (R)", {0, -1, -1}, true});
        v.push_back({"V (G)", {1, -1, -1}, true});
        v.push_back({"Depth (B)", {2, -1, -1}, true});
        v.push_back({"Mask (A)", {3, -1, -1}, true});
        return v;
    };

    auto add8 = [&](const QString& label, const mesh::Texture& t) {
        if (t.data.empty() || t.width <= 0 || t.height <= 0)
            return;
        MapSource m;
        m.label = label;
        m.data8 = t.data.data();
        m.width = t.width;
        m.height = t.height;
        m.views = standardViews();
        maps_.push_back(std::move(m));
    };
    auto add16 = [&](const QString& label, const mesh::Texture16& t) {
        if (t.data.empty() || t.width <= 0 || t.height <= 0)
            return;
        MapSource m;
        m.label = label;
        m.data16 = t.data.data();
        m.width = t.width;
        m.height = t.height;
        m.views = planeViews();
        maps_.push_back(std::move(m));
    };

    if (originalMesh_) {
        const mesh::Mesh& mesh = *originalMesh_;
        add8("Color", mesh.colorTexture());
        add8("Normal", mesh.normalTexture());
        add8("Height", mesh.heightTexture());
        static const char* const kPlaneLabels[6] = {"X+ plane", "X- plane", "Y+ plane",
                                                    "Y- plane", "Z+ plane", "Z- plane"};
        for (int i = 0; i < 6; i++)
            add16(kPlaneLabels[i], mesh.planes()[i]);
    }

    mapCombo_->blockSignals(true);
    mapCombo_->clear();
    int sel = 0;
    for (int i = 0; i < (int)maps_.size(); i++) {
        mapCombo_->addItem(maps_[i].label);
        if (maps_[i].label == prevLabel)
            sel = i;
    }
    mapCombo_->setCurrentIndex(maps_.empty() ? -1 : sel);
    mapCombo_->blockSignals(false);

    onMapChanged();

    // Restore the previous channel / mip when the same map is still selected.
    if (!maps_.empty() && maps_[sel].label == prevLabel) {
        if (prevChannel >= 0 && prevChannel < channelCombo_->count())
            channelCombo_->setCurrentIndex(prevChannel);
        mipSlider_->setValue(std::min(prevMip, mipSlider_->maximum()));
    }
}

void TexturesModule::onMapChanged() {
    int idx = mapCombo_->currentIndex();

    channelCombo_->blockSignals(true);
    channelCombo_->clear();
    mipSlider_->blockSignals(true);

    if (idx < 0 || idx >= (int)maps_.size()) {
        mipSlider_->setRange(0, 0);
        mipSlider_->setEnabled(false);
        channelCombo_->setEnabled(false);
        channelCombo_->blockSignals(false);
        mipSlider_->blockSignals(false);
        mipLabel_->clear();
        infoLabel_->setText(originalMesh_ ? "The loaded mesh has no textures."
                                          : "No mesh loaded.");
        pixmapItem_->setPixmap(QPixmap());
        scene_->setSceneRect(QRectF());
        return;
    }

    const MapSource& m = maps_[idx];
    for (const ChannelView& v : m.views)
        channelCombo_->addItem(v.name);
    channelCombo_->setEnabled(true);
    channelCombo_->setCurrentIndex(0);

    int levels = levelCount(m);
    mipSlider_->setRange(0, levels - 1);
    mipSlider_->setValue(0);
    mipSlider_->setEnabled(levels > 1);

    channelCombo_->blockSignals(false);
    mipSlider_->blockSignals(false);

    scene_->setSceneRect(0, 0, m.width, m.height);
    refreshImage();
    fitView();
}

// ─── Rendering ────────────────────────────────────────────────────────────────

int TexturesModule::levelCount(const MapSource& m) {
    int levels = 1;
    for (int s = std::max(m.width, m.height); s > 1; s >>= 1)
        levels++;
    return levels;
}

std::vector<float> TexturesModule::buildLevel(const MapSource& m, int level, int& outW,
                                              int& outH) {
    outW = std::max(1, m.width >> level);
    outH = std::max(1, m.height >> level);
    const float scale = m.data8 ? 1.f / 255.f : 1.f / 65535.f;

    std::vector<float> out((size_t)outW * outH * 4);
    for (int y = 0; y < outH; y++) {
        int y0 = (int)((int64_t)y * m.height / outH);
        int y1 = std::max(y0 + 1, (int)((int64_t)(y + 1) * m.height / outH));
        for (int x = 0; x < outW; x++) {
            int x0 = (int)((int64_t)x * m.width / outW);
            int x1 = std::max(x0 + 1, (int)((int64_t)(x + 1) * m.width / outW));
            double acc[4] = {0, 0, 0, 0};
            for (int sy = y0; sy < y1; sy++) {
                for (int sx = x0; sx < x1; sx++) {
                    size_t i = ((size_t)sy * m.width + sx) * 4;
                    for (int c = 0; c < 4; c++)
                        acc[c] += m.data8 ? m.data8[i + c] : m.data16[i + c];
                }
            }
            double n = (double)(y1 - y0) * (x1 - x0);
            size_t o = ((size_t)y * outW + x) * 4;
            for (int c = 0; c < 4; c++)
                out[o + c] = (float)(acc[c] / n) * scale;
        }
    }
    return out;
}

void TexturesModule::refreshImage() {
    int idx = mapCombo_->currentIndex();
    if (idx < 0 || idx >= (int)maps_.size())
        return;
    const MapSource& m = maps_[idx];
    int viewIdx = std::clamp(channelCombo_->currentIndex(), 0, (int)m.views.size() - 1);
    const ChannelView& view = m.views[viewIdx];
    int level = std::clamp(mipSlider_->value(), 0, levelCount(m) - 1);

    int w = 0, h = 0;
    std::vector<float> data = buildLevel(m, level, w, h);
    mipLabel_->setText(QString("%1 / %2  (%3x%4)").arg(level).arg(mipSlider_->maximum()).arg(w).arg(h));

    const int outChannels = view.grey ? 1 : 3;
    float lo[3] = {0, 0, 0};
    float hi[3] = {1, 1, 1};
    if (stretchCheck_->isChecked()) {
        for (int k = 0; k < outChannels; k++) {
            lo[k] = std::numeric_limits<float>::max();
            hi[k] = std::numeric_limits<float>::lowest();
            if (view.source[k] < 0) {
                lo[k] = hi[k] = 0.f;
                continue;
            }
            for (size_t i = 0; i < (size_t)w * h; i++) {
                float v = data[i * 4 + view.source[k]];
                lo[k] = std::min(lo[k], v);
                hi[k] = std::max(hi[k], v);
            }
        }
    }

    QImage img(w, h, QImage::Format_RGB888);
    for (int y = 0; y < h; y++) {
        uchar* row = img.scanLine(y);
        for (int x = 0; x < w; x++) {
            size_t i = ((size_t)y * w + x) * 4;
            uchar rgb[3] = {0, 0, 0};
            for (int k = 0; k < outChannels; k++) {
                if (view.source[k] < 0)
                    continue;
                float v = data[i + view.source[k]];
                float range = hi[k] - lo[k];
                float n = (range > 1e-8f) ? (v - lo[k]) / range : std::clamp(v, 0.f, 1.f);
                rgb[k] = (uchar)std::clamp((int)std::lround(n * 255.f), 0, 255);
            }
            if (view.grey)
                rgb[1] = rgb[2] = rgb[0];
            row[x * 3 + 0] = rgb[0];
            row[x * 3 + 1] = rgb[1];
            row[x * 3 + 2] = rgb[2];
        }
    }

    // Keep the on-screen size constant across mip levels: a coarser level is drawn
    // scaled up to the base level's extent.
    pixmapItem_->setPixmap(QPixmap::fromImage(img));
    pixmapItem_->setScale((double)m.width / w);

    infoLabel_->setText(QString("%1 — %2x%3 base, %4-bit")
                            .arg(m.label).arg(m.width).arg(m.height).arg(m.data8 ? 8 : 16));
}

void TexturesModule::fitView() {
    if (!view_)
        return;
    if (!view_->isVisible()) {
        fitPending_ = true;
        return;
    }
    fitPending_ = false;
    if (!scene_->sceneRect().isEmpty())
        view_->fitInView(scene_->sceneRect(), Qt::KeepAspectRatio);
}
