#include "texture_prep.h"
#include <QImage>
#include <QColor>
#include <QString>
#include <algorithm>
#include <cmath>

namespace {

QColor bilinearSample(const QImage& img, double u, double v) {
    double x = u * img.width()  - 0.5;
    double y = v * img.height() - 0.5;
    int x0 = (int)std::floor(x), y0 = (int)std::floor(y);
    int x1 = x0 + 1, y1 = y0 + 1;
    double fx = x - x0, fy = y - y0;

    auto cx = [&](int xx) { return std::clamp(xx, 0, img.width()  - 1); };
    auto cy = [&](int yy) { return std::clamp(yy, 0, img.height() - 1); };

    QColor c00 = img.pixelColor(cx(x0), cy(y0));
    QColor c10 = img.pixelColor(cx(x1), cy(y0));
    QColor c01 = img.pixelColor(cx(x0), cy(y1));
    QColor c11 = img.pixelColor(cx(x1), cy(y1));

    auto mix = [&](double a, double b, double c, double d) {
        double top = a + (b - a) * fx;
        double bot = c + (d - c) * fx;
        return top + (bot - top) * fy;
    };

    QColor out;
    out.setRgbF(
        mix(c00.redF(),   c10.redF(),   c01.redF(),   c11.redF()),
        mix(c00.greenF(), c10.greenF(), c01.greenF(), c11.greenF()),
        mix(c00.blueF(),  c10.blueF(),  c01.blueF(),  c11.blueF()),
        mix(c00.alphaF(), c10.alphaF(), c01.alphaF(), c11.alphaF()));
    return out;
}

std::vector<float> resampleColorRGBA(const QImage& imgIn, int outW, int outH) {
    QImage img = imgIn.convertToFormat(QImage::Format_RGBA8888);
    std::vector<float> out((size_t)outW * outH * 4);
    for (int y = 0; y < outH; y++) {
        for (int x = 0; x < outW; x++) {
            QColor c = bilinearSample(img, (x + 0.5) / outW, (y + 0.5) / outH);
            size_t idx = ((size_t)y * outW + x) * 4;
            out[idx + 0] = (float)c.redF();
            out[idx + 1] = (float)c.greenF();
            out[idx + 2] = (float)c.blueF();
            out[idx + 3] = (float)c.alphaF();
        }
    }
    return out;
}

std::vector<float> resampleDepthR(const QImage& imgIn, int outW, int outH) {
    QImage img = imgIn.convertToFormat(QImage::Format_Grayscale8);
    std::vector<float> out((size_t)outW * outH);
    for (int y = 0; y < outH; y++) {
        for (int x = 0; x < outW; x++) {
            QColor c = bilinearSample(img, (x + 0.5) / outW, (y + 0.5) / outH);
            out[(size_t)y * outW + x] = (float)c.redF();
        }
    }
    return out;
}

std::vector<float> resampleNormalXYZ(const QImage& imgIn, int outW, int outH) {
    QImage img = imgIn.convertToFormat(QImage::Format_RGB888);
    std::vector<float> out((size_t)outW * outH * 3);
    for (int y = 0; y < outH; y++) {
        for (int x = 0; x < outW; x++) {
            QColor c = bilinearSample(img, (x + 0.5) / outW, (y + 0.5) / outH);
            float nx = (float)c.redF()   * 2.f - 1.f;
            float ny = (float)c.greenF() * 2.f - 1.f;
            float nz = (float)c.blueF()  * 2.f - 1.f;
            float len = std::sqrt(nx * nx + ny * ny + nz * nz);
            if (len > 1e-8f) { nx /= len; ny /= len; nz /= len; }
            else { nx = 0.f; ny = 0.f; nz = 1.f; }
            size_t idx = ((size_t)y * outW + x) * 3;
            out[idx + 0] = nx;
            out[idx + 1] = ny;
            out[idx + 2] = nz;
        }
    }
    return out;
}

// ─── Mip downsampling ──────────────────────────────────────────────────────

std::vector<float> downsampleAvg(const std::vector<float>& src, int w, int h, int channels, int& outW, int& outH) {
    outW = std::max(1, w / 2);
    outH = std::max(1, h / 2);
    std::vector<float> dst((size_t)outW * outH * channels, 0.0f);
    for (int y = 0; y < outH; y++) {
        for (int x = 0; x < outW; x++) {
            int sx0 = std::min(w - 1, x * 2), sx1 = std::min(w - 1, x * 2 + 1);
            int sy0 = std::min(h - 1, y * 2), sy1 = std::min(h - 1, y * 2 + 1);
            for (int c = 0; c < channels; c++) {
                float sum = src[((size_t)sy0 * w + sx0) * channels + c]
                          + src[((size_t)sy0 * w + sx1) * channels + c]
                          + src[((size_t)sy1 * w + sx0) * channels + c]
                          + src[((size_t)sy1 * w + sx1) * channels + c];
                dst[((size_t)y * outW + x) * channels + c] = sum * 0.25f;
            }
        }
    }
    return dst;
}

// R(avg)/G(max)/B(passthrough 0)/A(max) mixed downsample for the Relief Map.
std::vector<float> downsampleReliefMixed(const std::vector<float>& src, int w, int h, int& outW, int& outH) {
    outW = std::max(1, w / 2);
    outH = std::max(1, h / 2);
    std::vector<float> dst((size_t)outW * outH * 4, 0.0f);
    for (int y = 0; y < outH; y++) {
        for (int x = 0; x < outW; x++) {
            int sx0 = std::min(w - 1, x * 2), sx1 = std::min(w - 1, x * 2 + 1);
            int sy0 = std::min(h - 1, y * 2), sy1 = std::min(h - 1, y * 2 + 1);
            size_t i00 = ((size_t)sy0 * w + sx0) * 4, i10 = ((size_t)sy0 * w + sx1) * 4;
            size_t i01 = ((size_t)sy1 * w + sx0) * 4, i11 = ((size_t)sy1 * w + sx1) * 4;

            float rAvg = (src[i00 + 0] + src[i10 + 0] + src[i01 + 0] + src[i11 + 0]) * 0.25f;
            float gMax = std::max(std::max(src[i00 + 1], src[i10 + 1]), std::max(src[i01 + 1], src[i11 + 1]));
            float aMax = std::max(std::max(src[i00 + 3], src[i10 + 3]), std::max(src[i01 + 3], src[i11 + 3]));

            size_t di = ((size_t)y * outW + x) * 4;
            dst[di + 0] = rAvg;
            dst[di + 1] = gMax;
            dst[di + 2] = 0.0f;
            dst[di + 3] = aMax;
        }
    }
    return dst;
}

MipPyramid buildPyramid(const std::vector<float>& mip0, int w, int h, int channels, bool renormalizeAsNormal) {
    MipPyramid pyr;
    pyr.width = w;
    pyr.height = h;
    pyr.channels = channels;
    pyr.mips.push_back(mip0);

    std::vector<float> cur = mip0;
    int cw = w, ch = h;
    while (cw > 1 || ch > 1) {
        int nw, nh;
        std::vector<float> next = downsampleAvg(cur, cw, ch, channels, nw, nh);
        if (renormalizeAsNormal) {
            for (size_t i = 0; i + 2 < next.size(); i += channels) {
                float x = next[i], y = next[i + 1], z = next[i + 2];
                float len = std::sqrt(x * x + y * y + z * z);
                if (len > 1e-8f) { next[i] = x / len; next[i + 1] = y / len; next[i + 2] = z / len; }
            }
        }
        pyr.mips.push_back(next);
        cur = next; cw = nw; ch = nh;
    }
    return pyr;
}

MipPyramid buildReliefPyramid(const std::vector<float>& mip0, int w, int h) {
    MipPyramid pyr;
    pyr.width = w;
    pyr.height = h;
    pyr.channels = 4;
    pyr.mips.push_back(mip0);

    std::vector<float> cur = mip0;
    int cw = w, ch = h;
    while (cw > 1 || ch > 1) {
        int nw, nh;
        std::vector<float> next = downsampleReliefMixed(cur, cw, ch, nw, nh);
        pyr.mips.push_back(next);
        cur = next; cw = nw; ch = nh;
    }
    return pyr;
}

} // namespace

