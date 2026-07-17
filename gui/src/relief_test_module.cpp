#include "gui/relief_test_module.h"
#include "relief/uv_atlas.h"
#include <algorithm>
#include <cmath>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QGroupBox>
#include <QScrollArea>
#include <QSplitter>
#include <QFileDialog>
#include <QMessageBox>

// ─── Resample helpers (bilinear from RawImage → float mip0) ─────────────────

namespace {

void bilinearSampleF(const RawImage& img, double u, double v, float out[4]) {
    double x = u * img.width  - 0.5;
    double y = v * img.height - 0.5;
    int x0 = (int)std::floor(x), y0 = (int)std::floor(y);
    int x1 = x0 + 1, y1 = y0 + 1;
    double fx = x - x0, fy = y - y0;
    auto cx = [&](int xx) { return std::clamp(xx, 0, img.width  - 1); };
    auto cy = [&](int yy) { return std::clamp(yy, 0, img.height - 1); };
    int c = img.channels;
    const uint8_t* p00 = img.data + ((size_t)cy(y0) * img.width + cx(x0)) * c;
    const uint8_t* p10 = img.data + ((size_t)cy(y0) * img.width + cx(x1)) * c;
    const uint8_t* p01 = img.data + ((size_t)cy(y1) * img.width + cx(x0)) * c;
    const uint8_t* p11 = img.data + ((size_t)cy(y1) * img.width + cx(x1)) * c;
    for (int i = 0; i < c && i < 4; i++) {
        float v00 = p00[i] / 255.0f, v10 = p10[i] / 255.0f;
        float v01 = p01[i] / 255.0f, v11 = p11[i] / 255.0f;
        float top = v00 + (v10 - v00) * (float)fx;
        float bot = v01 + (v11 - v01) * (float)fx;
        out[i] = top + (bot - top) * (float)fy;
    }
    for (int i = c; i < 4; i++) out[i] = (i == 3) ? 1.0f : 0.0f;
}

std::vector<float> resampleColorRGBA(const RawImage& img, int outW, int outH) {
    std::vector<float> out((size_t)outW * outH * 4);
    float s[4];
    for (int y = 0; y < outH; y++)
        for (int x = 0; x < outW; x++) {
            bilinearSampleF(img, (x + 0.5) / outW, (y + 0.5) / outH, s);
            size_t idx = ((size_t)y * outW + x) * 4;
            out[idx+0] = s[0]; out[idx+1] = s[1]; out[idx+2] = s[2]; out[idx+3] = s[3];
        }
    return out;
}

std::vector<float> resampleDepthR(const RawImage& img, int outW, int outH) {
    std::vector<float> out((size_t)outW * outH);
    float s[4];
    for (int y = 0; y < outH; y++)
        for (int x = 0; x < outW; x++) {
            bilinearSampleF(img, (x + 0.5) / outW, (y + 0.5) / outH, s);
            out[(size_t)y * outW + x] = s[0];
        }
    return out;
}

std::vector<float> resampleNormalXYZ(const RawImage& img, int outW, int outH) {
    std::vector<float> out((size_t)outW * outH * 3);
    float s[4];
    for (int y = 0; y < outH; y++)
        for (int x = 0; x < outW; x++) {
            bilinearSampleF(img, (x + 0.5) / outW, (y + 0.5) / outH, s);
            float nx = s[0] * 2.f - 1.f, ny = s[1] * 2.f - 1.f, nz = s[2] * 2.f - 1.f;
            float len = std::sqrt(nx*nx + ny*ny + nz*nz);
            if (len > 1e-8f) { nx /= len; ny /= len; nz /= len; }
            else { nx = 0.f; ny = 0.f; nz = 1.f; }
            size_t idx = ((size_t)y * outW + x) * 3;
            out[idx+0] = nx; out[idx+1] = ny; out[idx+2] = nz;
        }
    return out;
}

} // namespace

// ─── Constructor ─────────────────────────────────────────────────────────────

ReliefTestModule::ReliefTestModule(QWidget *parent)
    : QWidget(parent)
{
    QHBoxLayout *outerLayout = new QHBoxLayout(this);
    outerLayout->setContentsMargins(0, 0, 0, 0);

    QSplitter *splitter = new QSplitter(Qt::Horizontal, this);
    outerLayout->addWidget(splitter);

    this->reliefView = new ReliefView();
    this->reliefView->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    splitter->addWidget(this->reliefView);

    QWidget *controls = buildControls();
    splitter->addWidget(controls);
}

