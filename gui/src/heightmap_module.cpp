#include "gui/heightmap_module.h"
#include "relief/heightmap.h"

namespace {
class HeightmapWorker : public QObject {
    Q_OBJECT
public:
    const QEMSimplifier* simplified = nullptr;
    const QEMSimplifier* original   = nullptr;
    int width  = 512;
    int height = 512;
    HeightmapResult results[1];
signals:
    void progress(int overall, const QString& text);
    void finished();
public slots:
    void run() {
        emit progress(0, "UV Correspondence…");
        auto cb = [this](int pct) { emit progress(pct, "UV Correspondence…"); };
        results[0] = HeightmapBaker::bakeUVDistance(*simplified, *original, width, height, cb);
        emit progress(100, "Done");
        emit finished();
    }
};
} // namespace
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QGroupBox>
#include <QScrollArea>
#include <QSplitter>
#include <QFileDialog>
#include <QMessageBox>
#include <QImage>
#include <QPixmap>

// ─── Constructor ─────────────────────────────────────────────────────────────

HeightmapModule::HeightmapModule(QWidget* parent)
    : QWidget(parent)
{
    buildUI();
}

// ─── buildUI ─────────────────────────────────────────────────────────────────

void HeightmapModule::buildUI()
{
    QHBoxLayout* outerLayout = new QHBoxLayout(this);
    outerLayout->setContentsMargins(0, 0, 0, 0);

    QSplitter* splitter = new QSplitter(Qt::Horizontal, this);
    outerLayout->addWidget(splitter);

    // ── Left: preview QGroupBox ───────────────────────────────────────────────
    QGroupBox* panel = new QGroupBox("UV Correspondence");
    panel->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    QVBoxLayout* pLayout = new QVBoxLayout(panel);

    hmPreview_ = new QLabel();
    hmPreview_->setAlignment(Qt::AlignCenter);
    hmPreview_->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    hmPreview_->setMinimumSize(200, 200);
    hmPreview_->setStyleSheet("background-color: #1e1e1e; border: 1px solid #555;");
    hmPreview_->setText("(not baked)");
    pLayout->addWidget(hmPreview_, 1);

    hmInfoLabel_ = new QLabel("Range: —");
    hmInfoLabel_->setFixedHeight(18);
    hmInfoLabel_->setAlignment(Qt::AlignCenter);
    pLayout->addWidget(hmInfoLabel_);

    hmSaveBtn_ = new QPushButton("Save");
    hmSaveBtn_->setEnabled(false);
    connect(hmSaveBtn_, &QPushButton::clicked, this, &HeightmapModule::onSaveHeightmap);
    pLayout->addWidget(hmSaveBtn_);

    splitter->addWidget(panel);

    // ── Right: controls in a QScrollArea ─────────────────────────────────────
    QWidget* controlsWidget = new QWidget();
    QVBoxLayout* mainLayout = new QVBoxLayout(controlsWidget);

    QGroupBox* ctrlGroup = new QGroupBox("Baking Controls");
    QVBoxLayout* ctrlOuter = new QVBoxLayout(ctrlGroup);

    QHBoxLayout* ctrlRow = new QHBoxLayout();
    ctrlRow->addWidget(new QLabel("Resolution:"));
    hmResCombo_ = new QComboBox();
    hmResCombo_->addItem("128 × 128",   128);
    hmResCombo_->addItem("256 × 256",   256);
    hmResCombo_->addItem("512 × 512",   512);
    hmResCombo_->addItem("1024 × 1024", 1024);
    hmResCombo_->addItem("2048 × 2048", 2048);
    hmResCombo_->addItem("4096 × 4096", 4096);
    hmResCombo_->setCurrentIndex(2);
    ctrlRow->addWidget(hmResCombo_);

    ctrlRow->addSpacing(16);
    hmBakeBtn_ = new QPushButton("Bake");
    hmBakeBtn_->setMinimumWidth(120);
    connect(hmBakeBtn_, &QPushButton::clicked, this, &HeightmapModule::onBake);
    ctrlRow->addWidget(hmBakeBtn_);
    ctrlRow->addStretch();
    ctrlOuter->addLayout(ctrlRow);

    QHBoxLayout* progressRow = new QHBoxLayout();
    hmProgressBar_ = new QProgressBar();
    hmProgressBar_->setRange(0, 100);
    hmProgressBar_->setValue(0);
    hmProgressBar_->setTextVisible(true);
    hmProgressBar_->setFixedHeight(18);
    progressRow->addWidget(hmProgressBar_, 1);

    hmProgressLabel_ = new QLabel("Ready");
    hmProgressLabel_->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    progressRow->addWidget(hmProgressLabel_);
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

void HeightmapModule::onModelLoaded(QEMSimplifier* original, QEMSimplifier* simplified)
{
    originalMesh_   = original;
    simplifiedMesh_ = simplified;
    reset();
}

void HeightmapModule::onMeshUpdated(QEMSimplifier* original, QEMSimplifier* simplified)
{
    originalMesh_   = original;
    simplifiedMesh_ = simplified;
}

// ─── Private slots ────────────────────────────────────────────────────────────

void HeightmapModule::onBake()
{
    launchBake();
}

void HeightmapModule::onBakeProgress(int overall, const QString& text)
{
    hmProgressBar_->setValue(overall);
    hmProgressLabel_->setText(text);
}

void HeightmapModule::onBakeDone()
{
    hmResult_ = static_cast<HeightmapWorker*>(hmWorker_)->results[0];
    displayHeightmap(hmResult_);

    // Read of hmWorker_'s results is done — safe to delete it now. hmThread_
    // self-deletes once truly idle (see QThread::finished connection in launchBake).
    hmWorker_->deleteLater();
    hmWorker_ = nullptr;
    hmThread_ = nullptr;

    setBakeButtonsEnabled(true);
    hmProgressLabel_->setText("Done");
    emit statusMessage("Bake complete");
    emit bakeReady(hmResult_);
}

void HeightmapModule::onSaveHeightmap()
{
    if (!hmResult_.valid || hmResult_.image.empty())
        return;

    QString fileName = QFileDialog::getSaveFileName(this,
        "Save Heightmap", "",
        "PNG Image (*.png);;All Files (*)");

    if (fileName.isEmpty())
        return;

    QImage img(hmResult_.image.data(), hmResult_.width, hmResult_.height,
               hmResult_.width, QImage::Format_Grayscale8);
    img = img.mirrored(false, true);

    if (img.save(fileName))
    {
        emit statusMessage("Saved: " + fileName);
        QMessageBox::information(this, "Saved", "Heightmap saved as PNG.");
    }
    else
    {
        QMessageBox::critical(this, "Error", "Failed to save image.");
    }
}

// ─── Private methods ──────────────────────────────────────────────────────────

void HeightmapModule::launchBake()
{
    if (hmThread_ && hmThread_->isRunning())
        return;

    if (!simplifiedMesh_ || simplifiedMesh_->faceCount() == 0)
    {
        QMessageBox::warning(this, "Warning",
            "Simplify the mesh first before baking a heightmap.");
        return;
    }
    if (!originalMesh_ || originalMesh_->faceCount() == 0)
    {
        QMessageBox::warning(this, "Warning", "No original mesh loaded.");
        return;
    }

    bool hasUVs = false;
    for (const auto& v : simplifiedMesh_->vertices)
        if (v.uv.squaredNorm() > 1e-12) { hasUVs = true; break; }
    if (!hasUVs)
    {
        QMessageBox::warning(this, "Warning",
            "The simplified mesh has no UV coordinates.\n"
            "Load a mesh with UVs (e.g. a GLTF with texture coordinates).");
        return;
    }

    int res = hmResCombo_->currentData().toInt();

    setBakeButtonsEnabled(false);
    hmProgressBar_->setValue(0);
    hmProgressLabel_->setText("Starting…");

    hmPreview_->setText("(baking…)");
    hmPreview_->setPixmap(QPixmap());
    hmInfoLabel_->setText("Range: —");
    hmSaveBtn_->setEnabled(false);

    auto* worker = new HeightmapWorker();
    worker->simplified = simplifiedMesh_;
    worker->original   = originalMesh_;
    worker->width      = res;
    worker->height     = res;
    hmWorker_ = worker;

    hmThread_ = new QThread(this);
    worker->moveToThread(hmThread_);

    connect(hmThread_, &QThread::started,          worker, &HeightmapWorker::run);
    connect(worker,    &HeightmapWorker::progress, this,   &HeightmapModule::onBakeProgress);
    connect(worker,    &HeightmapWorker::finished, this,   &HeightmapModule::onBakeDone);
    connect(worker,    &HeightmapWorker::finished, hmThread_, &QThread::quit);
    // hmWorker_ is deleted explicitly in onBakeDone(), after it has read the results —
    // NOT via a finished->deleteLater connection on hmWorker_ itself. That connection
    // would be direct (hmWorker_ and the finished() emitter share hmThread_'s affinity),
    // so it'd race the queued, cross-thread delivery of finished() to onBakeDone():
    // hmThread_ could process the deferred delete and free hmWorker_'s result vectors
    // before/while onBakeDone() reads them from the main thread, corrupting the heap
    // (manifesting later as a stray std::bad_alloc).
    // Only delete the QThread once it has actually wound down (QThread::finished,
    // fired when its event loop really exits) — deleting it right after quit() is
    // merely requested races the real thread teardown and aborts the app
    // (QThread: Destroyed while thread is still running). This races much more
    // reliably on fast bakes (e.g. 128×128) where there's almost no delay.
    connect(hmThread_, &QThread::finished, hmThread_, &QObject::deleteLater);

    emit statusMessage(QString("Baking %1×%1…").arg(res));
    hmThread_->start();
}

void HeightmapModule::displayHeightmap(const HeightmapResult& r)
{
    if (!r.valid || r.image.empty())
    {
        hmPreview_->setText("(bake failed)");
        hmInfoLabel_->setText("Range: —");
        hmSaveBtn_->setEnabled(false);
        return;
    }

    QImage img(r.image.data(), r.width, r.height, r.width, QImage::Format_Grayscale8);
    img = img.mirrored(false, true);

    QPixmap px = QPixmap::fromImage(img);
    QSize labelSize = hmPreview_->size();
    if (labelSize.isEmpty())
        labelSize = QSize(300, 300);
    hmPreview_->setPixmap(
        px.scaled(labelSize, Qt::KeepAspectRatio, Qt::SmoothTransformation));

    hmInfoLabel_->setText(
        QString("Range: [%1, %2]")
            .arg((double)r.minH, 0, 'f', 4)
            .arg((double)r.maxH, 0, 'f', 4));

    hmSaveBtn_->setEnabled(true);
}

void HeightmapModule::setBakeButtonsEnabled(bool enabled)
{
    hmBakeBtn_->setEnabled(enabled);
}

void HeightmapModule::reset()
{
    hmResult_ = HeightmapResult{};
    hmPreview_->setText("(not baked)");
    hmPreview_->setPixmap(QPixmap());
    hmInfoLabel_->setText("Range: —");
    hmSaveBtn_->setEnabled(false);
}

#include "heightmap_module.moc"
