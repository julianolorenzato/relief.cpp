#pragma once
#include <QOpenGLWidget>
#include <QOpenGLFunctions_3_3_Core>
#include <QOpenGLBuffer>
#include <QOpenGLVertexArrayObject>
#include <QOpenGLShaderProgram>
#include <QPoint>
#include <glm/glm.hpp>
#include "qem.h"

// Renders two meshes overlaid: original (solid blue) + simplified (wireframe orange).
class OverlayGLWidget : public QOpenGLWidget, protected QOpenGLFunctions_3_3_Core {
    Q_OBJECT
public:
    explicit OverlayGLWidget(QWidget* parent = nullptr);
    ~OverlayGLWidget() override;

    void setMeshes(const QEMSimplifier* original, const QEMSimplifier* simplified);
    void updateSimplifiedMesh();
    void resetCamera();
    void syncCamera(float rotX, float rotY, float z);

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
    struct MeshBuf {
        QOpenGLBuffer vbo{QOpenGLBuffer::VertexBuffer};
        QOpenGLBuffer ebo{QOpenGLBuffer::IndexBuffer};
        QOpenGLVertexArrayObject vao;
        int indexCount = 0;
    };

    MeshBuf origBuf, simpBuf;
    const QEMSimplifier* origMesh = nullptr;
    const QEMSimplifier* simpMesh = nullptr;

    float rotX = 0.f, rotY = 0.f, zoom = 3.f;
    QPoint lastMouse;

    // Scene-level normalization derived from original mesh bounding box.
    glm::vec3 sceneCenter{0.f, 0.f, 0.f};
    float sceneScale = 1.f;

    QOpenGLShaderProgram shader;
    void createShader();
    void uploadMesh(const QEMSimplifier* m, MeshBuf& buf);
    void drawBuf(MeshBuf& buf, const QVector3D& color, bool wire);
};
