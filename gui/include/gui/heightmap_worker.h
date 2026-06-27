#pragma once
#include <QObject>
#include <QList>
#include "relief/heightmap.h"
#include "relief/qem.h"

// Runs the UV-correspondence baking strategy on a worker thread.
// Read results[0] only after finished() is received in the main thread.
class HeightmapWorker : public QObject {
    Q_OBJECT

public:
    // ── Inputs — set before moving to thread ─────────────────────────────────
    const QEMSimplifier* simplified = nullptr;
    const QEMSimplifier* original   = nullptr;
    int        width      = 512;
    int        height     = 512;

    // ── Outputs — read after finished() is received in the main thread ───────
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
