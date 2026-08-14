/**
 * @file orbital3dview.h
 * @brief Unified orbital-camera OpenGL viewport used across the app's
 *        pipeline stages (solid/textured mesh, overlay comparison, with an
 *        optional UV-wireframe toggle over Solid/Textured).
 */
#pragma once
#include <QOpenGLWidget>
#include <QOpenGLFunctions_3_3_Core>
#include <QOpenGLBuffer>
#include <QOpenGLVertexArrayObject>
#include <QOpenGLShaderProgram>
#include <QColor>
#include <QLabel>
#include <QPushButton>
#include <QPoint>
#include <glm/glm.hpp>
#include "relief/qem.h"

/// Selects what Orbital3DView renders and which shader/buffers it uses.
enum class RenderMode
{
    Solid,
    Textured,
    Overlay
};

/**
 * @brief Unified orbital-camera 3D viewport. Replaces GLWidget,
 *        OverlayGLWidget, and ReliefGLWidget with a single configurable
 *        widget. Vertex layout is 12 floats:
 *        [pos(3) | normal(3) | uv(2) | tangent(4, w = handedness)], stride 48 bytes.
 */
class Orbital3DView : public QOpenGLWidget, protected QOpenGLFunctions_3_3_Core
{
    Q_OBJECT

public:
    explicit Orbital3DView(RenderMode mode = RenderMode::Solid, const QString &title = {}, QWidget *parent = nullptr);
    ~Orbital3DView() override;

    /// Switches render mode (and rebuilds the color-swatch row visibility for Overlay).
    void setMode(RenderMode mode);

    /// Sets the title label shown above the viewport.
    void setTitle(const QString &title);

    /// Updates the "N faces / N vertices" stats label.
    void setStats(int faces, int vertices);

    /// Sets the mesh for single-mesh modes (Solid, Textured).
    void setMesh(const QEMSimplifier *mesh);

    /// Sets both meshes for Overlay mode (primary = blue, secondary = orange).
    void setMeshes(const QEMSimplifier *primary, const QEMSimplifier *secondary);

    /// Re-uploads the primary mesh's vertex data (e.g. after inflate/deflate) without resetting the camera.
    void updateMeshData();

    /// Re-uploads the secondary mesh's vertex data only (Overlay mode).
    void updateSecondaryMesh();

    /// Resets the orbit camera to its default position.
    void resetCamera();

    /// Applies external camera parameters (for syncing multiple linked viewports).
    void syncCamera(float rotX, float rotY, float z);

signals:
    /// Emitted after a mouse-driven camera change, so linked viewports can call syncCamera().
    void cameraChanged(float rotX, float rotY, float z);

public slots:
    void setWireframe(bool);
    void setCullFace(bool);
    void setTextured(bool);
    void setUVMode(bool);
    void setShowBoundaryEdges(bool);
    void setShowInternalEdges(bool);
    void setPrimaryColor(const QColor &c);
    void setSecondaryColor(const QColor &c);

protected:
    void initializeGL() override;
    void resizeGL(int w, int h) override;
    void paintGL() override;
    void resizeEvent(QResizeEvent *e) override;
    void mousePressEvent(QMouseEvent *) override;
    void mouseMoveEvent(QMouseEvent *) override;
    void wheelEvent(QWheelEvent *) override;

private:
    RenderMode mode_;
    QLabel *titleLabel_ = nullptr;
    QLabel *statsLabel_ = nullptr;
    QWidget *colorRow_ = nullptr;
    QPushButton *primaryColorBtn_ = nullptr;
    QPushButton *secondaryColorBtn_ = nullptr;

    QColor primaryColor_{89, 140, 242};
    QColor secondaryColor_{242, 127, 25};

    void createColorRow();
    void applyColorBtnStyle(QPushButton *btn, const QColor &c);

    // Render options
    bool wireframe_ = false;
    bool cullFace_ = true;
    bool textured_ = false;
    bool uvMode_ = false;
    bool showBoundary_ = false;
    bool showInternal_ = false;

    // Camera (spherical coordinates)
    float rotX_ = 0.f, rotY_ = 0.f, zoom_ = 3.f;
    QPoint lastMouse_;
    glm::vec3 meshCenter_{0.f, 0.f, 0.f};
    float meshNormScale_ = 1.f;

    // Mesh pointers (not owned)
    const QEMSimplifier *primaryMesh_ = nullptr;
    const QEMSimplifier *secondaryMesh_ = nullptr;

    // Deferred upload flags — all GL work happens at the start of paintGL()
    bool primaryMeshDirty_ = false;
    bool secondaryMeshDirty_ = false;

    // Shader programs
    QOpenGLShaderProgram solidProg_;
    QOpenGLShaderProgram overlayProg_;
    QOpenGLShaderProgram edgeProg_;
    QOpenGLShaderProgram uvBgProg_;
    QOpenGLShaderProgram uvLineProg_;

    // Primary mesh VAO/VBO/EBO (Solid, Textured, UV)
    QOpenGLBuffer primaryVbo_{QOpenGLBuffer::VertexBuffer};
    QOpenGLBuffer primaryEbo_{QOpenGLBuffer::IndexBuffer};
    QOpenGLVertexArrayObject primaryVao_;
    int primaryIndexCount_ = 0;

    // Secondary mesh VAO/VBO/EBO (Overlay only)
    QOpenGLBuffer secondaryVbo_{QOpenGLBuffer::VertexBuffer};
    QOpenGLBuffer secondaryEbo_{QOpenGLBuffer::IndexBuffer};
    QOpenGLVertexArrayObject secondaryVao_;
    int secondaryIndexCount_ = 0;

    // Edge overlay (Solid/Textured): 6 floats per vertex [pos(3) | color(3)]
    QOpenGLBuffer edgeVbo_{QOpenGLBuffer::VertexBuffer};
    QOpenGLVertexArrayObject edgeVao_;
    int edgeVertexCount_ = 0;
    int boundaryEdgeEnd_ = 0;

    // UV wireframe (UV mode): 2D UV positions sharing primaryEbo_
    QOpenGLBuffer uvVbo_{QOpenGLBuffer::VertexBuffer};
    QOpenGLVertexArrayObject uvVao_;
    // UV background quad
    QOpenGLBuffer uvBgVbo_{QOpenGLBuffer::VertexBuffer};
    QOpenGLVertexArrayObject uvBgVao_;

    // GL texture objects
    GLuint colorTex_ = 0;

    /// Compiles/links all shader programs used by the different render modes.
    void createShaders();
    void buildPrimaryBuffers();   ///< Uploads the primary mesh; recomputes meshCenter_/meshNormScale_.
    void buildSecondaryBuffers(); ///< Uploads the secondary mesh; reuses existing meshCenter_/meshNormScale_.
    /// Rebuilds the boundary/internal edge overlay buffer from the primary mesh.
    void buildEdgeBuffers();
    /// Rebuilds the UV-space wireframe buffer from the primary mesh.
    void buildUVBuffers();
    /// Uploads the primary mesh's embedded texture as colorTex_.
    void uploadColorFromMesh();
    /// Deletes all owned GL texture objects.
    void deleteTextures();

    glm::mat4 viewMatrix() const;
    glm::mat4 modelMatrix() const;
    glm::mat4 projMatrix() const;

    /// Draws Solid/Textured mode (and the edge overlay, if enabled).
    void paintSolid();
    /// Draws Overlay mode (primary + secondary mesh, edge overlay).
    void paintOverlay();
    /// Draws UV mode (UV-space wireframe over a checker background).
    void paintUV();
};
