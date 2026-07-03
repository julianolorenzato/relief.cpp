#pragma once
#include <QOpenGLWidget>
#include <QOpenGLFunctions_3_3_Core>
#include <QOpenGLBuffer>
#include <QOpenGLVertexArrayObject>
#include <QOpenGLShaderProgram>
#include <QPoint>
#include <QVector3D>
#include <QMatrix4x4>
#include "relief/qem.h"
#include "relief/textures.h"
#include "relief/uv_atlas.h"

// Dedicated OpenGL widget for relief mapping.
// Receives a simplified mesh and a TexturePrepResult, renders with the
// mip-hierarchical relief mapping shader (relief.vert / relief.frag).
class ReliefView : public QOpenGLWidget, protected QOpenGLFunctions_3_3_Core
{
    Q_OBJECT

public:
    explicit ReliefView(QWidget *parent = nullptr);
    ~ReliefView() override;

    void setMesh(const QEMSimplifier *mesh);
    void setColorMap(const MipPyramid& pyr);
    void setReliefMap(const MipPyramid& pyr);
    void setNormalMap(const MipPyramid& pyr);
    void setOffsetMap(const OffsetMapResult& off);
    bool hasTextures() const { return colorTex != 0 && reliefTex != 0 && normalTex != 0 && offsetTex != 0; }

    void resetCamera();
    void syncCamera(float rotX, float rotY, float zoom);

signals:
    void cameraChanged(float rotX, float rotY, float zoom);

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
    QVector3D meshCenter{0.f, 0.f, 0.f};
    float meshNormScale = 1.f;

    // Mesh (not owned)
    const QEMSimplifier *mesh = nullptr;

    // Shader uniforms derived from texture resolution
    float lastMip   = 0.f;
    float texelSize = 1.f;

    // OpenGL resources
    QOpenGLShaderProgram     prog;
    QOpenGLBuffer            vbo{QOpenGLBuffer::VertexBuffer};
    QOpenGLBuffer            ebo{QOpenGLBuffer::IndexBuffer};
    QOpenGLVertexArrayObject vao;
    int indexCount = 0;

    GLuint colorTex    = 0;
    GLuint reliefTex   = 0;
    GLuint normalTex   = 0;
    GLuint offsetTex   = 0;
    GLuint samplerPoint = 0;

    void buildMeshBuffers();
    void uploadPyramid(GLuint &texId, const MipPyramid &pyr);
    void uploadOffsetMap(GLuint &texId, const OffsetMapResult &off);
    void deleteTextures();

    QMatrix4x4 viewMatrix() const;
    QMatrix4x4 modelMatrix() const;
    QMatrix4x4 projMatrix() const;
};
