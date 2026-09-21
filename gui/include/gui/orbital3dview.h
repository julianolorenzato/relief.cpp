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
#include <set>
#include "relief/mesh.h"
#include "relief/mesh/edgesel.h"

/// Selects what Orbital3DView renders and which shader/buffers it uses.
enum class RenderMode
{
    Solid,
    Textured,
    Overlay
};

/// Selects what mouse-drag input does in the viewport.
enum class InteractionMode
{
    Orbit,       ///< Left-drag orbits the camera (default).
    BrushSelect, ///< Left/right-drag paints/erases the edge-selection brush.
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

    /// Sets the mesh for single-mesh modes (Solid, Textured).
    /// @param normalizationSource Mesh whose bounding sphere determines
    ///        meshCenter_/meshNormScale_ instead of `mesh`'s own, so that
    ///        multiple linked viewports stay at the same visual zoom even
    ///        when their meshes' bounding volumes diverge (e.g. after an
    ///        op that replaces one with a differently-sized bounding
    ///        volume). Defaults to `mesh` itself when null.
    void setMesh(const mesh::Mesh *mesh, const mesh::Mesh *normalizationSource = nullptr);

    /// Sets both meshes for Overlay mode (primary = blue, secondary = orange).
    /// @param normalizationSource See setMesh(); defaults to `primary` when null.
    void setMeshes(const mesh::Mesh *primary, const mesh::Mesh *secondary,
                    const mesh::Mesh *normalizationSource = nullptr);

    /// Resets the orbit camera to its default position.
    void resetCamera();

    /// Applies external camera parameters (for syncing multiple linked viewports).
    void syncCamera(float rotX, float rotY, float z);

    /// Switches between orbit-camera and brush-selection mouse handling.
    void setInteractionMode(InteractionMode m);
    InteractionMode interactionMode() const { return interactionMode_; }

    /// @return The current brush-selected edge set (vertex-id pairs, small-first).
    const std::set<mesh::Edge> &selectedEdges() const { return brushSelection_.edges(); }

signals:
    /// Emitted after a mouse-driven camera change, so linked viewports can call syncCamera().
    void cameraChanged(float rotX, float rotY, float z);
    /// Emitted whenever the brush selection changes (paint, erase, or clear).
    void selectionChanged(int edgeCount);

public slots:
    void setWireframe(bool);
    void setCullFace(bool);
    void setTextured(bool);
    void setUVMode(bool);
    void setShowInternalEdges(bool);
    void setShowSeamEdges(bool);
    void setPrimaryColor(const QColor &c);
    void setSecondaryColor(const QColor &c);
    /// Sets the world-space point light position used by Solid/Textured shading
    /// (matches ReliefView's setLightX/Y/Z so both view types share one light).
    void setLightX(double v);
    void setLightY(double v);
    void setLightZ(double v);

    /// Sets the brush radius, in normalized mesh space (bounding-sphere radius 1).
    void setBrushRadius(double normalizedRadius);
    /// Sets the max normal angle (degrees) the brush's flood fill may cross between faces.
    void setBrushAngleThresholdDeg(double degrees);
    /// Sets how the brush's flood fill decides normal similarity across faces.
    void setBrushPropagationMode(mesh::edgesel::PropagationMode mode);
    /// Clears the current brush edge selection.
    void clearBrushSelection();

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

    // Point light for Solid/Textured shading — default matches ReliefView::lightPos.
    glm::vec3 lightPos_{0.f, 2.f, 1.5f};

    void createColorRow();
    void applyColorBtnStyle(QPushButton *btn, const QColor &c);
    /// Refreshes the "N faces / N vertices" stats label from primaryMesh_. Called
    /// whenever the primary mesh pointer changes (setMesh()/setMeshes()).
    void updateStatsLabel();

    // Render options
    bool wireframe_ = false;
    bool cullFace_ = true;
    bool textured_ = false;
    bool uvMode_ = false;
    bool showInternal_ = false;
    bool showSeam_ = false;

    // Camera (spherical coordinates)
    float rotX_ = 0.f, rotY_ = 0.f, zoom_ = 3.f;
    QPoint lastMouse_;
    glm::vec3 meshCenter_{0.f, 0.f, 0.f};
    float meshNormScale_ = 1.f;

    // Brush selection
    InteractionMode interactionMode_ = InteractionMode::Orbit;
    double brushRadius_ = 0.05;
    double brushAngleThresholdDeg_ = 35.0;
    mesh::edgesel::PropagationMode brushPropagationMode_ = mesh::edgesel::PropagationMode::Chained;
    mesh::edgesel::FaceAdjacency brushAdjacency_;
    std::vector<Eigen::Vector3d> brushFaceNormals_;
    mesh::edgesel::BrushSelection brushSelection_;
    /// Set whenever the selection changes; the highlight VBO is only ever
    /// rebuilt (GL calls) from inside paintGL, where the context is current.
    bool highlightDirty_ = false;
    /// Ray, in raw (unnormalized) mesh space, for the given widget-space point.
    struct Ray { Eigen::Vector3d origin, dir; };
    Ray screenRay(const QPoint &p) const;
    /// Raycasts at `p` and, on hit, applies one brush touch (paint or erase).
    void brushTouchAt(const QPoint &p, bool erase);
    void rebuildHighlightBuffer();

    // Mesh pointers (not owned)
    const mesh::Mesh *primaryMesh_ = nullptr;
    const mesh::Mesh *secondaryMesh_ = nullptr;
    /// Mesh whose bounding sphere drives meshCenter_/meshNormScale_; falls
    /// back to primaryMesh_ when null. See setMesh()'s normalizationSource.
    const mesh::Mesh *normalizationMesh_ = nullptr;

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
    int seamEdgeEnd_ = 0;

    // Brush-selection highlight, same layout as edgeVbo_ above.
    QOpenGLBuffer highlightVbo_{QOpenGLBuffer::VertexBuffer};
    QOpenGLVertexArrayObject highlightVao_;
    int highlightVertexCount_ = 0;

    // UV wireframe (UV mode): 2D UV positions sharing primaryEbo_
    QOpenGLBuffer uvVbo_{QOpenGLBuffer::VertexBuffer};
    QOpenGLVertexArrayObject uvVao_;
    // UV background quad
    QOpenGLBuffer uvBgVbo_{QOpenGLBuffer::VertexBuffer};
    QOpenGLVertexArrayObject uvBgVao_;

    // GL texture objects
    GLuint colorTex_ = 0;
    GLuint normalTex_ = 0;

    /// Compiles/links all shader programs used by the different render modes.
    void createShaders();
    void buildPrimaryBuffers();   ///< Uploads the primary mesh; recomputes meshCenter_/meshNormScale_.
    void buildSecondaryBuffers(); ///< Uploads the secondary mesh; reuses existing meshCenter_/meshNormScale_.
    /// Rebuilds the seam/internal edge overlay buffer from the primary mesh.
    void buildEdgeBuffers();
    /// Rebuilds the UV-space wireframe buffer from the primary mesh.
    void buildUVBuffers();
    /// Uploads the primary mesh's embedded texture as colorTex_.
    void uploadColorFromMesh();
    /// Uploads the primary mesh's embedded normal map as normalTex_.
    void uploadNormalFromMesh();
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
