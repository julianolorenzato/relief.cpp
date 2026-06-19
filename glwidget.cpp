#include "glwidget.h"
#include <QMouseEvent>
#include <QWheelEvent>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <iostream>

GLWidget::GLWidget(QWidget* parent)
    : QOpenGLWidget(parent), vbo(QOpenGLBuffer::VertexBuffer), ebo(QOpenGLBuffer::IndexBuffer) {
    setFocusPolicy(Qt::StrongFocus);
}

GLWidget::~GLWidget() {
    makeCurrent();
    if (vbo.isCreated()) vbo.destroy();
    if (ebo.isCreated()) ebo.destroy();
    if (vao.isCreated()) vao.destroy();
    if (uvVboData.isCreated()) uvVboData.destroy();
    if (uvVao.isCreated()) uvVao.destroy();
    if (bgVboData.isCreated()) bgVboData.destroy();
    if (bgVao.isCreated()) bgVao.destroy();
    if (edgeVbo.isCreated()) edgeVbo.destroy();
    if (edgeVao.isCreated()) edgeVao.destroy();
    if (textureId) glDeleteTextures(1, &textureId);
    doneCurrent();
}

void GLWidget::setMesh(const QEMSimplifier* m) {
    mesh = m;
    makeCurrent();
    updateMeshBuffers();
    updateEdgeOverlay();
    uploadTexture(m);
    doneCurrent();
    resetCamera();
}

void GLWidget::createShaderProgram() {
    const char* vertexShader = R"(
        #version 330 core
        layout(location = 0) in vec3 position;
        layout(location = 1) in vec3 normal;
        layout(location = 2) in vec2 texCoord;

        uniform mat4 model;
        uniform mat4 view;
        uniform mat4 projection;

        out vec3 FragPos;
        out vec3 Normal;
        out vec2 TexCoord;

        void main() {
            FragPos = vec3(model * vec4(position, 1.0));
            Normal = mat3(transpose(inverse(model))) * normal;
            TexCoord = texCoord;
            gl_Position = projection * view * vec4(FragPos, 1.0);
        }
    )";

    const char* fragmentShader = R"(
        #version 330 core
        in vec3 FragPos;
        in vec3 Normal;
        in vec2 TexCoord;
        out vec4 FragColor;

        uniform sampler2D textureSampler;
        uniform bool useTexture;

        void main() {
            vec3 norm = normalize(Normal);
            vec3 lightDir = normalize(vec3(1.0, 1.0, 1.0));
            float diff = max(dot(norm, lightDir), 0.0);
            vec3 color = useTexture
                ? texture(textureSampler, TexCoord).rgb
                : vec3(0.5, 0.7, 0.9);
            vec3 result = color * (0.3 + 0.7 * diff);
            FragColor = vec4(result, 1.0);
        }
    )";

    if (!shaderProgram.addShaderFromSourceCode(QOpenGLShader::Vertex, vertexShader)) {
        std::cerr << "Vertex shader error: " << shaderProgram.log().toStdString() << "\n";
        return;
    }

    if (!shaderProgram.addShaderFromSourceCode(QOpenGLShader::Fragment, fragmentShader)) {
        std::cerr << "Fragment shader error: " << shaderProgram.log().toStdString() << "\n";
        return;
    }

    if (!shaderProgram.link()) {
        std::cerr << "Shader linking error: " << shaderProgram.log().toStdString() << "\n";
        return;
    }
}

void GLWidget::createEdgeShaderProgram() {
    const char* vertexShader = R"(
        #version 330 core
        layout(location = 0) in vec3 position;
        layout(location = 1) in vec3 color;

        uniform mat4 model;
        uniform mat4 view;
        uniform mat4 projection;

        out vec3 vColor;

        void main() {
            vColor = color;
            gl_Position = projection * view * model * vec4(position, 1.0);
        }
    )";

    const char* fragmentShader = R"(
        #version 330 core
        in vec3 vColor;
        out vec4 FragColor;

        void main() {
            FragColor = vec4(vColor, 1.0);
        }
    )";

    if (!edgeShaderProgram.addShaderFromSourceCode(QOpenGLShader::Vertex, vertexShader)) {
        std::cerr << "Edge vertex shader error: " << edgeShaderProgram.log().toStdString() << "\n";
        return;
    }
    if (!edgeShaderProgram.addShaderFromSourceCode(QOpenGLShader::Fragment, fragmentShader)) {
        std::cerr << "Edge fragment shader error: " << edgeShaderProgram.log().toStdString() << "\n";
        return;
    }
    if (!edgeShaderProgram.link()) {
        std::cerr << "Edge shader linking error: " << edgeShaderProgram.log().toStdString() << "\n";
        return;
    }
}

