#pragma once
#include <QObject>
#include <QList>
#include "heightmap.h"
#include "qem.h"

// Runs the requested baking strategies sequentially on a worker thread.
// Set 'strategies' to a subset of {0, 1} before starting:
//   0 = Ray Cast (no cage)
//   1 = Ray Cast with cage (uses cageOffset)
// Read results[] only after finished() is received in the main thread.
class HeightmapWorker : public QObject {
    Q_OBJECT

public:
    // ── Inputs — set before moving to thread ─────────────────────────────────
    const QEMSimplifier* simplified = nullptr;
    const QEMSimplifier* original   = nullptr;
    int        width      = 512;
    int        height     = 512;
    double     cageOffset = 0.05;
    QList<int> strategies = {0, 1};

    // ── Outputs — read after finished() is received in the main thread ───────
    HeightmapResult results[2];
    NormalmapResult nmResult;

signals:
    void progress(int overall, const QString& text);
    void finished();

public slots:
    void run() {
        static const char* names[3] = {"Ray Cast", "Ray Cast + Cage", "Normal Map"};

        int n = strategies.size();
        if (n == 0) { emit progress(100, "Done"); emit finished(); return; }

        for (int idx = 0; idx < n; idx++) {
            int s    = strategies[idx];
            int base = idx * 100 / n;
            int end  = (idx + 1) * 100 / n;

            QString label = QString("%1/%2: %3…").arg(idx + 1).arg(n).arg(names[s]);
            emit progress(base, label);

            auto cb = [this, base, end, label](int pct) {
                emit progress(base + pct * (end - base) / 100, label);
            };

            switch (s) {
                case 0: results[0] = HeightmapBaker::bakeRayCast(
                            *simplified, *original, width, height, cb); break;
                case 1: results[1] = HeightmapBaker::bakeRayCastCage(
                            *simplified, *original, width, height, cageOffset, cb); break;
                case 2: nmResult = HeightmapBaker::bakeNormalMap(
                            *simplified, *original, width, height, cb); break;
            }
        }

        emit progress(100, "Done");
        emit finished();
    }
};
