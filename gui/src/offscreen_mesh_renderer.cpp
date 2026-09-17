/**
 * @file offscreen_mesh_renderer.cpp
 * @brief OffscreenMeshRenderer implementation: see offscreen_mesh_renderer.h.
 */
#include "gui/offscreen_mesh_renderer.h"

#include <array>
#include <tuple>
#include "gui/orbital3dview.h"

namespace {
/// (rotX, rotY, zoom) presets: front, back, right, left, top, bottom.
/// ±89° (not ±90°) avoids Orbital3DView's spherical-camera pole edge case.
const std::array<std::tuple<float, float, float>, 6> kCameraPresets = {{
    {0.f, 0.f, 3.f},
    {0.f, 180.f, 3.f},
    {0.f, 90.f, 3.f},
    {0.f, 270.f, 3.f},
    {89.f, 0.f, 3.f},
    {-89.f, 0.f, 3.f},
}};
} // namespace

OffscreenMeshRenderer::OffscreenMeshRenderer(int size) : view_(new Orbital3DView(RenderMode::Solid)) {
    // Realizes the widget's GL surface without ever showing a visible
    // window, so grabFramebuffer() works headlessly afterward.
    view_->setAttribute(Qt::WA_DontShowOnScreen, true);
    view_->resize(size, size);
    view_->show();
}

OffscreenMeshRenderer::~OffscreenMeshRenderer() {
    delete view_;
}

std::vector<QImage> OffscreenMeshRenderer::renderViews(const mesh::Mesh& mesh) {
    view_->setMesh(&mesh);

    std::vector<QImage> images;
    images.reserve(kCameraPresets.size());
    for (const auto& [rotX, rotY, zoom] : kCameraPresets) {
        view_->syncCamera(rotX, rotY, zoom);
        images.push_back(view_->grabFramebuffer().convertToFormat(QImage::Format_RGBA8888));
    }
    return images;
}