void GLWidget::updateMeshBuffers() {
    if (!mesh || mesh->vertices.empty()) return;

    // Calcular bounding box para normalização de escala/centro
    Eigen::Vector3d bmin( 1e18,  1e18,  1e18);
    Eigen::Vector3d bmax(-1e18, -1e18, -1e18);
    for (const auto& v : mesh->vertices) {
        if (v.removed) continue;
        bmin = bmin.cwiseMin(v.pos);
        bmax = bmax.cwiseMax(v.pos);
    }
    Eigen::Vector3d center = (bmin + bmax) * 0.5;
    double radius = (bmax - bmin).norm() * 0.5;
    if (radius < 1e-9) radius = 1.0;
    meshCenter    = glm::vec3((float)center.x(), (float)center.y(), (float)center.z());
    meshNormScale = 1.0f / (float)radius;

    std::vector<float> vertices;
    std::vector<unsigned int> indices;

    // Construir array de vértices com posição e normal
    std::vector<Eigen::Vector3d> normals(mesh->vertices.size(), Eigen::Vector3d::Zero());

    // Calcular normais dos triângulos
    for (const auto& face : mesh->faces) {
        if (face.removed) continue;
        const Eigen::Vector3d& p0 = mesh->vertices[face.v[0]].pos;
        const Eigen::Vector3d& p1 = mesh->vertices[face.v[1]].pos;
        const Eigen::Vector3d& p2 = mesh->vertices[face.v[2]].pos;

        Eigen::Vector3d n = (p1 - p0).cross(p2 - p0);
        normals[face.v[0]] += n;
        normals[face.v[1]] += n;
        normals[face.v[2]] += n;
    }

    // Normalizar
    for (auto& n : normals) n = n.normalized();

    // Remapeamento de vértices ativos
    std::vector<int> remap(mesh->vertices.size(), -1);
    int newVertexCount = 0;
    for (size_t i = 0; i < mesh->vertices.size(); i++) {
        if (!mesh->vertices[i].removed) {
            remap[i] = newVertexCount++;
            const auto& v = mesh->vertices[i];
            const auto& n = normals[i];
            vertices.push_back(v.pos.x());
            vertices.push_back(v.pos.y());
            vertices.push_back(v.pos.z());
            vertices.push_back(n.x());
            vertices.push_back(n.y());
            vertices.push_back(n.z());
            vertices.push_back((float)v.uv.x());
            vertices.push_back((float)v.uv.y());
        }
    }

    // Faces
    for (const auto& face : mesh->faces) {
        if (face.removed) continue;
        indices.push_back(remap[face.v[0]]);
        indices.push_back(remap[face.v[1]]);
        indices.push_back(remap[face.v[2]]);
    }

    indexCount = indices.size();

    if (!vao.isCreated()) vao.create();
    if (!vbo.isCreated()) vbo.create();
    if (!ebo.isCreated()) ebo.create();

    vao.bind();

    vbo.bind();
    vbo.allocate(vertices.data(), vertices.size() * sizeof(float));

    ebo.bind();
    ebo.allocate(indices.data(), indices.size() * sizeof(unsigned int));

    // Posição
    shaderProgram.enableAttributeArray(0);
    shaderProgram.setAttributeBuffer(0, GL_FLOAT, 0, 3, 8 * sizeof(float));

    // Normal
    shaderProgram.enableAttributeArray(1);
    shaderProgram.setAttributeBuffer(1, GL_FLOAT, 3 * sizeof(float), 3, 8 * sizeof(float));

    // UV
    shaderProgram.enableAttributeArray(2);
    shaderProgram.setAttributeBuffer(2, GL_FLOAT, 6 * sizeof(float), 2, 8 * sizeof(float));

    vao.release();

    updateUVBuffers();
}