TexturePrepResult TexturePrepBaker::bake(
    const QEMSimplifier& mesh,
    const std::string& colorPath,
    const std::string& depthPath,
    const std::string& normalPath,
    int workRes,
    int seamBandTexels,
    TexturePrepProgressCb cb) {
    TexturePrepResult result;
    auto progress = [&](int p) { if (cb) cb(p); };

    QImage colorImg(QString::fromStdString(colorPath));
    QImage depthImg(QString::fromStdString(depthPath));
    QImage normalImg(QString::fromStdString(normalPath));
    if (colorImg.isNull() || depthImg.isNull() || normalImg.isNull()) {
        result.valid = false;
        return result;
    }
    progress(5);

    std::vector<float> colorMip0 = resampleColorRGBA(colorImg, workRes, workRes);
    progress(15);
    std::vector<float> depthMip0 = resampleDepthR(depthImg, workRes, workRes);
    progress(25);
    std::vector<float> normalMip0 = resampleNormalXYZ(normalImg, workRes, workRes);
    progress(35);

    std::vector<int> faceIsland = UVAtlas::detectIslands(mesh);
    progress(45);
    result.offsetMap = UVAtlas::bakeOffsetMap(mesh, faceIsland, workRes, workRes, seamBandTexels);
    progress(55);

    // Relief map mip0: R=depth, G=depth (min==max bound at finest level), B=reserved, A=seam discriminant.
    std::vector<float> reliefMip0((size_t)workRes * workRes * 4);
    for (size_t i = 0; i < (size_t)workRes * workRes; i++) {
        reliefMip0[i * 4 + 0] = depthMip0[i];
        reliefMip0[i * 4 + 1] = depthMip0[i];
        reliefMip0[i * 4 + 2] = 0.0f;
        reliefMip0[i * 4 + 3] = result.offsetMap.data[i * 4 + 3];
    }

    result.reliefMap = buildReliefPyramid(reliefMip0, workRes, workRes);
    progress(75);
    result.normalMap = buildPyramid(normalMip0, workRes, workRes, 3, /*renormalizeAsNormal=*/true);
    progress(90);
    result.colorMap = buildPyramid(colorMip0, workRes, workRes, 4, /*renormalizeAsNormal=*/false);
    progress(100);

    result.valid = true;
    return result;
}
