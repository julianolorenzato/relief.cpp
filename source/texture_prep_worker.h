#pragma once
#include <QObject>
#include <QString>
#include <string>
#include "texture_prep.h"
#include "qem.h"

// Bakes the Textures Preparation pipeline (Color/Relief/Normal/Offset maps) on a
// worker thread. Set inputs before moving to thread; read `result` only after
// finished() is received in the main thread. CPU-only — GL texture upload of the
// resulting mip pyramids must happen on the main/GL thread.
class TexturePrepWorker : public QObject {
    Q_OBJECT

public:
    // ── Inputs — set before moving to thread ─────────────────────────────────
    const QEMSimplifier* mesh = nullptr;
    std::string colorPath, depthPath, normalPath;
    int workRes        = 512;
    int seamBandTexels = 4;

    // ── Output — read after finished() is received in the main thread ────────
    TexturePrepResult result;

signals:
    void progress(int overall, const QString& text);
    void finished();

public slots:
    void run() {
        emit progress(0, "Baking textures…");

        auto cb = [this](int pct) {
            emit progress(pct, "Baking textures…");
        };

        result = TexturePrepBaker::bake(*mesh, colorPath, depthPath, normalPath, workRes, seamBandTexels, cb);

        emit progress(100, result.valid ? "Done" : "Failed to load input textures");
        emit finished();
    }
};