void GLWidget::updateEdgeOverlay() {
    if (!mesh) return;

    static const float kBoundaryColor[3] = {1.0f, 0.15f, 0.1f};
    static const float kInternalColor[3] = {0.8f, 0.8f, 0.8f};

    auto edges = mesh->classifyEdges();

    std::vector<float> lineVerts;
    lineVerts.reserve(edges.size() * 2 * 6);

    for (const auto& e : edges) {
        const auto& p0 = mesh->vertices[e.v1].pos;
        const auto& p1 = mesh->vertices[e.v2].pos;
        const float* c = e.boundary ? kBoundaryColor : kInternalColor;

        lineVerts.push_back((float)p0.x()); lineVerts.push_back((float)p0.y()); lineVerts.push_back((float)p0.z());
        lineVerts.push_back(c[0]); lineVerts.push_back(c[1]); lineVerts.push_back(c[2]);

        lineVerts.push_back((float)p1.x()); lineVerts.push_back((float)p1.y()); lineVerts.push_back((float)p1.z());
        lineVerts.push_back(c[0]); lineVerts.push_back(c[1]); lineVerts.push_back(c[2]);
    }

    edgeVertexCount = (int)(lineVerts.size() / 6);

    if (!edgeVao.isCreated()) edgeVao.create();
    if (!edgeVbo.isCreated()) edgeVbo.create();

    edgeVao.bind();
    edgeVbo.bind();
    edgeVbo.allocate(lineVerts.data(), (int)(lineVerts.size() * sizeof(float)));

    edgeShaderProgram.enableAttributeArray(0);
    edgeShaderProgram.setAttributeBuffer(0, GL_FLOAT, 0, 3, 6 * sizeof(float));

    edgeShaderProgram.enableAttributeArray(1);
    edgeShaderProgram.setAttributeBuffer(1, GL_FLOAT, 3 * sizeof(float), 3, 6 * sizeof(float));

    edgeVao.release();
}

void GLWidget::uploadTexture(const QEMSimplifier* m) {
    if (textureId) {
        glDeleteTextures(1, &textureId);
        textureId = 0;
    }
    if (!m || m->textureData.empty()) return;

    glGenTextures(1, &textureId);
    glBindTexture(GL_TEXTURE_2D, textureId);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA,
                 m->textureWidth, m->textureHeight,
                 0, GL_RGBA, GL_UNSIGNED_BYTE, m->textureData.data());
    glGenerateMipmap(GL_TEXTURE_2D);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
    glBindTexture(GL_TEXTURE_2D, 0);
}

void GLWidget::setColorTexture(const MipPyramid& pyr) {
    makeCurrent();
    if (textureId) {
        glDeleteTextures(1, &textureId);
        textureId = 0;
    }
    if (!pyr.mips.empty() && pyr.width > 0 && pyr.height > 0) {
        GLenum internalFormat = pyr.channels == 3 ? GL_RGB32F : GL_RGBA32F;
        GLenum format         = pyr.channels == 3 ? GL_RGB    : GL_RGBA;

        glGenTextures(1, &textureId);
        glBindTexture(GL_TEXTURE_2D, textureId);
        glTexImage2D(GL_TEXTURE_2D, 0, internalFormat, pyr.width, pyr.height,
                     0, format, GL_FLOAT, pyr.mips[0].data());
        glGenerateMipmap(GL_TEXTURE_2D);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
        glBindTexture(GL_TEXTURE_2D, 0);
    }
    doneCurrent();

    textured = textureId != 0;
    update();
}

void GLWidget::updateMeshData() {
    makeCurrent();
    updateMeshBuffers();
    updateEdgeOverlay();
    doneCurrent();
    update();
}

void GLWidget::setWireframe(bool enabled) {
    wireframe = enabled;
    update();
}

void GLWidget::setShowEdgeClassification(bool enabled) {
    showEdgeClassification = enabled;
    update();
}

void GLWidget::setCullFace(bool enabled) {
    cullFace = enabled;
    update();
}

void GLWidget::setTextured(bool enabled) {
    textured = enabled;
    update();
}

void GLWidget::initializeGL() {
    initializeOpenGLFunctions();
    glClearColor(0.1f, 0.1f, 0.1f, 1.0f);
    glEnable(GL_DEPTH_TEST);
    glEnable(GL_CULL_FACE);

    createShaderProgram();
    createEdgeShaderProgram();
    createUVShaders();
    resetCamera();
}

