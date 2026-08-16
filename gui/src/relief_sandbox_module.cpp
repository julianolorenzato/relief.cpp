/**
 * @file relief_sandbox_module.cpp
 * @brief ReliefSandboxModule implementation: loads a mesh plus
 * color/depth/normal images independently of the main pipeline, resamples them
 * into mip0 buffers, and bakes/previews the relief maps synchronously.
 */

#include "gui/relief_sandbox_module.h"

#include <QCheckBox>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QFileDialog>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QMessageBox>
#include <QPainter>
#include <QPushButton>
#include <QScrollArea>
#include <QSpinBox>
#include <QSplitter>
#include <QVBoxLayout>
#include <algorithm>
#include <cmath>

#include "gui/texture_inspector_dialog.h"
#include "relief/textures.h"
#include "relief/uv_atlas.h"

/**
 * A bunch of tiny helpers.
 */
namespace {

/**
 * Builds one "thumbnail + Load <label>… button" row
 * and appends it to `texLayout`.
 */
void makeTexRow(QVBoxLayout* texLayout, ReliefSandboxModule* self,
                const char* label, QLabel*& thumb, QPushButton*& btn,
                void (ReliefSandboxModule::*slot)()) {
    QHBoxLayout* row = new QHBoxLayout();
    thumb = new QLabel();
    thumb->setFixedSize(48, 48);
    thumb->setAlignment(Qt::AlignCenter);
    thumb->setStyleSheet("background-color:#1e1e1e; border:1px solid #555;");
    row->addWidget(thumb);
    btn = new QPushButton(QString("Load %1…").arg(label));
    QObject::connect(btn, &QPushButton::clicked, self, slot);
    row->addWidget(btn, 1);
    texLayout->addLayout(row);
}

/**
 * Scales `img` into `label` as a thumbnail preview.
 */
void setThumb(QLabel* label, const QImage& img) {
    label->setPixmap(QPixmap::fromImage(img).scaled(
        label->size(), Qt::KeepAspectRatio, Qt::SmoothTransformation));
}

}  // namespace

ReliefSandboxModule::ReliefSandboxModule(QWidget* parent) : QWidget(parent) {
    QHBoxLayout* outerLayout = new QHBoxLayout(this);
    outerLayout->setContentsMargins(0, 0, 0, 0);

    // Root container, horizontally divides the screen in two parts.
    QSplitter* splitter = new QSplitter(Qt::Horizontal, this);
    outerLayout->addWidget(splitter);

    // Viewport widget (left)
    this->reliefView = new ReliefView();
    connect(this->reliefView, &ReliefView::pixelPicked, this,
            &ReliefSandboxModule::onPixelPicked);

    // Controls widget (right)
    QWidget* controls = buildControls();

    // Add the two parts to the module
    splitter->addWidget(this->reliefView);
    splitter->addWidget(controls);
}

MeshControls::MeshControls(QWidget* outerControls, ReliefSandboxModule* self) {
    QGroupBox* group = new QGroupBox("Mesh");
    QVBoxLayout* meshLayout = new QVBoxLayout(group);

    QPushButton* loadBtn = new QPushButton("Load Mesh (OBJ / GLTF)…");
    QObject::connect(loadBtn, &QPushButton::clicked, self,
                     &ReliefSandboxModule::onLoadMesh);
    meshLayout->addWidget(loadBtn);

    this->statusLbl = new QLabel("No mesh loaded");
    this->statusLbl->setWordWrap(true);
    this->statusLbl->setStyleSheet("color: #aaa; font-size: 11px;");
    meshLayout->addWidget(this->statusLbl);

    outerControls->layout()->addWidget(group);
}

TextureControls::TextureControls(QWidget* outerControls,
                                 ReliefSandboxModule* self) {
    QGroupBox* group = new QGroupBox("Input Textures");
    QVBoxLayout* texLayout = new QVBoxLayout(group);

    QPushButton* btn = nullptr;
    makeTexRow(texLayout, self, "Color", this->thumbColor, btn,
               &ReliefSandboxModule::onLoadColor);
    makeTexRow(texLayout, self, "Depth", this->thumbDepth, btn,
               &ReliefSandboxModule::onLoadDepth);
    makeTexRow(texLayout, self, "Normal", this->thumbNormal, btn,
               &ReliefSandboxModule::onLoadNormal);

    QPushButton* inspectBtn = new QPushButton("Inspect Textures…");
    QObject::connect(inspectBtn, &QPushButton::clicked, self,
                     &ReliefSandboxModule::onInspectTextures);
    texLayout->addWidget(inspectBtn);

    outerControls->layout()->addWidget(group);
}

