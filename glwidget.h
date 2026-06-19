#pragma once
#include <QOpenGLWidget>
#include <QOpenGLFunctions_3_3_Core>
#include <QOpenGLBuffer>
#include <QOpenGLVertexArrayObject>
#include <QOpenGLShaderProgram>
#include <QMatrix4x4>
#include <QVector3D>
#include <memory>
#include <vector>
#include <glm/glm.hpp>
#include "qem.h"
#include "texture_prep.h"

class GLWidget : public QOpenGLWidget, protected QOpenGLFunctions_3_3_Core {
    Q_OBJECT

public:
    explicit GLWidget(QWidget* parent = nullptr);
    ~GLWidget() override;

    // Define a malha a renderizar
    void setMesh(const QEMSimplifier* mesh);
    // Atualiza buffers GL sem resetar câmera (para inflate/deflate em tempo real)
    void updateMeshData();
    // Substitui a textura da malha (ex.: Color Map gerado em Textures Preparation)
    // por um mip pyramid já carregado em memória, e liga a textura automaticamente.
    void setColorTexture(const MipPyramid& pyr);

    // Resetar câmera
    void resetCamera();

    // Aplicar estado de câmera vindo de outra viewport (sem re-emitir sinal)
    void syncCamera(float rotX, float rotY, float z);

public slots:
    void setWireframe(bool enabled);
    void setCullFace(bool enabled);
    void setTextured(bool enabled);
    void setUVMode(bool enabled);
    void setShowEdgeClassification(bool enabled);

signals:
    void cameraChanged(float rotX, float rotY, float z);

protected:
    void initializeGL() override;
    void resizeGL(int w, int h) override;
    void paintGL() override;

    // Eventos do mouse
    void mousePressEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void wheelEvent(QWheelEvent* event) override;

private:
    const QEMSimplifier* mesh = nullptr;
    QOpenGLBuffer vbo, ebo;
    QOpenGLVertexArrayObject vao;
    int indexCount = 0;

    // Câmera
    QVector3D cameraPos = {0, 0, 3};
    QVector3D cameraTarget = {0, 0, 0};
    QVector3D cameraUp = {0, 1, 0};
    float rotationX = 0.0f;
    float rotationY = 0.0f;
    float zoom = 3.0f;

    bool wireframe = false;
    bool cullFace  = true;
    bool textured  = false;
    bool uvMode    = false;
    bool showEdgeClassification = false;

    GLuint textureId = 0;
    void uploadTexture(const QEMSimplifier* m);

    // Normalização automática de escala/centro ao carregar malha
    glm::vec3 meshCenter{0.0f, 0.0f, 0.0f};
    float meshNormScale{1.0f};

    // Mouse
    QPoint lastMousePos;

    // Shader 3D
    QOpenGLShaderProgram shaderProgram;
    void createShaderProgram();
    void updateMeshBuffers();

    // Overlay de classificação de aresta (boundary vs interna)
    QOpenGLBuffer edgeVbo{QOpenGLBuffer::VertexBuffer};
    QOpenGLVertexArrayObject edgeVao;
    QOpenGLShaderProgram edgeShaderProgram;
    int edgeVertexCount = 0;
    void createEdgeShaderProgram();
    void updateEdgeOverlay();

    // UV view
    QOpenGLVertexArrayObject uvVao;
    QOpenGLBuffer uvVboData{QOpenGLBuffer::VertexBuffer};
    QOpenGLVertexArrayObject bgVao;
    QOpenGLBuffer bgVboData{QOpenGLBuffer::VertexBuffer};
    QOpenGLShaderProgram uvShader;
    QOpenGLShaderProgram bgShader;
    void createUVShaders();
    void updateUVBuffers();
    void paintUVMode();
};