void GLWidget::createUVShaders() {
    const char* bgVert = R"(
        #version 330 core
        layout(location = 0) in vec2 pos;
        out vec2 TexCoord;
        void main() {
            TexCoord = pos * 0.5 + 0.5;
            gl_Position = vec4(pos, 0.0, 1.0);
        }
    )";
    const char* bgFrag = R"(
        #version 330 core
        in vec2 TexCoord;
        out vec4 FragColor;
        uniform sampler2D textureSampler;
        uniform bool hasTexture;
        void main() {
            if (hasTexture) {
                FragColor = texture(textureSampler, TexCoord);
            } else {
                // checkerboard cinza como fundo quando sem textura
                ivec2 tile = ivec2(TexCoord * 8.0);
                float c = ((tile.x + tile.y) % 2 == 0) ? 0.55 : 0.45;
                FragColor = vec4(c, c, c, 1.0);
            }
        }
    )";
    bgShader.addShaderFromSourceCode(QOpenGLShader::Vertex, bgVert);
    bgShader.addShaderFromSourceCode(QOpenGLShader::Fragment, bgFrag);
    bgShader.link();

    const char* uvVert = R"(
        #version 330 core
        layout(location = 0) in vec2 uvCoord;
        void main() {
            gl_Position = vec4(uvCoord.x * 2.0 - 1.0, uvCoord.y * 2.0 - 1.0, 0.0, 1.0);
        }
    )";
    const char* uvFrag = R"(
        #version 330 core
        out vec4 FragColor;
        uniform vec4 wireColor;
        void main() {
            FragColor = wireColor;
        }
    )";
    uvShader.addShaderFromSourceCode(QOpenGLShader::Vertex, uvVert);
    uvShader.addShaderFromSourceCode(QOpenGLShader::Fragment, uvFrag);
    uvShader.link();
}

void GLWidget::updateUVBuffers() {
    if (!mesh) return;

    // Posições 2D: coordenadas UV de cada vértice ativo (mesma ordem que updateMeshBuffers)
    std::vector<float> uvPositions;
    for (const auto& v : mesh->vertices) {
        if (v.removed) continue;
        uvPositions.push_back((float)v.uv.x());
        uvPositions.push_back((float)v.uv.y());
    }

    if (!uvVao.isCreated()) uvVao.create();
    if (!uvVboData.isCreated()) uvVboData.create();

    uvVao.bind();
    uvVboData.bind();
    uvVboData.allocate(uvPositions.data(), (int)(uvPositions.size() * sizeof(float)));
    // reutiliza o mesmo EBO do VAO 3D (mesma ordem de remapeamento)
    ebo.bind();
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 2 * sizeof(float), nullptr);
    uvVao.release();

    // Quad de fundo (-1,-1) → (1,1)
    static const float bgVerts[] = { -1.f,-1.f,  1.f,-1.f,  1.f,1.f,  -1.f,1.f };
    if (!bgVao.isCreated()) bgVao.create();
    if (!bgVboData.isCreated()) bgVboData.create();

    bgVao.bind();
    bgVboData.bind();
    bgVboData.allocate(bgVerts, sizeof(bgVerts));
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 2 * sizeof(float), nullptr);
    bgVao.release();
}

void GLWidget::setUVMode(bool enabled) {
    uvMode = enabled;
    update();
}

void GLWidget::paintUVMode() {
    glDisable(GL_DEPTH_TEST);
    glDisable(GL_CULL_FACE);
    glPolygonMode(GL_FRONT_AND_BACK, GL_FILL);

    // Fundo: textura ou checkerboard
    bgShader.bind();
    bgShader.setUniformValue("hasTexture", textureId != 0);
    if (textureId) {
        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, textureId);
        bgShader.setUniformValue("textureSampler", 0);
    }
    bgVao.bind();
    glDrawArrays(GL_TRIANGLE_FAN, 0, 4);
    bgVao.release();
    if (textureId) glBindTexture(GL_TEXTURE_2D, 0);
    bgShader.release();

    // Wireframe UV
    if (uvVao.isCreated() && indexCount > 0) {
        glPolygonMode(GL_FRONT_AND_BACK, GL_LINE);
        glLineWidth(1.2f);
        uvShader.bind();
        uvShader.setUniformValue("wireColor", QVector4D(0.05f, 0.9f, 0.4f, 1.0f));
        uvVao.bind();
        glDrawElements(GL_TRIANGLES, indexCount, GL_UNSIGNED_INT, nullptr);
        uvVao.release();
        uvShader.release();
        glPolygonMode(GL_FRONT_AND_BACK, GL_FILL);
    }

    glEnable(GL_DEPTH_TEST);
}

