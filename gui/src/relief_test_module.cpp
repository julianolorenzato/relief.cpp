#include "gui/relief_test_module.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QGroupBox>
#include <QScrollArea>
#include <QSplitter>
#include <QFileDialog>
#include <QMessageBox>
#include <QApplication>

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

    this->renderBtn = new QPushButton("Render");
    connect(this->renderBtn, &QPushButton::clicked, this, &ReliefTestModule::onBake);
    ctrlLayout->addWidget(this->renderBtn);

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
}

void ReliefTestModule::onBake()
{
    if (!this->mesh || this->colorImg.isNull() || this->depthImg.isNull() || this->normalImg.isNull())
    {
        QMessageBox::warning(this, "Missing inputs", "Load a mesh and all three textures first.");
        return;
    }

    this->renderBtn->setEnabled(false);
    QApplication::processEvents();

    QImage c = this->colorImg.convertToFormat(QImage::Format_RGBA8888);
    QImage d = this->depthImg.convertToFormat(QImage::Format_Grayscale8);
    QImage n = this->normalImg.convertToFormat(QImage::Format_RGB888);

    RawImage rawColor  { c.constBits(), c.width(), c.height(), 4 };
    RawImage rawDepth  { d.constBits(), d.width(), d.height(), 1 };
    RawImage rawNormal { n.constBits(), n.width(), n.height(), 3 };

    this->tpResult = TextureBaker::bake(*this->mesh, rawColor, rawDepth, rawNormal, 512, 4);

    if (!this->tpResult.valid)
        QMessageBox::critical(this, "Error", "Bake failed.");
    else
        this->reliefView->setTextures(this->tpResult);

    this->renderBtn->setEnabled(true);
}

// ─── Helpers ─────────────────────────────────────────────────────────────────

void ReliefTestModule::setThumb(QLabel *label, const QImage &img)
{
    label->setPixmap(QPixmap::fromImage(img)
                         .scaled(label->size(), Qt::KeepAspectRatio, Qt::SmoothTransformation));
}
