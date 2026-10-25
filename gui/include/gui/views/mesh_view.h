/**
 * @file mesh_view.h
 * @brief Orbital viewport for plain mesh display across the app's pipeline
 *        stages (solid/textured mesh, overlay comparison, with an optional
 *        UV-wireframe toggle over Solid/Textured, and edge-brush selection).
 */
#pragma once
#include <QColor>
#include <QLabel>
#include <QPushButton>
#include <glm/glm.hpp>
#include <set>
#include "gui/views/orbital3dview.h"
#include "relief/mesh.h"
#include "relief/mesh/edgesel.h"

/// Selects what MeshView renders and which shader/buffers it uses.
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
 * @brief Configurable mesh viewport (Solid, Textured, Overlay and UV modes, plus
 *        edge-brush selection) built on the Orbital3DView camera.
 */
class MeshView : public Orbital3DView
{
    Q_OBJECT

public:
    explicit MeshView(RenderMode mode = RenderMode::Solid, const QString &title = {}, QWidget *parent = nullptr);
    ~MeshView() override;

    /// Switches render mode (and rebuilds the color-swatch row visibility for Overlay).
    void setMode(RenderMode mode);

    /// Sets the title label shown above the viewport.
    void setTitle(const QString &title);

    /// Sets both meshes for Overlay mode (primary = blue, secondary = orange).
    void setMeshes(const mesh::Mesh *primary, const mesh::Mesh *secondary);

    /// Switches between orbit-camera and brush-selection mouse handling.
    void setInteractionMode(InteractionMode m);
    InteractionMode interactionMode() const { return interactionMode_; }

    /// @return The current brush-selected edge set (vertex-id pairs, small-first).
    const std::set<mesh::Edge> &selectedEdges() const { return brushSelection_.edges(); }

signals:
    /// Emitted whenever the brush selection changes (paint, erase, or clear).
    void selectionChanged(int edgeCount);

public slots:
    void setTextured(bool);
    void setUVMode(bool);
    void setShowInternalEdges(bool);
    void setShowSeamEdges(bool);
    void setPrimaryColor(const QColor &c);
    void setSecondaryColor(const QColor &c);
    /// Sets the brush radius, in normalized mesh space (bounding-sphere radius 1).
    void setBrushRadius(double normalizedRadius);
    /// Sets the max normal angle (degrees) the brush's flood fill may cross between faces.
    void setBrushAngleThresholdDeg(double degrees);
    /// Sets how the brush's flood fill decides normal similarity across faces.
    void setBrushPropagationMode(mesh::edgesel::PropagationMode mode);
    /// Clears the current brush edge selection.
    void clearBrushSelection();

protected:
    void initPrograms() override;
    void onMeshChanged() override;
    void onMeshAssigned() override;
    void prepareFrame() override;
    void paintScene() override;
    void resizeEvent(QResizeEvent *e) override;
    void mousePressEvent(QMouseEvent *) override;
    void mouseMoveEvent(QMouseEvent *) override;

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
    /// Refreshes the "N faces / N vertices" stats label from mesh_. Called
    /// whenever the primary mesh pointer changes (setMesh()/setMeshes()).
    void updateStatsLabel();

    // Render options
    bool textured_ = false;
    bool uvMode_ = false;
    bool showInternal_ = false;
    bool showSeam_ = false;

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

    // Secondary mesh (not owned). The primary mesh is the base class's mesh_.
    const mesh::Mesh *secondaryMesh_ = nullptr;

    // Deferred upload flag — all GL work happens at the start of paintGL()
    bool secondaryMeshDirty_ = false;

    // Shader programs
    QOpenGLShaderProgram solidProg_;
    QOpenGLShaderProgram overlayProg_;
    QOpenGLShaderProgram edgeProg_;
    QOpenGLShaderProgram uvBgProg_;
    QOpenGLShaderProgram uvLineProg_;

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

    // UV wireframe (UV mode): 2D UV positions sharing meshEbo_
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
    void buildSecondaryBuffers(); ///< Uploads the secondary mesh; reuses the primary's meshCenter_/meshNormScale_.
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

    /// Draws Solid/Textured mode (and the edge overlay, if enabled).
    void paintSolid();
    /// Draws Overlay mode (primary + secondary mesh, edge overlay).
    void paintOverlay();
    /// Draws UV mode (UV-space wireframe over a checker background).
    void paintUV();
};