void GLWidget::resizeGL(int w, int h) {
    glViewport(0, 0, w, h);
}

void GLWidget::paintGL() {
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

    if (!mesh || !vao.isCreated() || indexCount == 0) return;

    if (uvMode) {
        paintUVMode();
        return;
    }

    if (cullFace) glEnable(GL_CULL_FACE); else glDisable(GL_CULL_FACE);
    glPolygonMode(GL_FRONT_AND_BACK, wireframe ? GL_LINE : GL_FILL);

    shaderProgram.bind();

    // Projeção
    glm::mat4 projection = glm::perspective(
        glm::radians(45.0f),
        (float)width() / std::max(1, height()),
        0.1f, 100.0f
    );
    shaderProgram.setUniformValue("projection", QMatrix4x4(glm::value_ptr(projection)).transposed());

    // View
    glm::vec3 pos(
        zoom * sinf(glm::radians(rotationY)) * cosf(glm::radians(rotationX)),
        zoom * sinf(glm::radians(rotationX)),
        zoom * cosf(glm::radians(rotationY)) * cosf(glm::radians(rotationX))
    );
    glm::mat4 view = glm::lookAt(pos, glm::vec3(0, 0, 0), glm::vec3(0, 1, 0));
    shaderProgram.setUniformValue("view", QMatrix4x4(glm::value_ptr(view)).transposed());

    // Model: centraliza e normaliza para caber na tela
    glm::mat4 model = glm::scale(glm::mat4(1.0f), glm::vec3(meshNormScale))
                    * glm::translate(glm::mat4(1.0f), -meshCenter);
    shaderProgram.setUniformValue("model", QMatrix4x4(glm::value_ptr(model)).transposed());

    bool useTexture = textured && textureId != 0;
    shaderProgram.setUniformValue("useTexture", useTexture);
    if (useTexture) {
        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, textureId);
        shaderProgram.setUniformValue("textureSampler", 0);
    }

    vao.bind();
    glDrawElements(GL_TRIANGLES, indexCount, GL_UNSIGNED_INT, 0);
    vao.release();

    if (useTexture) glBindTexture(GL_TEXTURE_2D, 0);
    shaderProgram.release();

    if (showEdgeClassification && edgeVao.isCreated() && edgeVertexCount > 0) {
        edgeShaderProgram.bind();
        edgeShaderProgram.setUniformValue("projection", QMatrix4x4(glm::value_ptr(projection)).transposed());
        edgeShaderProgram.setUniformValue("view", QMatrix4x4(glm::value_ptr(view)).transposed());
        edgeShaderProgram.setUniformValue("model", QMatrix4x4(glm::value_ptr(model)).transposed());

        glDepthFunc(GL_LEQUAL);
        glLineWidth(1.5f);
        edgeVao.bind();
        glDrawArrays(GL_LINES, 0, edgeVertexCount);
        edgeVao.release();
        edgeShaderProgram.release();
        glDepthFunc(GL_LESS);
        glLineWidth(1.0f);
    }
}

void GLWidget::resetCamera() {
    rotationX = 0.0f;
    rotationY = 0.0f;
    zoom = 3.0f;
    update();
    emit cameraChanged(rotationX, rotationY, zoom);
}

void GLWidget::syncCamera(float rotX, float rotY, float z) {
    rotationX = rotX;
    rotationY = rotY;
    zoom = z;
    update();
}

void GLWidget::mousePressEvent(QMouseEvent* event) {
    lastMousePos = event->pos();
}

void GLWidget::mouseMoveEvent(QMouseEvent* event) {
    if (!(event->buttons() & Qt::LeftButton)) return;

    int dx = event->pos().x() - lastMousePos.x();
    int dy = event->pos().y() - lastMousePos.y();

    rotationY += dx * 0.5f;
    rotationX += dy * 0.5f;

    lastMousePos = event->pos();
    update();
    emit cameraChanged(rotationX, rotationY, zoom);
}

void GLWidget::wheelEvent(QWheelEvent* event) {
    zoom -= event->angleDelta().y() * 0.001f;
    zoom = std::max(0.1f, std::min(zoom, 20.0f));
    update();
    emit cameraChanged(rotationX, rotationY, zoom);
}