PixelPickControls::PixelPickControls(QWidget* outerControls) {
    QGroupBox* group = new QGroupBox("Pixel Pick");
    QVBoxLayout* pickLayout = new QVBoxLayout(group);

    this->infoLbl = new QLabel(
        "Click a pixel in the view to see where it samples the color texture.");
    this->infoLbl->setWordWrap(true);
    this->infoLbl->setStyleSheet("color: #aaa; font-size: 11px;");
    pickLayout->addWidget(this->infoLbl);

    this->previewLbl = new QLabel();
    this->previewLbl->setFixedSize(220, 220);
    this->previewLbl->setAlignment(Qt::AlignCenter);
    this->previewLbl->setStyleSheet(
        "background-color:#1e1e1e; border:1px solid #555;");
    pickLayout->addWidget(this->previewLbl);

    outerControls->layout()->addWidget(group);
}

void ReliefSandboxModule::buildReliefParamsGroup(QWidget* outerControls) {
    QGroupBox* ctrlGroup = new QGroupBox("Relief Mapping Parameters");
    QVBoxLayout* ctrlLayout = new QVBoxLayout(ctrlGroup);

    // Enabled
    QCheckBox* reliefEnabledCheck = new QCheckBox("Enable Relief Mapping");
    reliefEnabledCheck->setChecked(true);
    connect(reliefEnabledCheck, &QCheckBox::toggled, this->reliefView,
            &ReliefView::setReliefEnabled);
    ctrlLayout->addWidget(reliefEnabledCheck);

    // Steps
    QHBoxLayout* stepsRow = new QHBoxLayout();
    stepsRow->addWidget(new QLabel("Steps:"));
    QSpinBox* stepsSpin = new QSpinBox();
    stepsSpin->setRange(1, 512);
    stepsSpin->setValue(256);
    connect(stepsSpin, QOverload<int>::of(&QSpinBox::valueChanged),
            this->reliefView, &ReliefView::setSteps);
    stepsRow->addWidget(stepsSpin, 1);
    ctrlLayout->addLayout(stepsRow);

    // Depth Scale
    QHBoxLayout* depthRow = new QHBoxLayout();
    depthRow->addWidget(new QLabel("Depth Scale:"));
    QDoubleSpinBox* depthScaleSpin = new QDoubleSpinBox();
    depthScaleSpin->setRange(0.0, 2.0);
    depthScaleSpin->setSingleStep(0.01);
    depthScaleSpin->setDecimals(4);
    depthScaleSpin->setValue(0.05);
    connect(depthScaleSpin,
            QOverload<double>::of(&QDoubleSpinBox::valueChanged),
            this->reliefView, &ReliefView::setDepthScale);
    depthRow->addWidget(depthScaleSpin, 1);
    ctrlLayout->addLayout(depthRow);

    // Use Atlas
    QCheckBox* useAtlasCheck = new QCheckBox("Use Atlas (Island Leaping)");
    useAtlasCheck->setChecked(true);
    connect(useAtlasCheck, &QCheckBox::toggled, this->reliefView,
            &ReliefView::setUseAtlas);
    ctrlLayout->addWidget(useAtlasCheck);

    // Texture Type
    QHBoxLayout* texTypeRow = new QHBoxLayout();
    texTypeRow->addWidget(new QLabel("Texture Type:"));
    QComboBox* textureTypeCombo = new QComboBox();
    textureTypeCombo->addItem("Depth Map", 0);
    textureTypeCombo->addItem("Height Map", 1);
    connect(textureTypeCombo,
            QOverload<int>::of(&QComboBox::currentIndexChanged), this,
            [this, textureTypeCombo](int) {
                this->reliefView->setReliefTextureType(
                    textureTypeCombo->currentData().toInt());
            });
    texTypeRow->addWidget(textureTypeCombo, 1);
    ctrlLayout->addLayout(texTypeRow);

    // Debug Type
    QHBoxLayout* debugRow = new QHBoxLayout();
    debugRow->addWidget(new QLabel("Debug:"));
    QComboBox* debugViewCombo = new QComboBox();
    debugViewCombo->addItem("Shaded", 0);
    debugViewCombo->addItem("Step Count", 1);
    debugViewCombo->addItem("Leap Count", 2);
    debugViewCombo->addItem("UV After Relief", 3);
    connect(debugViewCombo, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, [this, debugViewCombo](int) {
                this->reliefView->setDebugView(
                    debugViewCombo->currentData().toInt());
            });
    debugRow->addWidget(debugViewCombo, 1);
    ctrlLayout->addLayout(debugRow);

    // View Type
    QHBoxLayout* viewRow = new QHBoxLayout();
    QCheckBox* wireframeCheck = new QCheckBox("Wireframe");
    connect(wireframeCheck, &QCheckBox::toggled, this->reliefView,
            &ReliefView::setWireframe);
    viewRow->addWidget(wireframeCheck);
    QCheckBox* cullFaceCheck = new QCheckBox("Backface Cull");
    cullFaceCheck->setChecked(true);
    connect(cullFaceCheck, &QCheckBox::toggled, this->reliefView,
            &ReliefView::setCullFace);
    viewRow->addWidget(cullFaceCheck);
    ctrlLayout->addLayout(viewRow);

    QPushButton* resetCamBtn = new QPushButton("Reset Camera");
    connect(resetCamBtn, &QPushButton::clicked, this->reliefView,
            &ReliefView::resetCamera);
    ctrlLayout->addWidget(resetCamBtn);

    outerControls->layout()->addWidget(ctrlGroup);
}

