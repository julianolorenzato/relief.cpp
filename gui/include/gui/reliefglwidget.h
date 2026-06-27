#pragma once
#include <QOpenGLWidget>
#include <QOpenGLFunctions_3_3_Core>
#include <QOpenGLBuffer>
#include <QOpenGLVertexArrayObject>
#include <QOpenGLShaderProgram>
#include <QPoint>
#include <glm/glm.hpp>
#include "core/qem.h"
#include "gui/texture_prep.h"

// Renders a mesh with the RTMA_Functions.ush relief-mapping algorithm (GLSL port)
// applied in the fragment shader, using the maps baked by TexturePrepBaker.
class ReliefGLWidget : public QOpenGLWidget, protected QOpenGLFunctions_3_3_Core {
    Q_OBJECT

public:
    explicit ReliefGLWidget(QWidget* parent = nullptr);
    ~ReliefGLWidget() override;

    void setMesh(const QEMSimplifier* mesh);
    void setTextures(const TexturePrepResult& result);
    bool hasTextures() const { return colorTex != 0; }

    void resetCamera();

    // Aplicar estado de câmera vindo de outra viewport (sem re-emitir sinal)
    void syncCamera(float rotX, float rotY, float z);

public slots:
    void setWireframe(bool enabled);
    void setCullFace(bool enabled);
    void setReliefEnabled(bool enabled);
    void setUseAtlas(bool enabled);
    void setSteps(int steps);
    void setBinarySteps(int steps);
    void setDepthScale(double scale);
    void setDebugView(int mode);               // 0 Shaded, 1 Steps, 2 Leaps, 3 UV

signals:
    void cameraChanged(float rotX, float rotY, float z);

protected:
    void initializeGL() override;
    void resizeGL(int w, int h) override;
    void paintGL() override;

    void mousePressEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void wheelEvent(QWheelEvent* event) override;

private:
    const QEMSimplifier* mesh = nullptr;

    QOpenGLBuffer vbo{QOpenGLBuffer::VertexBuffer}, ebo{QOpenGLBuffer::IndexBuffer};
    QOpenGLVertexArrayObject vao;
    int indexCount = 0;

    QOpenGLShaderProgram shaderProgram;
    void createShaderProgram();
    void updateMeshBuffers();

    GLuint colorTex = 0, reliefTex = 0, normalTex = 0, offsetTex = 0;
    GLuint samplerPointId = 0;
    float lastMip = 0.0f;
    float texelSize = 1.0f;
    void uploadPyramid(GLuint& texId, const MipPyramid& pyr);
    void uploadOffsetMap(GLuint& texId, const OffsetMapResult& off);
    void deleteTextures();

    // setMesh()/setTextures() may be called while this widget has never been painted
    // yet (e.g. hidden behind a QStackedWidget placeholder), in which case its GL
    // context/function pointers aren't valid. Defer the actual GL upload work to the
    // start of paintGL(), which Qt only ever calls once the context is ready.
    bool meshDirty = false;
    bool texturesDirty = false;
    const TexturePrepResult* pendingTextures = nullptr;

    // Camera (independent from the other tabs' viewports — drag to rotate, scroll to zoom).
    QVector3D cameraUp{0, 1, 0};
    float rotationX = 0.0f, rotationY = 0.0f, zoom = 3.0f;
    QPoint lastMousePos;
    glm::vec3 meshCenter{0.0f, 0.0f, 0.0f};
    float meshNormScale = 1.0f;

    bool wireframe = false;
    bool cullFace  = true;

    // Relief-mapping parameters
    bool  reliefEnabled             = true;
    bool  useAtlas                 = true;
    int   steps                    = 64;
    int   binarySteps               = 5;
    float depthScale               = 0.05f;
    int   debugView                = 0;
};