QWidget *ReliefTestModule::buildControls()
{
    QWidget *controls = new QWidget();
    QVBoxLayout *layout = new QVBoxLayout(controls);
    layout->setContentsMargins(4, 4, 4, 4);

    // ── Mesh group ───────────────────────────────────────────────────────────
    QGroupBox *meshGroup = new QGroupBox("Mesh");
    QVBoxLayout *meshLayout = new QVBoxLayout(meshGroup);

    this->loadMeshBtn = new QPushButton("Load Mesh (OBJ / GLTF)…");
    connect(this->loadMeshBtn, &QPushButton::clicked, this, &ReliefTestModule::onLoadMesh);
    meshLayout->addWidget(this->loadMeshBtn);

    this->meshStatusLbl = new QLabel("No mesh loaded");
    this->meshStatusLbl->setWordWrap(true);
    this->meshStatusLbl->setStyleSheet("color: #aaa; font-size: 11px;");
    meshLayout->addWidget(this->meshStatusLbl);

    layout->addWidget(meshGroup);

    // ── Input textures group ─────────────────────────────────────────────────
    QGroupBox *texGroup = new QGroupBox("Input Textures");
    QVBoxLayout *texLayout = new QVBoxLayout(texGroup);

    auto makeTexRow = [&](const char *label, QLabel *&thumb, QPushButton *&btn, auto slot)
    {
        QHBoxLayout *row = new QHBoxLayout();
        thumb = new QLabel();
        thumb->setFixedSize(48, 48);
        thumb->setAlignment(Qt::AlignCenter);
        thumb->setStyleSheet("background-color:#1e1e1e; border:1px solid #555;");
        thumb->setText("—");
        row->addWidget(thumb);
        btn = new QPushButton(QString("Load %1…").arg(label));
        connect(btn, &QPushButton::clicked, this, slot);
        row->addWidget(btn, 1);
        texLayout->addLayout(row);
    };

    makeTexRow("Color", this->thumbColor, this->loadColorBtn, &ReliefTestModule::onLoadColor);
    makeTexRow("Depth", this->thumbDepth, this->loadDepthBtn, &ReliefTestModule::onLoadDepth);
    makeTexRow("Normal", this->thumbNormal, this->loadNormalBtn, &ReliefTestModule::onLoadNormal);

    layout->addWidget(texGroup);

    // ── Relief parameters group ───────────────────────────────────────────────
    QGroupBox *ctrlGroup = new QGroupBox("Relief Mapping Parameters");
    QVBoxLayout *ctrlLayout = new QVBoxLayout(ctrlGroup);

    this->reliefEnabledCheck = new QCheckBox("Enable Relief Mapping");
    this->reliefEnabledCheck->setChecked(true);
    connect(this->reliefEnabledCheck, &QCheckBox::toggled, this->reliefView, &ReliefView::setReliefEnabled);
    ctrlLayout->addWidget(this->reliefEnabledCheck);

    QHBoxLayout *stepsRow = new QHBoxLayout();
    stepsRow->addWidget(new QLabel("Steps:"));
    this->stepsSpin = new QSpinBox();
    this->stepsSpin->setRange(1, 256);
    this->stepsSpin->setValue(64);
    connect(this->stepsSpin, QOverload<int>::of(&QSpinBox::valueChanged), this->reliefView, &ReliefView::setSteps);
    stepsRow->addWidget(this->stepsSpin, 1);
    ctrlLayout->addLayout(stepsRow);

    QHBoxLayout *depthRow = new QHBoxLayout();
    depthRow->addWidget(new QLabel("Depth Scale:"));
    this->depthScaleSpin = new QDoubleSpinBox();
    this->depthScaleSpin->setRange(0.0, 2.0);
    this->depthScaleSpin->setSingleStep(0.01);
    this->depthScaleSpin->setDecimals(4);
    this->depthScaleSpin->setValue(0.05);
    connect(this->depthScaleSpin, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this->reliefView, &ReliefView::setDepthScale);
    depthRow->addWidget(this->depthScaleSpin, 1);
    ctrlLayout->addLayout(depthRow);

    this->useAtlasCheck = new QCheckBox("Use Atlas (Island Leaping)");
    this->useAtlasCheck->setChecked(true);
    connect(this->useAtlasCheck, &QCheckBox::toggled, this->reliefView, &ReliefView::setUseAtlas);
    ctrlLayout->addWidget(this->useAtlasCheck);

    QHBoxLayout *texTypeRow = new QHBoxLayout();
    texTypeRow->addWidget(new QLabel("Texture Type:"));
    this->textureTypeCombo = new QComboBox();
    this->textureTypeCombo->addItem("Depth Map", 0);
    this->textureTypeCombo->addItem("Height Map", 1);
    connect(this->textureTypeCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this](int)
            { this->reliefView->setReliefTextureType(this->textureTypeCombo->currentData().toInt()); });
    texTypeRow->addWidget(this->textureTypeCombo, 1);
    ctrlLayout->addLayout(texTypeRow);

    QHBoxLayout *debugRow = new QHBoxLayout();
    debugRow->addWidget(new QLabel("Debug:"));
    this->debugViewCombo = new QComboBox();
    this->debugViewCombo->addItem("Shaded", 0);
    this->debugViewCombo->addItem("Step Count", 1);
    this->debugViewCombo->addItem("Leap Count", 2);
    this->debugViewCombo->addItem("UV After Relief", 3);
    connect(this->debugViewCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this](int)
            { this->reliefView->setDebugView(this->debugViewCombo->currentData().toInt()); });
    debugRow->addWidget(this->debugViewCombo, 1);
    ctrlLayout->addLayout(debugRow);

    QHBoxLayout *viewRow = new QHBoxLayout();
    this->wireframeCheck = new QCheckBox("Wireframe");
    connect(this->wireframeCheck, &QCheckBox::toggled, this->reliefView, &ReliefView::setWireframe);
    viewRow->addWidget(this->wireframeCheck);
    this->cullFaceCheck = new QCheckBox("Backface Cull");
    this->cullFaceCheck->setChecked(true);
    connect(this->cullFaceCheck, &QCheckBox::toggled, this->reliefView, &ReliefView::setCullFace);
    viewRow->addWidget(this->cullFaceCheck);
    ctrlLayout->addLayout(viewRow);

    this->resetCamBtn = new QPushButton("Reset Camera");
    connect(this->resetCamBtn, &QPushButton::clicked, this->reliefView, &ReliefView::resetCamera);
    ctrlLayout->addWidget(this->resetCamBtn);

    layout->addWidget(ctrlGroup);
    layout->addStretch();

    QScrollArea *scrollArea = new QScrollArea();
    scrollArea->setWidget(controls);
    scrollArea->setWidgetResizable(true);
    scrollArea->setMinimumWidth(220);
    scrollArea->setMaximumWidth(360);
    scrollArea->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);

    return scrollArea;
}

