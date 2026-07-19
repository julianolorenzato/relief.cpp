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
#include "relief/textures.h"
#include "relief/uv_atlas.h"

enum class RenderMode { Solid, Textured, Overlay, Relief, UV };

// Unified orbital-camera 3D viewport. Replaces GLWidget, OverlayGLWidget, and
// ReliefGLWidget with a single configurable widget. Vertex layout is 12 floats:
// [pos(3) | normal(3) | uv(2) | tangent(4, w = handedness)], stride 48 bytes.
class Orbital3DView : public QOpenGLWidget, protected QOpenGLFunctions_3_3_Core {
    Q_OBJECT

public:
    explicit Orbital3DView(RenderMode mode = RenderMode::Solid, const QString& title = {}, QWidget* parent = nullptr);
    ~Orbital3DView() override;

    void setMode(RenderMode mode);
    void setTitle(const QString& title);
    void setStats(int faces, int vertices);

    // Single-mesh modes (Solid, Textured, Relief, UV)
    void setMesh(const QEMSimplifier* mesh);
    // Two-mesh mode (Overlay: primary = blue, secondary = orange)
    void setMeshes(const QEMSimplifier* primary, const QEMSimplifier* secondary);
    // Live update of primary mesh data (inflate/deflate) — does not reset camera
    void updateMeshData();
    // Live update of secondary mesh only (Overlay mode)
    void updateSecondaryMesh();

    // Upload a float MipPyramid as color texture (overrides mesh's embedded texture)
    void setColorTexture(const MipPyramid& pyr);
    void setColorMap(const MipPyramid& pyr);
    void setReliefMap(const MipPyramid& pyr);
    void setNormalMap(const MipPyramid& pyr);
    void setOffsetMap(const OffsetMapResult& off);
    bool hasTextures() const { return colorTex_ != 0; }

    void resetCamera();
    void syncCamera(float rotX, float rotY, float z);

signals:
    void cameraChanged(float rotX, float rotY, float z);

public slots:
    void setWireframe(bool);
    void setCullFace(bool);
    void setTextured(bool);
    void setUVMode(bool);
    void setShowBoundaryEdges(bool);
    void setShowInternalEdges(bool);
    void setPrimaryColor(const QColor& c);
    void setSecondaryColor(const QColor& c);
    void setReliefEnabled(bool);
    void setUseAtlas(bool);
    void setSteps(int);
    void setBinarySteps(int);
    void setDepthScale(double);
    void setDebugView(int);

protected:
    void initializeGL() override;
    void resizeGL(int w, int h) override;
    void paintGL() override;
    void resizeEvent(QResizeEvent* e) override;
    void mousePressEvent(QMouseEvent*) override;
    void mouseMoveEvent(QMouseEvent*) override;
    void wheelEvent(QWheelEvent*) override;

private:
    RenderMode mode_;
    QLabel*       titleLabel_       = nullptr;
    QLabel*       statsLabel_       = nullptr;
    QWidget*      colorRow_         = nullptr;
    QPushButton*  primaryColorBtn_  = nullptr;
    QPushButton*  secondaryColorBtn_= nullptr;

    QColor primaryColor_   { 89, 140, 242 };
    QColor secondaryColor_ { 242, 127,  25 };

    void createColorRow();
    void applyColorBtnStyle(QPushButton* btn, const QColor& c);

    // Render options
    bool wireframe_     = false;
    bool cullFace_      = true;
    bool textured_      = false;
    bool uvMode_        = false;
    bool showBoundary_  = false;
    bool showInternal_  = false;
    bool reliefEnabled_ = true;
    bool useAtlas_      = true;
    int  steps_         = 64;
    int  binarySteps_   = 5;
    float depthScale_   = 0.05f;
    int  debugView_     = 0;

