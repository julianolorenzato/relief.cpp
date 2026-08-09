/**
 * @file relief_view.h
 * @brief Dedicated OpenGL widget for relief mapping, including offscreen
 *        pixel picking to resolve a screen click to a texture UV.
 */
#pragma once
#include <QOpenGLWidget>
#include <QOpenGLFunctions_3_3_Core>
#include <QOpenGLBuffer>
#include <QOpenGLVertexArrayObject>
#include <QOpenGLShaderProgram>
#include <QOpenGLTexture>
#include <QPoint>
#include <QVector3D>
#include <QMatrix4x4>
#include "relief/qem.h"
#include "relief/textures.h"
#include "relief/uv_atlas.h"

/// @brief Dedicated OpenGL widget for relief mapping. Receives a simplified
///        mesh and its baked maps (color/relief/normal/offset), and renders
///        with the mip-hierarchical relief mapping shader (relief.vert / relief.frag).
class ReliefView : public QOpenGLWidget, protected QOpenGLFunctions_3_3_Core
{
    Q_OBJECT

public:
    explicit ReliefView(QWidget *parent = nullptr);
    ~ReliefView() override;

    /// Sets the mesh to render (not owned).
    void setMesh(const QEMSimplifier *mesh);
    /// Uploads the relief-mapping color map.
    void setColorMap(const MipPyramid& pyr);
    /// Uploads the relief-mapping depth/height map.
    void setReliefMap(const MipPyramid& pyr);
    /// Uploads the relief-mapping normal map.
    void setNormalMap(const MipPyramid& pyr);
    /// Uploads the cross-seam Offset_Map used to leap relief rays across UV islands.
    void setOffsetMap(const OffsetMapResult& off);
    /// @return true once all four textures (color/relief/normal/offset) have been uploaded.
    bool hasTextures() const { return colorTex && reliefTex && normalTex && offsetTex; }

    /// Resets the orbit camera to its default position.
    void resetCamera();
    /// Applies external camera parameters (for syncing with a linked viewport).
    void syncCamera(float rotX, float rotY, float zoom);

signals:
    /// Emitted after a mouse-driven camera change, so a linked viewport can call syncCamera().
    void cameraChanged(float rotX, float rotY, float zoom);
    /// Emitted after a click is resolved to a texture UV (see performPick).
    /// hit is false if the click didn't land on any mesh geometry.
    void pixelPicked(QPointF uv, bool hit);

public slots:
    void setReliefEnabled(bool v);
    void setUseAtlas(bool v);
    void setReliefTextureType(int v);
    void setSteps(int v);
    void setDepthScale(double v);
    void setDebugView(int v);
    void setWireframe(bool v);
    void setCullFace(bool v);

protected:
    void initializeGL() override;
    void resizeGL(int w, int h) override;
    void paintGL() override;
    void mousePressEvent(QMouseEvent *e) override;
    void mouseReleaseEvent(QMouseEvent *e) override;
    void mouseMoveEvent(QMouseEvent *e) override;
    void wheelEvent(QWheelEvent *e) override;

private:
    // Render state
    bool wireframe    = false;
    bool cullFace     = true;
    bool reliefEnabled      = true;
    bool useAtlas           = true;
    int  reliefTextureType  = 0;  // 0 = depth map, 1 = height map
    int  steps        = 64;
    float depthScale  = 0.05f;
    int  debugView    = 0;

    // Camera (spherical coordinates)
    float rotX = 0.f, rotY = 0.f, zoom = 3.f;
    QPoint lastMouse;
    QPoint pressPos;
    QVector3D meshCenter{0.f, 0.f, 0.f};
    float meshNormScale = 1.f;

    // Pixel picking: a click queues a widget-space position here; paintGL
    // resolves it after the normal (visible) draw so picking never disturbs
    // the on-screen frame.
    bool pickPending = false;
    QPoint pickPos;
    GLuint pickFbo = 0, pickColorTex = 0, pickDepthRbo = 0;
    int pickFboW = 0, pickFboH = 0;
    /// (Re)creates the offscreen pick FBO/textures at size w x h if the size changed.
    void ensurePickFbo(int w, int h);
    /// Deletes the offscreen pick FBO/textures.
    void deletePickFbo();
    /// Renders a UV-encoding pass into the pick FBO and reads back the texel at `widgetPos`, emitting pixelPicked.
    void performPick(const QPoint &widgetPos);

    // Mesh (not owned)
    const QEMSimplifier *mesh = nullptr;

    // OpenGL resources
    QOpenGLShaderProgram     prog;
    QOpenGLBuffer            vbo{QOpenGLBuffer::VertexBuffer};
    QOpenGLBuffer            ebo{QOpenGLBuffer::IndexBuffer};
    QOpenGLVertexArrayObject vao;
    int indexCount = 0;

    QOpenGLTexture *colorTex   = nullptr;
    QOpenGLTexture *reliefTex  = nullptr;
    QOpenGLTexture *normalTex  = nullptr;
    QOpenGLTexture *offsetTex  = nullptr;

    /// Uploads the mesh's vertex/index buffers.
    void buildMeshBuffers();
    /// Uploads `pyr` as a mip-mapped color texture into `tex`.
    void uploadColorMap(QOpenGLTexture *&tex, const MipPyramid &pyr);
    /// Uploads `pyr` as a mip-mapped normal texture into `tex`.
    void uploadNormalMap(QOpenGLTexture *&tex, const MipPyramid &pyr);
    /// Uploads `pyr` as a mip-mapped relief (depth/height) texture into `tex`.
    void uploadReliefMap(QOpenGLTexture *&tex, const MipPyramid &pyr);
    /// Uploads `off` as an RGBA32F offset-map texture into `tex`.
    void uploadOffsetMap(QOpenGLTexture *&tex, const OffsetMapResult &off);
    /// Deletes all owned GL texture objects.
    void deleteTextures();

    QMatrix4x4 viewMatrix() const;
    QMatrix4x4 modelMatrix() const;
    QMatrix4x4 projMatrix() const;
};