// ─── Slots ────────────────────────────────────────────────────────────────────

void ReliefTestModule::onLoadMesh()
{
    QString path = QFileDialog::getOpenFileName(this, "Open Mesh File", "",
                                                "Model Files (*.obj *.gltf *.glb);;OBJ Files (*.obj);;GLTF Files (*.gltf *.glb);;All Files (*)");
    if (path.isEmpty())
        return;

    auto m = std::make_unique<QEMSimplifier>();
    bool ok = path.endsWith(".obj", Qt::CaseInsensitive)
                  ? m->loadOBJ(path.toStdString())
                  : m->loadGLTF(path.toStdString());

    if (!ok)
    {
        QMessageBox::critical(this, "Error", "Failed to load mesh file.");
        return;
    }

    this->mesh = std::move(m);
    this->reliefView->setMesh(this->mesh.get());

    QFileInfo fi(path);
    this->meshStatusLbl->setText(QString("%1  (%2 faces)").arg(fi.fileName()).arg(this->mesh->faceCount()));

    if (!this->depthImg.isNull())
        recomputeDepthTextures();
}

void ReliefTestModule::onLoadColor()
{
    QString path = QFileDialog::getOpenFileName(this, "Open Color Texture", "",
                                                "Images (*.png *.jpg *.jpeg *.bmp *.tga);;All Files (*)");
    if (path.isEmpty())
        return;
    this->colorImg.load(path);
    if (this->colorImg.isNull())
    {
        QMessageBox::critical(this, "Error", "Failed to load color image.");
        return;
    }
    setThumb(this->thumbColor, this->colorImg);

    int kRes = 1;
    while (kRes < std::max(this->colorImg.width(), this->colorImg.height())) kRes <<= 1;

    QImage c = this->colorImg.convertToFormat(QImage::Format_RGBA8888);
    RawImage raw{c.constBits(), c.width(), c.height(), 4};
    auto mip0 = resampleColorRGBA(raw, kRes, kRes);
    auto colorMap = Textures::buildBilinearPyramid(mip0, kRes, kRes, 4);
    this->reliefView->setColorMap(colorMap);
}

