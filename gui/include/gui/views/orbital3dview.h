/**
 * @file orbital3dview.h
 * @brief Abstract base for every orbital-camera OpenGL viewport in the app:
 *        orbit/zoom camera, camera syncing between linked viewports, mesh
 *        normalization and GPU buffers, a movable point light and its orb glyph.
 */
#pragma once
#include <QOpenGLWidget>
#include <QOpenGLFunctions_3_3_Core>
#include <QOpenGLBuffer>
#include <QOpenGLVertexArrayObject>
#include <QOpenGLShaderProgram>
#include <QOpenGLTexture>
#include <QMatrix4x4>
#include <QPoint>
#include <QString>
#include <glm/glm.hpp>
#include "relief/mesh.h"
#include "relief/textures.h"

class QLabel;

/**
 * @brief Abstract orbital-camera 3D viewport.
 *
 * Owns the camera, the normalized mesh and its interleaved GPU buffers, the point light
 * (with an optional orb glyph drawn at its position) and the wireframe/cull state.
 * Subclasses supply their shader programs and textures and draw the mesh in paintScene().
 *
 * The vertex layout is 12 floats: [pos(3) | normal(3) | uv(2) | tangent(4, w = handedness)],
 * stride 48 bytes.
 */
class Orbital3DView : public QOpenGLWidget, protected QOpenGLFunctions_3_3_Core
{
    Q_OBJECT

public:
    explicit Orbital3DView(QWidget *parent = nullptr);
    ~Orbital3DView() override;

    /// Sets the mesh to render (not owned) and resets the camera.
    void setMesh(const mesh::Mesh *mesh);

    /// Resets the orbit camera to its default position.
    void resetCamera();

    /// Applies external camera parameters (for syncing multiple linked viewports).
    void syncCamera(float rotX, float rotY, float zoom);

signals:
    /// Emitted after a mouse-driven camera change, so linked viewports can call syncCamera().
    void cameraChanged(float rotX, float rotY, float zoom);

public slots:
    void setWireframe(bool v);
    void setCullFace(bool v);
    /// Sets the world-space point light position (post-model-matrix space).
    void setLightX(double v);
    void setLightY(double v);
    void setLightZ(double v);

protected:
    void initializeGL() override;
    void resizeGL(int w, int h) override;
    void paintGL() override;
    void mousePressEvent(QMouseEvent *e) override;
    void mouseMoveEvent(QMouseEvent *e) override;
    void wheelEvent(QWheelEvent *e) override;
    void resizeEvent(QResizeEvent *e) override;

    /// Repositions the stats label above bottomOverlayHeight().
    void layoutStatsLabel();

    /// Compiles the subclass's own shader programs. Called once from initializeGL().
    virtual void initPrograms() = 0;
    /// Uploads the subclass's mesh-dependent resources (textures, edge buffers, ...). Called
    /// from paintGL() after the mesh buffers were rebuilt, with a current GL context.
    virtual void onMeshChanged() = 0;
    /// Draws the mesh. Called from paintGL() only when there is geometry and isReady().
    virtual void paintScene() = 0;

    /// Hook for other deferred uploads, run every frame after the mesh buffers are up to date.
    virtual void prepareFrame() {}
    /// Hook run at the very end of every frame that drew the scene (e.g. pixel picking).
    virtual void afterFrame() {}
    /// @return false while the subclass lacks the resources to draw; paintGL() then shows
    ///         notReadyMessage() instead of calling paintScene().
    virtual bool isReady() const { return true; }
    virtual QString notReadyMessage() const { return {}; }

    /// Builds the interleaved vertex array of `mesh` and uploads it to the given buffers.
    /// Used for the main mesh and by subclasses that draw additional meshes.
    void buildVertexArray(const mesh::Mesh *mesh, QOpenGLBuffer &vbo, QOpenGLBuffer &ebo,
                          QOpenGLVertexArrayObject &vao, int &indexCount);

