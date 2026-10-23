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
#include "relief/mesh.h"
#include "relief/textures.h"
#include "relief/uv_atlas.h"

/// @brief Dedicated OpenGL widget for relief mapping. Receives a mesh and renders it with
///        one of two shader programs:
///        - If the mesh carries baked planes (mesh::Mesh::planes), it is treated as the
///          bounding box and ray-marched against the 6 plane textures (relief.vert / relief.frag).
///        - Otherwise, with its baked maps (color/relief/normal/offset), the previous
///          mip-hierarchical relief mapping shader (old_relief.vert / old_relief.frag).
class ReliefView : public QOpenGLWidget, protected QOpenGLFunctions_3_3_Core
{
    Q_OBJECT

public:
    explicit ReliefView(QWidget *parent = nullptr);
    ~ReliefView() override;

    /// Sets the mesh to render (not owned). If the mesh has a height texture
    /// (mesh::Mesh::heightTexture), the color/relief/normal/offset maps are
    /// built from the mesh itself, replacing any previously uploaded ones.
    void setMesh(const mesh::Mesh *mesh);
    /// Uploads the relief-mapping color map.
    void setColorMap(const textures::MipPyramid& pyr);
    /// Uploads the relief-mapping depth/height map.
    void setReliefMap(const textures::MipPyramid& pyr);
    /// Uploads the relief-mapping normal map.
    void setNormalMap(const textures::MipPyramid& pyr);
    /// Uploads the cross-seam Offset_Map used to leap relief rays across UV islands.
    void setOffsetMap(const textures::MipPyramid& off);
    /// @return true once the textures of the active mode have been uploaded: color and the
    ///         6-plane array when rendering baked planes, else color/relief/normal/offset.
    bool hasTextures() const {
        return usePlanes ? (colorTex && planesTex) : (colorTex && reliefTex && normalTex && offsetTex);
    }

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
    /// Selects the 6-plane search: true for the mip-hierarchical one, false for uniform steps.
    void setMipSearch(bool v);
    void setReliefTextureType(int v);
    void setSteps(int v);
    void setDepthScale(double v);
    void setDebugView(int v);
    void setWireframe(bool v);
    void setCullFace(bool v);
    void setLightX(double v);
    void setLightY(double v);
    void setLightZ(double v);

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
    bool useMipSearch       = true;  // 6-plane shader only
    int  planeMipCount      = 1;     // Mip levels of planesTex
    int  steps        = 64;
    float depthScale  = 0.05f;
    int  debugView    = 0;

    // Camera (spherical coordinates)
    float rotX = 0.f, rotY = 0.f, zoom = 3.f;
    QPoint lastMouse;
    QPoint pressPos;
    QVector3D meshCenter{0.f, 0.f, 0.f};
    float meshNormScale = 1.f;

    // Lighting orb: a movable point light, visualized as a small unlit
    // sphere. Lives directly in the same post-model-matrix space as
    // FragPos/viewPosWorld, so no mesh normalization is applied to it.
    QVector3D lightPos{0.f, 2.f, 1.5f};

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
    const mesh::Mesh *mesh = nullptr;
    /// True when `mesh` has changed since GPU buffers were last uploaded, signaling paintGL() to rebuild them.
    bool meshDirty = false;

    /// True when `mesh` carries baked planes and is drawn with `prog` (the 6-plane shader)
    /// instead of `oldProg`.
    bool usePlanes = false;
    /// Bounds of `mesh` in its own space (the box that the 6 planes project onto).
    QVector3D boxMin{0.f, 0.f, 0.f};
    QVector3D boxMax{0.f, 0.f, 0.f};

    // OpenGL resources
    QOpenGLShaderProgram     prog;     ///< 6-plane relief shader (relief.vert / relief.frag).
    QOpenGLShaderProgram     oldProg;  ///< Previous relief shader (old_relief.vert / old_relief.frag).
    QOpenGLBuffer            vbo{QOpenGLBuffer::VertexBuffer};
    QOpenGLBuffer            ebo{QOpenGLBuffer::IndexBuffer};
    QOpenGLVertexArrayObject vao;
    int indexCount = 0;

    QOpenGLTexture *colorTex   = nullptr;
    QOpenGLTexture *reliefTex  = nullptr;
    QOpenGLTexture *normalTex  = nullptr;
    QOpenGLTexture *offsetTex  = nullptr;
    /// RGBA16 2D array of the 6 baked planes (layer = axis * 2 + (negative ? 1 : 0)).
    QOpenGLTexture *planesTex  = nullptr;

    // Lighting orb GL resources (fresnel-shaded sphere glyph, built once).
    QOpenGLShaderProgram     orbProg;
    QOpenGLBuffer            orbVbo{QOpenGLBuffer::VertexBuffer};
    QOpenGLBuffer            orbEbo{QOpenGLBuffer::IndexBuffer};
    QOpenGLVertexArrayObject orbVao;
    int orbIndexCount = 0;

    // Soft glow halo drawn as an additively-blended camera-facing billboard
    // behind the orb sphere.
    QOpenGLShaderProgram     haloProg;
    QOpenGLBuffer            haloVbo{QOpenGLBuffer::VertexBuffer};
    QOpenGLBuffer            haloEbo{QOpenGLBuffer::IndexBuffer};
    QOpenGLVertexArrayObject haloVao;
    int haloIndexCount = 0;

    /// Uploads the mesh's vertex/index buffers.
    void buildMeshBuffers();
    /// Builds and uploads all four relief maps from `mesh` itself, using its
    /// height texture for the relief map. Needs a current GL context.
    void buildMapsFromMesh();
    /// Builds and uploads the color map and the 6-plane array from `mesh`'s planes.
    /// Needs a current GL context.
    void buildPlanesFromMesh();
    /// Binds the shader program of the active mode, its uniforms and textures.
    /// @param debugView DebugView uniform value (e.g. 3 for the UV pick pass).
    /// @return The bound program, to release after drawing.
    QOpenGLShaderProgram *bindSurfaceProgram(int debugView);
    /// Builds and uploads the unit-radius sphere used to draw the light orb.
    void buildOrbMesh();
    /// Builds and uploads the unit quad used to draw the glow halo billboard.
    void buildHaloMesh();
    /// Uploads each of `pyr`'s mip levels into a newly (re)allocated `tex`
    /// (replacing whatever was there before, or leaving `tex` null if `pyr`
    /// is empty), with the given GL format and sampling filters.
    void uploadTexture(QOpenGLTexture *&tex, const textures::MipPyramid &pyr,
                        QOpenGLTexture::TextureFormat format,
                        QOpenGLTexture::PixelFormat pixelFormat,
                        QOpenGLTexture::Filter minFilter,
                        QOpenGLTexture::Filter magFilter);
    /// Deletes all owned GL texture objects.
    void deleteTextures();

    QMatrix4x4 viewMatrix() const;
    QMatrix4x4 modelMatrix() const;
    QMatrix4x4 projMatrix() const;
};
