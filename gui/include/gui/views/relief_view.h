/**
 * @file relief_view.h
 * @brief Orbital viewport that ray-marches a bounding box against the baked per-axis
 *        layered depth, color and normal textures.
 */
#pragma once
#include <QOpenGLTexture>
#include <QVector3D>
#include <array>
#include <optional>
#include "gui/views/orbital3dview.h"
#include "relief/tex.h"

/**
 * @brief Renders a mesh carrying baked layered views (mesh::Mesh::depthPlanes, colorPlanes
 *        and normalPlanes) as its bounding box, ray-marched against them (relief.vert /
 *        relief.frag).
 */
class ReliefView : public Orbital3DView
{
    Q_OBJECT

public:
    using Orbital3DView::Orbital3DView;
    ~ReliefView() override;

    /// @return true once the depth, color and normal plane arrays have been uploaded.
    bool hasTextures() const { return depthTex && colorTex && normalTex; }

public slots:
    void setReliefEnabled(bool v);
    void setSteps(int v);
    void setDebugView(int v);

protected:
    void initPrograms() override;
    void onMeshChanged() override;
    void paintScene() override;
    bool isReady() const override { return hasTextures(); }
    QString notReadyMessage() const override;

private:
    // Render state
    bool reliefEnabled = true;
    int  steps         = 64;
    int  debugView     = 0;

    QOpenGLShaderProgram prog;  ///< Layered-depth relief shader (relief.vert / relief.frag).
    /// RGBA16 2D array of the 3 baked depth planes (layer = axis), the hits in R, G, B, A.
    QOpenGLTexture *depthTex = nullptr;
    /// RGBA8 2D array of the baked color of each hit (layer = axis * kAxisHits + hit).
    QOpenGLTexture *colorTex = nullptr;
    /// RG8 2D array of the baked octahedral normal of each hit (same layers as colorTex).
    QOpenGLTexture *normalTex = nullptr;

    /**
     * @brief Builds a 2D array texture from baked planes.
     * @param planes The planes, one per layer, all the same size and format.
     * @param layers Number of planes.
     * @param filter Min/mag filter: Nearest for depth (it must not blend across the silhouette),
     *        Linear for color and normals.
     * @return The texture, or nullptr if the planes are missing or empty.
     *         Needs a current GL context.
     */
    QOpenGLTexture *uploadPlanes(const std::optional<tex::Texture> *planes, int layers,
                                 QOpenGLTexture::Filter filter);
    /// Uploads `mesh_`'s baked planes to `depthTex`, `colorTex` and `normalTex`.
    /// Needs a current GL context.
    void uploadPlanesTextures();
    /// Binds the shader program, its uniforms and textures.
    /// @param debugView DebugView uniform value.
    /// @return The bound program, to release after drawing.
    QOpenGLShaderProgram *bindSurfaceProgram(int debugView);
    /// Deletes all owned GL texture objects.
    void deleteTextures();
};
