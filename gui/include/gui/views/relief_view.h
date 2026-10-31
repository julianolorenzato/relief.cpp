/**
 * @file relief_view.h
 * @brief Orbital viewport that ray-marches a bounding box against 6 baked plane textures
 *        (depth, color and normal per plane).
 */
#pragma once
#include <QOpenGLTexture>
#include <QVector3D>
#include <array>
#include <optional>
#include "gui/views/orbital3dview.h"
#include "relief/tex.h"

/**
 * @brief Renders a mesh carrying baked planes (mesh::Mesh::depthPlanes, colorPlanes and
 *        normalPlanes) as its bounding box, ray-marched against them (relief.vert /
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

    QOpenGLShaderProgram prog;  ///< 6-plane relief shader (relief.vert / relief.frag).
    /// R16 2D array of the 6 baked depth planes (layer = axis * 2 + (negative ? 1 : 0)).
    QOpenGLTexture *depthTex = nullptr;
    /// RGBA8 2D array of the 6 baked color planes (same layers as depthTex).
    QOpenGLTexture *colorTex = nullptr;
    /// RG8 2D array of the 6 baked octahedral-normal planes (same layers as depthTex).
    QOpenGLTexture *normalTex = nullptr;

    /**
     * @brief Builds a 6-layer 2D array texture from baked planes.
     * @param planes The 6 planes, all the same size and format.
     * @param filter Min/mag filter: Nearest for depth (it must not blend across the silhouette),
     *        Linear for color and normals.
     * @return The texture, or nullptr if the planes are missing or empty.
     *         Needs a current GL context.
     */
    QOpenGLTexture *uploadPlanes(const std::array<std::optional<tex::Texture>, 6> &planes,
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
