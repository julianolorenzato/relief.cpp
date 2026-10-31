/**
 * @file relief_view.h
 * @brief Orbital viewport that ray-marches a bounding box against 6 baked plane textures.
 */
#pragma once
#include <QOpenGLTexture>
#include <QVector3D>
#include "gui/views/orbital3dview.h"

/**
 * @brief Renders a mesh carrying baked planes (mesh::Mesh::planes) as its bounding box,
 *        ray-marched against the 6 plane textures (relief.vert / relief.frag).
 */
class ReliefView : public Orbital3DView
{
    Q_OBJECT

public:
    using Orbital3DView::Orbital3DView;
    ~ReliefView() override;

    /// @return true once the color map and the 6-plane arrays have been uploaded.
    bool hasTextures() const { return colorTex && planesTex && planeNormalsTex; }

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
    QOpenGLTexture *colorTex = nullptr;
    /// RGBA16 2D array of the 6 baked planes (layer = axis * 2 + (negative ? 1 : 0)).
    QOpenGLTexture *planesTex = nullptr;
    /// RGBA16 2D array of the 6 baked smooth-normal planes (same layers as planesTex).
    QOpenGLTexture *planeNormalsTex = nullptr;

    /// Uploads `mesh_`'s color texture to `colorTex`.
    /// Needs a current GL context.
    /// @throws std::runtime_error if the mesh has no valid color texture.
    void uploadColorTexture();
    /// Builds and uploads the 6-plane arrays (`planesTex`, `planeNormalsTex`) from `mesh_`'s planes.
    /// Needs a current GL context.
    void uploadPlanesTextures();
    /// Binds the shader program, its uniforms and textures.
    /// @param debugView DebugView uniform value.
    /// @return The bound program, to release after drawing.
    QOpenGLShaderProgram *bindSurfaceProgram(int debugView);
    /// Deletes all owned GL texture objects.
    void deleteTextures();
};
