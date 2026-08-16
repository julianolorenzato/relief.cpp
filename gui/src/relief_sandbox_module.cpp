/**
 * @file relief_sandbox_module.cpp
 * @brief ReliefSandboxModule implementation: loads a mesh plus color/depth/normal
 *        images independently of the main pipeline, resamples them into mip0
 *        buffers, and bakes/previews the relief maps synchronously.
 */
#include "gui/relief_sandbox_module.h"
#include "gui/texture_inspector_dialog.h"
#include "relief/textures.h"
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
#include <QPainter>

// ─── Constructor ─────────────────────────────────────────────────────────────

ReliefSandboxModule::ReliefSandboxModule(QWidget *parent)
    : QWidget(parent)
{
    QHBoxLayout *outerLayout = new QHBoxLayout(this);
    outerLayout->setContentsMargins(0, 0, 0, 0);

    QSplitter *splitter = new QSplitter(Qt::Horizontal, this);
    outerLayout->addWidget(splitter);

    this->reliefView = new ReliefView();
    this->reliefView->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    splitter->addWidget(this->reliefView);
    connect(this->reliefView, &ReliefView::pixelPicked, this, &ReliefSandboxModule::onPixelPicked);

    QWidget *controls = buildControls();
    splitter->addWidget(controls);
}

QWidget *ReliefSandboxModule::buildControls()
{
    QWidget *controls = new QWidget();
    QVBoxLayout *layout = new QVBoxLayout(controls);
    layout->setContentsMargins(4, 4, 4, 4);

    // ── Mesh group ───────────────────────────────────────────────────────────
    QGroupBox *meshGroup = new QGroupBox("Mesh");
    QVBoxLayout *meshLayout = new QVBoxLayout(meshGroup);

    this->loadMeshBtn = new QPushButton("Load Mesh (OBJ / GLTF)…");
    connect(this->loadMeshBtn, &QPushButton::clicked, this, &ReliefSandboxModule::onLoadMesh);
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

    makeTexRow("Color", this->thumbColor, this->loadColorBtn, &ReliefSandboxModule::onLoadColor);
    makeTexRow("Depth", this->thumbDepth, this->loadDepthBtn, &ReliefSandboxModule::onLoadDepth);
    makeTexRow("Normal", this->thumbNormal, this->loadNormalBtn, &ReliefSandboxModule::onLoadNormal);

    this->inspectTexturesBtn = new QPushButton("Inspect Textures…");
    connect(this->inspectTexturesBtn, &QPushButton::clicked, this, &ReliefSandboxModule::onInspectTextures);
    texLayout->addWidget(this->inspectTexturesBtn);

    layout->addWidget(texGroup);

    // ── Pixel pick group ─────────────────────────────────────────────────────
    QGroupBox *pickGroup = new QGroupBox("Pixel Pick");
    QVBoxLayout *pickLayout = new QVBoxLayout(pickGroup);

    this->pickInfoLbl = new QLabel("Click a pixel in the view to see where it samples the color texture.");
    this->pickInfoLbl->setWordWrap(true);
    this->pickInfoLbl->setStyleSheet("color: #aaa; font-size: 11px;");
    pickLayout->addWidget(this->pickInfoLbl);

    this->pickPreviewLbl = new QLabel();
    this->pickPreviewLbl->setFixedSize(220, 220);
    this->pickPreviewLbl->setAlignment(Qt::AlignCenter);
    this->pickPreviewLbl->setStyleSheet("background-color:#1e1e1e; border:1px solid #555;");
    pickLayout->addWidget(this->pickPreviewLbl);

    layout->addWidget(pickGroup);

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

void ReliefSandboxModule::onLoadMesh()
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

void ReliefSandboxModule::onLoadColor()
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
    auto mip0 = Textures::resampleColorRGBA(raw, kRes, kRes);
    this->colorMapData = Textures::buildBilinearPyramid(mip0, kRes, kRes, 4);
    this->reliefView->setColorMap(this->colorMapData);
}