void ReliefTestModule::onLoadDepth()
{
    QString path = QFileDialog::getOpenFileName(this, "Open Depth (Heightmap) Texture", "",
                                                "Images (*.png *.jpg *.jpeg *.bmp *.tga);;All Files (*)");
    if (path.isEmpty())
        return;
    this->depthImg.load(path);
    if (this->depthImg.isNull())
    {
        QMessageBox::critical(this, "Error", "Failed to load depth image.");
        return;
    }
    setThumb(this->thumbDepth, this->depthImg);
    recomputeDepthTextures();
}

void ReliefTestModule::onLoadNormal()
{
    QString path = QFileDialog::getOpenFileName(this, "Open Normal Map Texture", "",
                                                "Images (*.png *.jpg *.jpeg *.bmp *.tga);;All Files (*)");
    if (path.isEmpty())
        return;
    this->normalImg.load(path);
    if (this->normalImg.isNull())
    {
        QMessageBox::critical(this, "Error", "Failed to load normal image.");
        return;
    }
    setThumb(this->thumbNormal, this->normalImg);

    int kRes = 1;
    while (kRes < std::max(this->normalImg.width(), this->normalImg.height())) kRes <<= 1;

    QImage n = this->normalImg.convertToFormat(QImage::Format_RGB888);
    RawImage raw{n.constBits(), n.width(), n.height(), 3};
    auto mip0 = resampleNormalXYZ(raw, kRes, kRes);
    auto normalMap = Textures::buildBilinearPyramid(mip0, kRes, kRes, 3, /*renormalizeAsNormal=*/true);
    this->reliefView->setNormalMap(normalMap);
}

void ReliefTestModule::recomputeDepthTextures()
{
    if (this->depthImg.isNull())
        return;

    int kRes = 1;
    while (kRes < std::max(this->depthImg.width(), this->depthImg.height())) kRes <<= 1;

    QImage d = this->depthImg.convertToFormat(QImage::Format_Grayscale8);
    RawImage rawDepth{d.constBits(), d.width(), d.height(), 1};
    auto depthMip0 = resampleDepthR(rawDepth, kRes, kRes);

    constexpr int kSeam = 4;
    std::vector<float> seamMip0((size_t)kRes * kRes, 0.f);

    if (this->mesh)
    {
        auto faceIsland = UVAtlas::detectIslands(*this->mesh);
        auto offsetMap  = UVAtlas::bakeOffsetMap(*this->mesh, faceIsland, kRes, kRes, kSeam);
        for (size_t i = 0; i < (size_t)kRes * kRes; i++)
            seamMip0[i] = offsetMap.data[i * 4 + 3];
        this->reliefView->setOffsetMap(offsetMap);
    }

    auto minPyr  = Textures::buildMinPyramid(depthMip0, kRes, kRes);
    auto maxPyr  = Textures::buildMaxPyramid(depthMip0, kRes, kRes);
    auto maskPyr = Textures::buildMaxPyramid(seamMip0,  kRes, kRes);

    MipPyramid reliefMap;
    reliefMap.width = kRes; reliefMap.height = kRes; reliefMap.channels = 4;
    for (int lvl = 0; lvl < minPyr.levelCount(); lvl++)
    {
        int w = std::max(1, kRes >> lvl), h = std::max(1, kRes >> lvl);
        std::vector<float> mip((size_t)w * h * 4);
        for (size_t i = 0; i < (size_t)w * h; i++)
        {
            mip[i*4+0] = minPyr.mips[lvl][i];
            mip[i*4+1] = maxPyr.mips[lvl][i];
            mip[i*4+2] = maskPyr.mips[lvl][i];
            mip[i*4+3] = 0.f;
        }
        reliefMap.mips.push_back(std::move(mip));
    }
    this->reliefView->setReliefMap(reliefMap);
}

// ─── Helpers ─────────────────────────────────────────────────────────────────

void ReliefTestModule::setThumb(QLabel *label, const QImage &img)
{
    label->setPixmap(QPixmap::fromImage(img)
                         .scaled(label->size(), Qt::KeepAspectRatio, Qt::SmoothTransformation));
}