    // Camera (spherical coordinates)
    float rotX_ = 0.f, rotY_ = 0.f, zoom_ = 3.f;
    QPoint lastMouse_;
    glm::vec3 meshCenter_{0.f, 0.f, 0.f};
    float meshNormScale_ = 1.f;

    // Mesh pointers (not owned)
    const QEMSimplifier* primaryMesh_   = nullptr;
    const QEMSimplifier* secondaryMesh_ = nullptr;

    // Deferred upload flags — all GL work happens at the start of paintGL()
    bool primaryMeshDirty_   = false;
    bool secondaryMeshDirty_ = false;
    bool reliefTexDirty_     = false;
    bool colorPyrDirty_      = false;
    const MipPyramid*      pendingColorMap_  = nullptr;
    const MipPyramid*      pendingReliefMap_ = nullptr;
    const MipPyramid*      pendingNormalMap_ = nullptr;
    const OffsetMapResult* pendingOffsetMap_ = nullptr;
    const MipPyramid*        pendingColorPyr_ = nullptr;

    // Relief shader uniforms (precomputed from texture resolution)
    float lastMip_   = 0.f;
    float texelSize_ = 1.f;

    // Shader programs
    QOpenGLShaderProgram solidProg_;
    QOpenGLShaderProgram overlayProg_;
    QOpenGLShaderProgram edgeProg_;
    QOpenGLShaderProgram uvBgProg_;
    QOpenGLShaderProgram uvLineProg_;
    QOpenGLShaderProgram reliefProg_;

    // Primary mesh VAO/VBO/EBO (Solid, Textured, Relief, UV)
    QOpenGLBuffer            primaryVbo_{QOpenGLBuffer::VertexBuffer};
    QOpenGLBuffer            primaryEbo_{QOpenGLBuffer::IndexBuffer};
    QOpenGLVertexArrayObject primaryVao_;
    int primaryIndexCount_ = 0;

    // Secondary mesh VAO/VBO/EBO (Overlay only)
    QOpenGLBuffer            secondaryVbo_{QOpenGLBuffer::VertexBuffer};
    QOpenGLBuffer            secondaryEbo_{QOpenGLBuffer::IndexBuffer};
    QOpenGLVertexArrayObject secondaryVao_;
    int secondaryIndexCount_ = 0;

    // Edge overlay (Solid/Textured): 6 floats per vertex [pos(3) | color(3)]
    QOpenGLBuffer            edgeVbo_{QOpenGLBuffer::VertexBuffer};
    QOpenGLVertexArrayObject edgeVao_;
    int edgeVertexCount_ = 0;
    int boundaryEdgeEnd_ = 0;

    // UV wireframe (UV mode): 2D UV positions sharing primaryEbo_
    QOpenGLBuffer            uvVbo_{QOpenGLBuffer::VertexBuffer};
    QOpenGLVertexArrayObject uvVao_;
    // UV background quad
    QOpenGLBuffer            uvBgVbo_{QOpenGLBuffer::VertexBuffer};
    QOpenGLVertexArrayObject uvBgVao_;

    // GL texture objects
    GLuint colorTex_  = 0;
    GLuint reliefTex_ = 0;
    GLuint normalTex_ = 0;
    GLuint offsetTex_ = 0;
    GLuint samplerPoint_ = 0;

    void createShaders();
    void buildPrimaryBuffers();   // recomputes meshCenter_/meshNormScale_
    void buildSecondaryBuffers(); // uses existing meshCenter_/meshNormScale_
    void buildEdgeBuffers();
    void buildUVBuffers();
    void uploadColorFromMesh();
    void uploadPyramid(GLuint& texId, const MipPyramid& pyr);
    void uploadOffsetMap(GLuint& texId, const OffsetMapResult& off);
    void deleteTextures();

    glm::mat4 viewMatrix() const;
    glm::mat4 modelMatrix() const;
    glm::mat4 projMatrix() const;

    void paintSolid();
    void paintOverlay();
    void paintUV();
    void paintRelief();
};