    /// Uploads each of `pyr`'s mip levels into a newly (re)allocated `tex`
    /// (replacing whatever was there before, or leaving `tex` null if `pyr`
    /// is empty), with the given GL format and sampling filters.
    static void uploadTexture(QOpenGLTexture *&tex, const textures::MipPyramid &pyr,
                              QOpenGLTexture::TextureFormat format,
                              QOpenGLTexture::PixelFormat pixelFormat,
                              QOpenGLTexture::Filter minFilter,
                              QOpenGLTexture::Filter magFilter);

    /// @return The camera's world-space position.
    glm::vec3 cameraPosition() const;
    glm::mat4 viewMatrix() const;
    glm::mat4 modelMatrix() const;
    glm::mat4 projMatrix() const;
    /// Converts a column-major glm::mat4 to the equivalent QMatrix4x4.
    static QMatrix4x4 toQt(const glm::mat4 &m);

    /// Height of the stats label, which is pinned to the bottom edge of the view.
    static constexpr int kStatsHeight = 22;
    QLabel *statsLabel_ = nullptr;

    // Render state
    bool wireframe_ = false;
    bool cullFace_ = true;

    // Camera (spherical coordinates)
    float rotX_ = 0.f, rotY_ = 0.f, zoom_ = 3.f;
    QPoint lastMouse_;
    glm::vec3 meshCenter_{0.f, 0.f, 0.f};
    float meshNormScale_ = 1.f;

    /// Point light, in the same post-model-matrix space as the fragment's world position,
    /// so no mesh normalization applies to it.
    glm::vec3 lightPos_{0.f, 2.f, 1.5f};

    // Mesh (not owned)
    const mesh::Mesh *mesh_ = nullptr;
    /// True when mesh_ has changed since the GPU buffers were last uploaded.
    bool meshDirty_ = false;
    /// Bounds of mesh_ in its own (raw) space, valid after the buffers were built.
    glm::vec3 meshBoundsMin_{0.f, 0.f, 0.f};
    glm::vec3 meshBoundsMax_{0.f, 0.f, 0.f};

    QOpenGLBuffer meshVbo_{QOpenGLBuffer::VertexBuffer};
    QOpenGLBuffer meshEbo_{QOpenGLBuffer::IndexBuffer};
    QOpenGLVertexArrayObject meshVao_;
    int meshIndexCount_ = 0;

private:
    /// Refreshes the "N faces / N vertices" stats label from mesh_. Called by setMesh().
    void updateStatsLabel();
    /// Uploads mesh_'s vertex/index buffers and recomputes its bounds and normalization.
    /// @return false if there is nothing to draw.
    bool buildMeshBuffers();
    /// Builds and uploads the unit-radius sphere used to draw the light orb.
    void buildOrbMesh();
    /// Builds and uploads the unit quad used to draw the glow halo billboard.
    void buildHaloMesh();
    /// Links `prog` from the given shader resources, logging any error under `name`.
    static void buildProgram(QOpenGLShaderProgram &prog, const char *name,
                             const QString &vertPath, const QString &fragPath);
    /// Compiles the orb and halo programs.
    void initLightPrograms();
    /// Draws the light orb and its glow halo at lightPos_.
    void drawLightOrb();

    // Light orb: a fresnel-shaded sphere glyph, built once.
    QOpenGLShaderProgram orbProg_;
    QOpenGLBuffer orbVbo_{QOpenGLBuffer::VertexBuffer};
    QOpenGLBuffer orbEbo_{QOpenGLBuffer::IndexBuffer};
    QOpenGLVertexArrayObject orbVao_;
    int orbIndexCount_ = 0;

    // Soft glow halo: an additively-blended camera-facing billboard behind the orb.
    QOpenGLShaderProgram haloProg_;
    QOpenGLBuffer haloVbo_{QOpenGLBuffer::VertexBuffer};
    QOpenGLBuffer haloEbo_{QOpenGLBuffer::IndexBuffer};
    QOpenGLVertexArrayObject haloVao_;
    int haloIndexCount_ = 0;
};