QWidget* ReliefSandboxModule::buildControls() {
    QWidget* controls = new QWidget();
    QVBoxLayout* layout = new QVBoxLayout(controls);
    layout->setContentsMargins(4, 4, 4, 4);

    this->meshControls = MeshControls(controls, this);
    this->textureControls = TextureControls(controls, this);
    this->pixelPickControls = PixelPickControls(controls);
    buildReliefParamsGroup(controls);

    layout->addStretch();

    QScrollArea* scrollArea = new QScrollArea();
    scrollArea->setWidget(controls);
    scrollArea->setWidgetResizable(true);
    scrollArea->setMinimumWidth(220);
    scrollArea->setMaximumWidth(360);
    scrollArea->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);

    return scrollArea;
}

// ------- Slots --------
void ReliefSandboxModule::onLoadMesh() {
    QString path =
        QFileDialog::getOpenFileName(this, "Open Mesh File", "",
                                     "Model Files (*.obj *.gltf *.glb);;OBJ "
                                     "Files (*.obj);;GLTF Files (*.gltf "
                                     "*.glb);;All Files (*)");
    if (path.isEmpty()) return;

    auto m = std::make_unique<QEMSimplifier>();
    bool ok = path.endsWith(".obj", Qt::CaseInsensitive)
                  ? m->loadOBJ(path.toStdString())
                  : m->loadGLTF(path.toStdString());

    if (!ok) {
        QMessageBox::critical(this, "Error", "Failed to load mesh file.");
        return;
    }

    this->mesh = std::move(m);
    this->reliefView->setMesh(this->mesh.get());

    QFileInfo fi(path);
    this->meshControls.statusLbl->setText(QString("%1  (%2 faces)")
                                              .arg(fi.fileName())
                                              .arg(this->mesh->faceCount()));

    if (!this->depthImg.isNull()) recomputeDepthTextures();
}

void ReliefSandboxModule::onLoadColor() {
    QString path = QFileDialog::getOpenFileName(
        this, "Open Color Texture", "",
        "Images (*.png *.jpg *.jpeg *.bmp *.tga);;All Files (*)");
    if (path.isEmpty()) return;

    this->colorImg.load(path);
    if (this->colorImg.isNull()) {
        QMessageBox::critical(this, "Error", "Failed to load color image.");
        return;
    }

    setThumb(this->textureControls.thumbColor, this->colorImg);

    int kRes = Textures::nextPowerOfTwo(
        std::max(this->colorImg.width(), this->colorImg.height()));

    QImage c = this->colorImg.convertToFormat(QImage::Format_RGBA8888);
    RawImage raw{c.constBits(), c.width(), c.height(), 4};
    this->colorMapData = Textures::buildColorMap(raw, kRes, kRes);
    this->reliefView->setColorMap(this->colorMapData);
}

