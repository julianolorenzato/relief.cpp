/**
 * @file offscreen_mesh_renderer.h
 * @brief Renders a mesh from several fixed camera angles into off-screen
 *        images, for use as PSNR comparison inputs (see relief/metrics.h).
 */
#pragma once
#include <QImage>
#include <vector>
#include "relief/mesh.h"

class Orbital3DView;

/// @brief Renders meshes off screen by reusing a single hidden Orbital3DView,
///        so PSNR scoring doesn't need a second rendering implementation.
class OffscreenMeshRenderer {
public:
    /// @param size Width/height, in pixels, of each rendered view.
    explicit OffscreenMeshRenderer(int size = 256);
    ~OffscreenMeshRenderer();

    /// @brief Renders `mesh` from 6 fixed camera angles (front, back, left,
    ///        right, top, bottom).
    /// @param mesh Mesh to render; must outlive the call (not stored after
    ///        it returns).
    /// @return One QImage (Format_RGBA8888) per camera angle, in the order above.
    std::vector<QImage> renderViews(const mesh::Mesh& mesh);

private:
    Orbital3DView* view_;
};