void ReliefSandboxModule::onLoadDepth()
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

void ReliefSandboxModule::onLoadNormal()
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
    auto mip0 = Textures::resampleNormalXYZ(raw, kRes, kRes);
    this->normalMapData = Textures::buildBilinearPyramid(mip0, kRes, kRes, 3, /*renormalizeAsNormal=*/true);
    this->reliefView->setNormalMap(this->normalMapData);
}

void ReliefSandboxModule::recomputeDepthTextures()
{
    if (this->depthImg.isNull())
        return;

    int kRes = 1;
    while (kRes < std::max(this->depthImg.width(), this->depthImg.height())) kRes <<= 1;

    QImage d = this->depthImg.convertToFormat(QImage::Format_Grayscale8);
    RawImage rawDepth{d.constBits(), d.width(), d.height(), 1};
    auto depthMip0 = Textures::resampleDepthR(rawDepth, kRes, kRes);

    constexpr int kSeam = 16;
    std::vector<float> seamMip0((size_t)kRes * kRes, 0.f);

    if (this->mesh)
    {
        auto faceIsland = UVAtlas::detectIslands(*this->mesh);
        this->offsetMapData = UVAtlas::bakeOffsetMap(*this->mesh, faceIsland, kRes, kRes, kSeam);
        for (size_t i = 0; i < (size_t)kRes * kRes; i++)
            seamMip0[i] = this->offsetMapData.data[i * 4 + 3];
        this->reliefView->setOffsetMap(this->offsetMapData);
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
    this->reliefMapData = std::move(reliefMap);
    this->reliefView->setReliefMap(this->reliefMapData);
}

void ReliefSandboxModule::onInspectTextures()
{
    if (this->colorMapData.levelCount() == 0 && this->reliefMapData.levelCount() == 0 &&
        this->normalMapData.levelCount() == 0 && this->offsetMapData.width == 0)
    {
        QMessageBox::information(this, "Texture Inspector", "No textures loaded yet.");
        return;
    }

    TextureInspectorDialog dlg(&this->colorMapData, &this->reliefMapData,
                                &this->normalMapData, &this->offsetMapData, this);
    dlg.exec();
}

void ReliefSandboxModule::onPixelPicked(QPointF uv, bool hit)
{
    if (!hit)
    {
        this->pickInfoLbl->setText("Click missed — no surface at that pixel.");
        this->pickPreviewLbl->clear();
        return;
    }

    this->pickInfoLbl->setText(QString("Texture UV: (%1, %2)").arg(uv.x(), 0, 'f', 4).arg(uv.y(), 0, 'f', 4));

    if (this->colorImg.isNull())
        return;

    // colorMapData's mip0 row y == colorImg's row y (see Textures::resampleColorRGBA
    // in ReliefSandboxModule::onLoadColor), so (u, v) maps onto colorImg with no flip.
    QImage preview = this->colorImg
                         .scaled(this->pickPreviewLbl->size(), Qt::KeepAspectRatio, Qt::SmoothTransformation)
                         .convertToFormat(QImage::Format_RGB32);
    QPainter p(&preview);
    p.setPen(QPen(Qt::red, 2));
    p.setBrush(Qt::NoBrush);
    QPointF marker(uv.x() * preview.width(), uv.y() * preview.height());
    p.drawEllipse(marker, 5, 5);
    p.drawLine(marker - QPointF(8, 0), marker + QPointF(8, 0));
    p.drawLine(marker - QPointF(0, 8), marker + QPointF(0, 8));
    p.end();

    this->pickPreviewLbl->setPixmap(QPixmap::fromImage(preview));
}

// ─── Helpers ─────────────────────────────────────────────────────────────────

void ReliefSandboxModule::setThumb(QLabel *label, const QImage &img)
{
    label->setPixmap(QPixmap::fromImage(img)
                         .scaled(label->size(), Qt::KeepAspectRatio, Qt::SmoothTransformation));
}