void ReliefSandboxModule::onLoadDepth() {
    QString path = QFileDialog::getOpenFileName(
        this, "Open Depth (Heightmap) Texture", "",
        "Images (*.png *.jpg *.jpeg *.bmp *.tga);;All Files (*)");
    if (path.isEmpty()) return;
    this->depthImg.load(path);
    if (this->depthImg.isNull()) {
        QMessageBox::critical(this, "Error", "Failed to load depth image.");
        return;
    }
    setThumb(this->textureControls.thumbDepth, this->depthImg);
    recomputeDepthTextures();
}

void ReliefSandboxModule::onLoadNormal() {
    QString path = QFileDialog::getOpenFileName(
        this, "Open Normal Map Texture", "",
        "Images (*.png *.jpg *.jpeg *.bmp *.tga);;All Files (*)");
    if (path.isEmpty()) return;
    this->normalImg.load(path);
    if (this->normalImg.isNull()) {
        QMessageBox::critical(this, "Error", "Failed to load normal image.");
        return;
    }
    setThumb(this->textureControls.thumbNormal, this->normalImg);

    int kRes = Textures::nextPowerOfTwo(
        std::max(this->normalImg.width(), this->normalImg.height()));

    QImage n = this->normalImg.convertToFormat(QImage::Format_RGB888);
    RawImage raw{n.constBits(), n.width(), n.height(), 3};
    this->normalMapData = Textures::buildNormalMap(raw, kRes, kRes);
    this->reliefView->setNormalMap(this->normalMapData);
}

void ReliefSandboxModule::recomputeDepthTextures() {
    if (this->depthImg.isNull()) return;

    int kRes = Textures::nextPowerOfTwo(
        std::max(this->depthImg.width(), this->depthImg.height()));

    QImage d = this->depthImg.convertToFormat(QImage::Format_Grayscale8);
    RawImage rawDepth{d.constBits(), d.width(), d.height(), 1};

    constexpr int kSeam = 16;
    if (this->mesh) {
        this->offsetMapData = UVAtlas::buildOffsetMap(*this->mesh, kRes, kRes, kSeam);
        this->reliefView->setOffsetMap(this->offsetMapData);
    }

    this->reliefMapData =
        Textures::buildReliefMap(rawDepth, kRes, kRes, this->offsetMapData);
    this->reliefView->setReliefMap(this->reliefMapData);
}

void ReliefSandboxModule::onInspectTextures() {
    if (this->colorMapData.levelCount() == 0 &&
        this->reliefMapData.levelCount() == 0 &&
        this->normalMapData.levelCount() == 0 &&
        this->offsetMapData.width == 0) {
        QMessageBox::information(this, "Texture Inspector",
                                 "No textures loaded yet.");
        return;
    }

    TextureInspectorDialog dlg(&this->colorMapData, &this->reliefMapData,
                               &this->normalMapData, &this->offsetMapData,
                               this);
    dlg.exec();
}

void ReliefSandboxModule::onPixelPicked(QPointF uv, bool hit) {
    if (!hit) {
        this->pixelPickControls.infoLbl->setText(
            "Click missed — no surface at that pixel.");
        this->pixelPickControls.previewLbl->clear();
        return;
    }

    this->pixelPickControls.infoLbl->setText(QString("Texture UV: (%1, %2)")
                                                 .arg(uv.x(), 0, 'f', 4)
                                                 .arg(uv.y(), 0, 'f', 4));

    if (this->colorImg.isNull()) return;

    // colorMapData's mip0 row y == colorImg's row y (see
    // Textures::buildColorMap's resampling in ReliefSandboxModule::onLoadColor),
    // so (u, v) maps onto colorImg with no flip.
    QImage preview = this->colorImg
                         .scaled(this->pixelPickControls.previewLbl->size(),
                                 Qt::KeepAspectRatio, Qt::SmoothTransformation)
                         .convertToFormat(QImage::Format_RGB32);
    QPainter p(&preview);
    p.setPen(QPen(Qt::red, 2));
    p.setBrush(Qt::NoBrush);
    QPointF marker(uv.x() * preview.width(), uv.y() * preview.height());
    p.drawEllipse(marker, 5, 5);
    p.drawLine(marker - QPointF(8, 0), marker + QPointF(8, 0));
    p.drawLine(marker - QPointF(0, 8), marker + QPointF(0, 8));
    p.end();

    this->pixelPickControls.previewLbl->setPixmap(QPixmap::fromImage(preview));
}
