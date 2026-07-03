#include "gui/orbital3dview.h"
#include <QColorDialog>
#include <QHBoxLayout>
#include <QMouseEvent>
#include <QWheelEvent>
#include <QPainter>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <cmath>
#include <algorithm>
#include <iostream>

// ─── Anonymous helpers ────────────────────────────────────────────────────────

namespace {

// Build 11-float interleaved vertex array [pos|normal|uv|tangent] and an index
// array for a QEMSimplifier mesh. Tangents are Gram-Schmidt-orthogonalised
// against the normal using per-face UV deltas.
void buildMeshVerts(const QEMSimplifier* mesh,
                    std::vector<float>& verts,
                    std::vector<unsigned int>& idxs)
{
    if (!mesh || mesh->vertices.empty()) return;

    std::vector<Eigen::Vector3d> normals(mesh->vertices.size(), Eigen::Vector3d::Zero());
    std::vector<Eigen::Vector3d> tangents(mesh->vertices.size(), Eigen::Vector3d::Zero());

    for (const auto& f : mesh->faces) {
        if (f.removed) continue;
        const Eigen::Vector3d& p0 = mesh->vertices[f.v[0]].pos;
        const Eigen::Vector3d& p1 = mesh->vertices[f.v[1]].pos;
        const Eigen::Vector3d& p2 = mesh->vertices[f.v[2]].pos;
        const Eigen::Vector2d& u0 = mesh->vertices[f.v[0]].uv;
        const Eigen::Vector2d& u1 = mesh->vertices[f.v[1]].uv;
        const Eigen::Vector2d& u2 = mesh->vertices[f.v[2]].uv;

        Eigen::Vector3d n = (p1 - p0).cross(p2 - p0);
        normals[f.v[0]] += n; normals[f.v[1]] += n; normals[f.v[2]] += n;

        Eigen::Vector3d e1 = p1 - p0, e2 = p2 - p0;
        Eigen::Vector2d d1 = u1 - u0, d2 = u2 - u0;
        double det = d1.x() * d2.y() - d2.x() * d1.y();
        if (std::abs(det) > 1e-12) {
            double r = 1.0 / det;
            Eigen::Vector3d T = r * (d2.y() * e1 - d1.y() * e2);
            tangents[f.v[0]] += T; tangents[f.v[1]] += T; tangents[f.v[2]] += T;
        }
    }

    for (auto& n : normals) n = n.normalized();
    for (size_t i = 0; i < tangents.size(); i++) {
        const Eigen::Vector3d& n = normals[i];
        Eigen::Vector3d t = tangents[i] - n * n.dot(tangents[i]);
        double len = t.norm();
        tangents[i] = len > 1e-8 ? t / len : Eigen::Vector3d(1.0, 0.0, 0.0);
    }

    std::vector<int> remap(mesh->vertices.size(), -1);
    int cnt = 0;
    for (size_t i = 0; i < mesh->vertices.size(); i++) {
        if (mesh->vertices[i].removed) continue;
        remap[i] = cnt++;
        const auto& v = mesh->vertices[i];
        const auto& n = normals[i];
        const auto& t = tangents[i];
        verts.push_back((float)v.pos.x()); verts.push_back((float)v.pos.y()); verts.push_back((float)v.pos.z());
        verts.push_back((float)n.x());     verts.push_back((float)n.y());     verts.push_back((float)n.z());
        verts.push_back((float)v.uv.x());  verts.push_back((float)v.uv.y());
        verts.push_back((float)t.x());     verts.push_back((float)t.y());     verts.push_back((float)t.z());
    }
    for (const auto& f : mesh->faces) {
        if (f.removed) continue;
        idxs.push_back(remap[f.v[0]]);
        idxs.push_back(remap[f.v[1]]);
        idxs.push_back(remap[f.v[2]]);
    }
}

// Upload interleaved vertex data to a VAO with the standard 11-float attribute layout.
// Caller must have already set up vao, vbo, ebo as created QOpenGLBuffer objects.
void setupMeshVAO(QOpenGLFunctions_3_3_Core* gl,
                  const std::vector<float>& verts,
                  const std::vector<unsigned int>& idxs,
                  QOpenGLBuffer& vbo,
                  QOpenGLBuffer& ebo,
                  QOpenGLVertexArrayObject& vao,
                  int& indexCount)
{
    indexCount = (int)idxs.size();
    if (!vao.isCreated()) vao.create();
    if (!vbo.isCreated()) vbo.create();
    if (!ebo.isCreated()) ebo.create();

    vao.bind();
    vbo.bind();
    vbo.allocate(verts.data(), (int)(verts.size() * sizeof(float)));
    ebo.bind();
    ebo.allocate(idxs.data(), (int)(idxs.size() * sizeof(unsigned int)));

    constexpr GLsizei stride = 11 * sizeof(float);
    gl->glEnableVertexAttribArray(0);
    gl->glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, stride, (void*)0);
    gl->glEnableVertexAttribArray(1);
    gl->glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, stride, (void*)(3 * sizeof(float)));
    gl->glEnableVertexAttribArray(2);
    gl->glVertexAttribPointer(2, 2, GL_FLOAT, GL_FALSE, stride, (void*)(6 * sizeof(float)));
    gl->glEnableVertexAttribArray(3);
    gl->glVertexAttribPointer(3, 3, GL_FLOAT, GL_FALSE, stride, (void*)(8 * sizeof(float)));

    vao.release();
}

QMatrix4x4 toQt(const glm::mat4& m) {
    return QMatrix4x4(glm::value_ptr(m)).transposed();
}

} // namespace

// ─── Constructor / destructor ─────────────────────────────────────────────────

Orbital3DView::Orbital3DView(RenderMode mode, const QString& title, QWidget* parent)
    : QOpenGLWidget(parent), mode_(mode)
{
    setFocusPolicy(Qt::StrongFocus);
    if (!title.isEmpty())
        setTitle(title);
    if (mode_ == RenderMode::Overlay)
        createColorRow();
}

Orbital3DView::~Orbital3DView() {
    makeCurrent();
    if (primaryVbo_.isCreated())   primaryVbo_.destroy();
    if (primaryEbo_.isCreated())   primaryEbo_.destroy();
    if (primaryVao_.isCreated())   primaryVao_.destroy();
    if (secondaryVbo_.isCreated()) secondaryVbo_.destroy();
    if (secondaryEbo_.isCreated()) secondaryEbo_.destroy();
    if (secondaryVao_.isCreated()) secondaryVao_.destroy();
    if (edgeVbo_.isCreated())      edgeVbo_.destroy();
    if (edgeVao_.isCreated())      edgeVao_.destroy();
    if (uvVbo_.isCreated())        uvVbo_.destroy();
    if (uvVao_.isCreated())        uvVao_.destroy();
    if (uvBgVbo_.isCreated())      uvBgVbo_.destroy();
    if (uvBgVao_.isCreated())      uvBgVao_.destroy();
    deleteTextures();
    if (samplerPoint_) glDeleteSamplers(1, &samplerPoint_);
    doneCurrent();
}

// ─── Public API ───────────────────────────────────────────────────────────────

void Orbital3DView::setMode(RenderMode mode) {
    mode_ = mode;
    update();
}

void Orbital3DView::setMesh(const QEMSimplifier* mesh) {
    primaryMesh_       = mesh;
    primaryMeshDirty_  = true;
    update();
    resetCamera();
}

void Orbital3DView::setMeshes(const QEMSimplifier* primary, const QEMSimplifier* secondary) {
    primaryMesh_         = primary;
    secondaryMesh_       = secondary;
    primaryMeshDirty_    = true;
    secondaryMeshDirty_  = true;
    update();
    resetCamera();
}

void Orbital3DView::updateMeshData() {
    primaryMeshDirty_ = true;
    update();
}

void Orbital3DView::updateSecondaryMesh() {
    secondaryMeshDirty_ = true;
    update();
}

void Orbital3DView::setColorTexture(const MipPyramid& pyr) {
    pendingColorPyr_ = &pyr;
    colorPyrDirty_   = true;
    update();
}

void Orbital3DView::setColorMap(const MipPyramid& pyr) {
    pendingColorMap_ = &pyr;
    reliefTexDirty_  = true;
    update();
}

void Orbital3DView::setReliefMap(const MipPyramid& pyr) {
    pendingReliefMap_ = &pyr;
    reliefTexDirty_   = true;
    int res = std::max(1, pyr.width);
    lastMip_   = std::log2((float)res);
    texelSize_ = 1.0f / (float)res;
    update();
}

void Orbital3DView::setNormalMap(const MipPyramid& pyr) {
    pendingNormalMap_ = &pyr;
    reliefTexDirty_   = true;
    update();
}

void Orbital3DView::setOffsetMap(const OffsetMapResult& off) {
    pendingOffsetMap_ = &off;
    reliefTexDirty_   = true;
    update();
}

void Orbital3DView::resetCamera() {
    rotX_ = 0.f; rotY_ = 0.f; zoom_ = 3.f;
    update();
    emit cameraChanged(rotX_, rotY_, zoom_);
}

void Orbital3DView::syncCamera(float rotX, float rotY, float z) {
    rotX_ = rotX; rotY_ = rotY; zoom_ = z;
    update();
    // No re-emit to avoid feedback loops
}

// ─── Slots ────────────────────────────────────────────────────────────────────

void Orbital3DView::setPrimaryColor(const QColor& c) {
    primaryColor_ = c;
    if (primaryColorBtn_) applyColorBtnStyle(primaryColorBtn_, c);
    update();
}
void Orbital3DView::setSecondaryColor(const QColor& c) {
    secondaryColor_ = c;
    if (secondaryColorBtn_) applyColorBtnStyle(secondaryColorBtn_, c);
    update();
}

void Orbital3DView::setWireframe(bool v)         { wireframe_    = v; update(); }
void Orbital3DView::setCullFace(bool v)           { cullFace_     = v; update(); }
void Orbital3DView::setTextured(bool v)           { textured_     = v; update(); }
void Orbital3DView::setUVMode(bool v)             { uvMode_       = v; update(); }
void Orbital3DView::setShowBoundaryEdges(bool v)  { showBoundary_ = v; update(); }
void Orbital3DView::setShowInternalEdges(bool v)  { showInternal_ = v; update(); }
void Orbital3DView::setReliefEnabled(bool v)      { reliefEnabled_= v; update(); }
void Orbital3DView::setUseAtlas(bool v)           { useAtlas_     = v; update(); }
void Orbital3DView::setSteps(int v)               { steps_        = std::max(1, v); update(); }
void Orbital3DView::setBinarySteps(int v)         { binarySteps_  = std::max(0, v); update(); }
void Orbital3DView::setDepthScale(double v)       { depthScale_   = (float)v; update(); }
void Orbital3DView::setDebugView(int v)           { debugView_    = v; update(); }

// ─── GL lifecycle ─────────────────────────────────────────────────────────────

void Orbital3DView::initializeGL() {
    initializeOpenGLFunctions();
    glClearColor(0.12f, 0.12f, 0.12f, 1.0f);
    glEnable(GL_DEPTH_TEST);

    createShaders();

    glGenSamplers(1, &samplerPoint_);
    glSamplerParameteri(samplerPoint_, GL_TEXTURE_MIN_FILTER, GL_NEAREST_MIPMAP_NEAREST);
    glSamplerParameteri(samplerPoint_, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glSamplerParameteri(samplerPoint_, GL_TEXTURE_WRAP_S, GL_REPEAT);
    glSamplerParameteri(samplerPoint_, GL_TEXTURE_WRAP_T, GL_REPEAT);
}

void Orbital3DView::resizeGL(int w, int h) {
    glViewport(0, 0, w, h);
}

void Orbital3DView::resizeEvent(QResizeEvent* e) {
    QOpenGLWidget::resizeEvent(e);
    if (titleLabel_)
        titleLabel_->setGeometry(0, 0, width(), titleLabel_->height());
    int bottomY = height();
    if (colorRow_) {
        bottomY -= colorRow_->height();
        colorRow_->setGeometry(0, bottomY, width(), colorRow_->height());
    }
    if (statsLabel_)
        statsLabel_->setGeometry(0, bottomY - statsLabel_->height(), width(), statsLabel_->height());
}

void Orbital3DView::applyColorBtnStyle(QPushButton* btn, const QColor& c) {
    btn->setStyleSheet(QString(
        "QPushButton {"
        "  background: rgba(20,20,20,200);"
        "  color: %1;"
        "  border: none;"
        "  font-size: 11px;"
        "  font-weight: bold;"
        "  padding: 2px 10px;"
        "}"
        "QPushButton:hover { background: rgba(40,40,40,220); }"
    ).arg(c.name()));
}

void Orbital3DView::createColorRow() {
    colorRow_ = new QWidget(this);
    colorRow_->setAttribute(Qt::WA_TranslucentBackground);
    colorRow_->setFixedHeight(28);

    auto* layout = new QHBoxLayout(colorRow_);
    layout->setContentsMargins(6, 0, 6, 0);
    layout->setSpacing(4);

    primaryColorBtn_ = new QPushButton("● Primary", colorRow_);
    secondaryColorBtn_ = new QPushButton("● Secondary", colorRow_);

    applyColorBtnStyle(primaryColorBtn_,   primaryColor_);
    applyColorBtnStyle(secondaryColorBtn_, secondaryColor_);

    layout->addStretch();
    layout->addWidget(primaryColorBtn_);
    layout->addWidget(secondaryColorBtn_);
    layout->addStretch();

    connect(primaryColorBtn_, &QPushButton::clicked, this, [this] {
        QColor c = QColorDialog::getColor(primaryColor_, this, "Primary mesh color");
        if (c.isValid()) setPrimaryColor(c);
    });
    connect(secondaryColorBtn_, &QPushButton::clicked, this, [this] {
        QColor c = QColorDialog::getColor(secondaryColor_, this, "Secondary mesh color");
        if (c.isValid()) setSecondaryColor(c);
    });

    colorRow_->raise();
    colorRow_->show();
}

void Orbital3DView::setStats(int faces, int vertices) {
    if (!statsLabel_) {
        statsLabel_ = new QLabel(this);
        statsLabel_->setAlignment(Qt::AlignCenter);
        statsLabel_->setFixedHeight(22);
        statsLabel_->setAttribute(Qt::WA_TransparentForMouseEvents);
        statsLabel_->setStyleSheet(
            "QLabel {"
            "  background: rgba(20, 20, 20, 180);"
            "  color: #a0a0a0;"
            "  font-size: 10px;"
            "  padding: 0 8px;"
            "}"
        );
    }
    statsLabel_->setText(QString("%1 faces  ·  %2 vertices").arg(faces).arg(vertices));
    int bottomY = height() - (colorRow_ ? colorRow_->height() : 0);
    statsLabel_->setGeometry(0, bottomY - statsLabel_->height(), width(), statsLabel_->height());
    statsLabel_->raise();
    statsLabel_->show();
}

void Orbital3DView::setTitle(const QString& title) {
    if (!titleLabel_) {
        titleLabel_ = new QLabel(this);
        titleLabel_->setAlignment(Qt::AlignCenter);
        titleLabel_->setFixedHeight(28);
        titleLabel_->setAttribute(Qt::WA_TransparentForMouseEvents);
        titleLabel_->setStyleSheet(
            "QLabel {"
            "  background: rgba(20, 20, 20, 210);"
            "  color: #d0d0d0;"
            "  font-weight: bold;"
            "  font-size: 11px;"
            "  letter-spacing: 1px;"
            "  padding: 0 8px;"
            "}"
        );
    }
    titleLabel_->setText(title.toUpper());
    titleLabel_->setGeometry(0, 0, width(), titleLabel_->height());
    titleLabel_->raise();
    titleLabel_->show();
}

// ─── Shader creation ──────────────────────────────────────────────────────────

void Orbital3DView::createShaders() {
    // Solid / Textured — Phong shading with optional texture
    {
        const char* vert = R"(
            #version 330 core
            layout(location = 0) in vec3 position;
            layout(location = 1) in vec3 normal;
            layout(location = 2) in vec2 texCoord;
            uniform mat4 model; uniform mat4 view; uniform mat4 projection;
            out vec3 FragPos; out vec3 Normal; out vec2 TexCoord;
            void main() {
                FragPos = vec3(model * vec4(position, 1.0));
                Normal  = mat3(transpose(inverse(model))) * normal;
                TexCoord = texCoord;
                gl_Position = projection * view * vec4(FragPos, 1.0);
            }
        )";
        const char* frag = R"(
            #version 330 core
            in vec3 FragPos; in vec3 Normal; in vec2 TexCoord;
            out vec4 FragColor;
            uniform sampler2D textureSampler;
            uniform bool useTexture;
            void main() {
                vec3 n    = normalize(Normal);
                float diff = max(dot(n, normalize(vec3(1.0, 1.0, 1.0))), 0.0);
                vec3 color = useTexture
                    ? texture(textureSampler, TexCoord).rgb
                    : vec3(0.5, 0.7, 0.9);
                FragColor = vec4(color * (0.3 + 0.7 * diff), 1.0);
            }
        )";
        solidProg_.addShaderFromSourceCode(QOpenGLShader::Vertex,   vert);
        solidProg_.addShaderFromSourceCode(QOpenGLShader::Fragment, frag);
        if (!solidProg_.link())
            std::cerr << "solidProg link error: " << solidProg_.log().toStdString() << "\n";
    }

    // Overlay — two-mesh Phong with per-mesh color
    {
        const char* vert = R"(
            #version 330 core
            layout(location = 0) in vec3 position;
            layout(location = 1) in vec3 normal;
            uniform mat4 model; uniform mat4 view; uniform mat4 projection;
            out vec3 fragNormal;
            void main() {
                fragNormal  = mat3(transpose(inverse(model))) * normal;
                gl_Position = projection * view * model * vec4(position, 1.0);
            }
        )";
        const char* frag = R"(
            #version 330 core
            in vec3 fragNormal;
            out vec4 FragColor;
            uniform vec3 meshColor;
            void main() {
                vec3 n    = normalize(fragNormal);
                float diff = max(dot(n, normalize(vec3(1.0, 1.0, 1.0))), 0.0);
                FragColor = vec4(meshColor * (0.25 + 0.75 * diff), 1.0);
            }
        )";
        overlayProg_.addShaderFromSourceCode(QOpenGLShader::Vertex,   vert);
        overlayProg_.addShaderFromSourceCode(QOpenGLShader::Fragment, frag);
        if (!overlayProg_.link())
            std::cerr << "overlayProg link error: " << overlayProg_.log().toStdString() << "\n";
    }

    // Edge overlay — per-vertex colour, no lighting
    {
        const char* vert = R"(
            #version 330 core
            layout(location = 0) in vec3 position;
            layout(location = 1) in vec3 color;
            uniform mat4 model; uniform mat4 view; uniform mat4 projection;
            out vec3 vColor;
            void main() {
                vColor      = color;
                gl_Position = projection * view * model * vec4(position, 1.0);
            }
        )";
        const char* frag = R"(
            #version 330 core
            in vec3 vColor; out vec4 FragColor;
            void main() { FragColor = vec4(vColor, 1.0); }
        )";
        edgeProg_.addShaderFromSourceCode(QOpenGLShader::Vertex,   vert);
        edgeProg_.addShaderFromSourceCode(QOpenGLShader::Fragment, frag);
        if (!edgeProg_.link())
            std::cerr << "edgeProg link error: " << edgeProg_.log().toStdString() << "\n";
    }

    // UV background — full-screen quad with texture or checkerboard
    {
        const char* vert = R"(
            #version 330 core
            layout(location = 0) in vec2 pos;
            out vec2 TexCoord;
            void main() { TexCoord = pos * 0.5 + 0.5; gl_Position = vec4(pos, 0.0, 1.0); }
        )";
        const char* frag = R"(
            #version 330 core
            in vec2 TexCoord; out vec4 FragColor;
            uniform sampler2D textureSampler; uniform bool hasTexture;
            void main() {
                if (hasTexture) { FragColor = texture(textureSampler, TexCoord); }
                else {
                    ivec2 t = ivec2(TexCoord * 8.0);
                    float c = ((t.x + t.y) % 2 == 0) ? 0.55 : 0.45;
                    FragColor = vec4(c, c, c, 1.0);
                }
            }
        )";
        uvBgProg_.addShaderFromSourceCode(QOpenGLShader::Vertex,   vert);
        uvBgProg_.addShaderFromSourceCode(QOpenGLShader::Fragment, frag);
        if (!uvBgProg_.link())
            std::cerr << "uvBgProg link error: " << uvBgProg_.log().toStdString() << "\n";
    }

    // UV wireframe — UV coordinates to NDC, flat color
    {
        const char* vert = R"(
            #version 330 core
            layout(location = 0) in vec2 uvCoord;
            void main() { gl_Position = vec4(uvCoord.x * 2.0 - 1.0, uvCoord.y * 2.0 - 1.0, 0.0, 1.0); }
        )";
        const char* frag = R"(
            #version 330 core
            out vec4 FragColor; uniform vec4 wireColor;
            void main() { FragColor = wireColor; }
        )";
        uvLineProg_.addShaderFromSourceCode(QOpenGLShader::Vertex,   vert);
        uvLineProg_.addShaderFromSourceCode(QOpenGLShader::Fragment, frag);
        if (!uvLineProg_.link())
            std::cerr << "uvLineProg link error: " << uvLineProg_.log().toStdString() << "\n";
    }

    // Relief — loaded from QRC resources (same sources as ReliefGLWidget)
    {
        if (!reliefProg_.addShaderFromSourceFile(QOpenGLShader::Vertex,   ":/shaders/relief.vert"))
            std::cerr << "relief vert error: " << reliefProg_.log().toStdString() << "\n";
        if (!reliefProg_.addShaderFromSourceFile(QOpenGLShader::Fragment, ":/shaders/relief.frag"))
            std::cerr << "relief frag error: " << reliefProg_.log().toStdString() << "\n";
        if (!reliefProg_.link())
            std::cerr << "reliefProg link error: " << reliefProg_.log().toStdString() << "\n";
    }
}

// ─── Buffer builders ──────────────────────────────────────────────────────────

void Orbital3DView::buildPrimaryBuffers() {
    if (!primaryMesh_ || primaryMesh_->vertices.empty()) return;

    // Compute bounding box for the model matrix normalization
    Eigen::Vector3d bmin( 1e18,  1e18,  1e18);
    Eigen::Vector3d bmax(-1e18, -1e18, -1e18);
    for (const auto& v : primaryMesh_->vertices) {
        if (v.removed) continue;
        bmin = bmin.cwiseMin(v.pos);
        bmax = bmax.cwiseMax(v.pos);
    }
    Eigen::Vector3d center = (bmin + bmax) * 0.5;
    double radius = (bmax - bmin).norm() * 0.5;
    if (radius < 1e-9) radius = 1.0;
    meshCenter_    = glm::vec3((float)center.x(), (float)center.y(), (float)center.z());
    meshNormScale_ = 1.0f / (float)radius;

    std::vector<float> verts;
    std::vector<unsigned int> idxs;
    buildMeshVerts(primaryMesh_, verts, idxs);
    setupMeshVAO(this, verts, idxs, primaryVbo_, primaryEbo_, primaryVao_, primaryIndexCount_);

    buildEdgeBuffers();
    buildUVBuffers();
    uploadColorFromMesh();
}

void Orbital3DView::buildSecondaryBuffers() {
    if (!secondaryMesh_ || secondaryMesh_->vertices.empty()) return;
    std::vector<float> verts;
    std::vector<unsigned int> idxs;
    buildMeshVerts(secondaryMesh_, verts, idxs);
    setupMeshVAO(this, verts, idxs, secondaryVbo_, secondaryEbo_, secondaryVao_, secondaryIndexCount_);
}

void Orbital3DView::buildEdgeBuffers() {
    if (!primaryMesh_) return;

    static const float kBound[3]    = {1.0f, 0.15f, 0.1f};
    static const float kInternal[3] = {0.8f, 0.8f,  0.8f};

    auto edges = primaryMesh_->classifyEdges();

    std::vector<float> lineVerts;
    lineVerts.reserve(edges.size() * 2 * 6);

    auto append = [&](const Eigen::Vector3d& a, const Eigen::Vector3d& b, const float* c) {
        lineVerts.push_back((float)a.x()); lineVerts.push_back((float)a.y()); lineVerts.push_back((float)a.z());
        lineVerts.push_back(c[0]); lineVerts.push_back(c[1]); lineVerts.push_back(c[2]);
        lineVerts.push_back((float)b.x()); lineVerts.push_back((float)b.y()); lineVerts.push_back((float)b.z());
        lineVerts.push_back(c[0]); lineVerts.push_back(c[1]); lineVerts.push_back(c[2]);
    };

    for (const auto& e : edges) {
        if (!e.boundary) continue;
        append(primaryMesh_->vertices[e.v1].pos, primaryMesh_->vertices[e.v2].pos, kBound);
    }
    boundaryEdgeEnd_ = (int)(lineVerts.size() / 6);

    for (const auto& e : edges) {
        if (e.boundary) continue;
        append(primaryMesh_->vertices[e.v1].pos, primaryMesh_->vertices[e.v2].pos, kInternal);
    }
    edgeVertexCount_ = (int)(lineVerts.size() / 6);

    if (!edgeVao_.isCreated()) edgeVao_.create();
    if (!edgeVbo_.isCreated()) edgeVbo_.create();

    edgeVao_.bind();
    edgeVbo_.bind();
    edgeVbo_.allocate(lineVerts.data(), (int)(lineVerts.size() * sizeof(float)));
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 6 * sizeof(float), (void*)0);
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, 6 * sizeof(float), (void*)(3 * sizeof(float)));
    edgeVao_.release();
}

void Orbital3DView::buildUVBuffers() {
    if (!primaryMesh_) return;

    std::vector<float> uvPos;
    for (const auto& v : primaryMesh_->vertices) {
        if (v.removed) continue;
        uvPos.push_back((float)v.uv.x());
        uvPos.push_back((float)v.uv.y());
    }

    if (!uvVao_.isCreated()) uvVao_.create();
    if (!uvVbo_.isCreated()) uvVbo_.create();

    uvVao_.bind();
    uvVbo_.bind();
    uvVbo_.allocate(uvPos.data(), (int)(uvPos.size() * sizeof(float)));
    primaryEbo_.bind();  // Share index buffer — same vertex ordering as buildMeshVerts
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 2 * sizeof(float), nullptr);
    uvVao_.release();

    static const float kBg[] = { -1.f,-1.f,  1.f,-1.f,  1.f,1.f,  -1.f,1.f };
    if (!uvBgVao_.isCreated()) uvBgVao_.create();
    if (!uvBgVbo_.isCreated()) uvBgVbo_.create();

    uvBgVao_.bind();
    uvBgVbo_.bind();
    uvBgVbo_.allocate(kBg, sizeof(kBg));
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 2 * sizeof(float), nullptr);
    uvBgVao_.release();
}

// ─── Texture helpers ──────────────────────────────────────────────────────────

void Orbital3DView::uploadColorFromMesh() {
    if (colorTex_) { glDeleteTextures(1, &colorTex_); colorTex_ = 0; }
    if (!primaryMesh_ || primaryMesh_->textureData.empty()) return;

    glGenTextures(1, &colorTex_);
    glBindTexture(GL_TEXTURE_2D, colorTex_);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA,
                 primaryMesh_->textureWidth, primaryMesh_->textureHeight,
                 0, GL_RGBA, GL_UNSIGNED_BYTE, primaryMesh_->textureData.data());
    glGenerateMipmap(GL_TEXTURE_2D);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
    glBindTexture(GL_TEXTURE_2D, 0);
}

void Orbital3DView::uploadPyramid(GLuint& texId, const MipPyramid& pyr) {
    if (texId) { glDeleteTextures(1, &texId); texId = 0; }
    if (pyr.mips.empty()) return;

    GLenum internalFmt = pyr.channels == 3 ? GL_RGB32F : GL_RGBA32F;
    GLenum extFmt      = pyr.channels == 3 ? GL_RGB    : GL_RGBA;

    glGenTextures(1, &texId);
    glBindTexture(GL_TEXTURE_2D, texId);
    int w = pyr.width, h = pyr.height;
    for (int lvl = 0; lvl < pyr.levelCount(); lvl++) {
        glTexImage2D(GL_TEXTURE_2D, lvl, internalFmt, w, h, 0, extFmt, GL_FLOAT, pyr.mips[lvl].data());
        w = std::max(1, w / 2);
        h = std::max(1, h / 2);
    }
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAX_LEVEL, pyr.levelCount() - 1);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
    glBindTexture(GL_TEXTURE_2D, 0);
}

void Orbital3DView::uploadOffsetMap(GLuint& texId, const OffsetMapResult& off) {
    if (texId) { glDeleteTextures(1, &texId); texId = 0; }
    if (off.data.empty()) return;

    glGenTextures(1, &texId);
    glBindTexture(GL_TEXTURE_2D, texId);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA32F, off.width, off.height,
                 0, GL_RGBA, GL_FLOAT, off.data.data());
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAX_LEVEL, 0);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
    glBindTexture(GL_TEXTURE_2D, 0);
}

void Orbital3DView::deleteTextures() {
    if (colorTex_)  { glDeleteTextures(1, &colorTex_);  colorTex_  = 0; }
    if (reliefTex_) { glDeleteTextures(1, &reliefTex_); reliefTex_ = 0; }
    if (normalTex_) { glDeleteTextures(1, &normalTex_); normalTex_ = 0; }
    if (offsetTex_) { glDeleteTextures(1, &offsetTex_); offsetTex_ = 0; }
}

// ─── Camera matrices ──────────────────────────────────────────────────────────

glm::mat4 Orbital3DView::projMatrix() const {
    return glm::perspective(glm::radians(45.0f),
                            (float)width() / std::max(1, height()),
                            0.1f, 100.0f);
}

glm::mat4 Orbital3DView::viewMatrix() const {
    glm::vec3 pos(
        zoom_ * sinf(glm::radians(rotY_)) * cosf(glm::radians(rotX_)),
        zoom_ * sinf(glm::radians(rotX_)),
        zoom_ * cosf(glm::radians(rotY_)) * cosf(glm::radians(rotX_)));
    return glm::lookAt(pos, glm::vec3(0.f), glm::vec3(0.f, 1.f, 0.f));
}

glm::mat4 Orbital3DView::modelMatrix() const {
    return glm::scale(glm::mat4(1.f), glm::vec3(meshNormScale_))
         * glm::translate(glm::mat4(1.f), -meshCenter_);
}

// ─── paintGL — deferred uploads then dispatch ─────────────────────────────────

void Orbital3DView::paintGL() {
    // Deferred GL uploads (safe: we are now inside a valid GL context)
    if (primaryMeshDirty_ && primaryMesh_) {
        buildPrimaryBuffers();
        primaryMeshDirty_ = false;
    }
    if (secondaryMeshDirty_ && secondaryMesh_) {
        buildSecondaryBuffers();
        secondaryMeshDirty_ = false;
    }
    if (reliefTexDirty_) {
        if (pendingColorMap_)  { uploadPyramid(colorTex_,  *pendingColorMap_);  pendingColorMap_  = nullptr; }
        if (pendingReliefMap_) { uploadPyramid(reliefTex_, *pendingReliefMap_); pendingReliefMap_ = nullptr; }
        if (pendingNormalMap_) { uploadPyramid(normalTex_, *pendingNormalMap_); pendingNormalMap_ = nullptr; }
        if (pendingOffsetMap_) { uploadOffsetMap(offsetTex_, *pendingOffsetMap_); pendingOffsetMap_ = nullptr; }
        reliefTexDirty_ = false;
    }
    if (colorPyrDirty_ && pendingColorPyr_) {
        if (colorTex_) { glDeleteTextures(1, &colorTex_); colorTex_ = 0; }
        const MipPyramid& p = *pendingColorPyr_;
        if (!p.mips.empty()) {
            GLenum ifmt = p.channels == 3 ? GL_RGB32F  : GL_RGBA32F;
            GLenum efmt = p.channels == 3 ? GL_RGB     : GL_RGBA;
            glGenTextures(1, &colorTex_);
            glBindTexture(GL_TEXTURE_2D, colorTex_);
            glTexImage2D(GL_TEXTURE_2D, 0, ifmt, p.width, p.height, 0, efmt, GL_FLOAT, p.mips[0].data());
            glGenerateMipmap(GL_TEXTURE_2D);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
            glBindTexture(GL_TEXTURE_2D, 0);
        }
        colorPyrDirty_   = false;
        pendingColorPyr_ = nullptr;
    }

    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

    if (!primaryVao_.isCreated() || primaryIndexCount_ == 0) return;

    // UV mode overrides Solid/Textured
    if (uvMode_ && (mode_ == RenderMode::Solid || mode_ == RenderMode::Textured)) {
        paintUV();
        return;
    }

    switch (mode_) {
        case RenderMode::Solid:
        case RenderMode::Textured:
            paintSolid();
            break;
        case RenderMode::Overlay:
            paintOverlay();
            break;
        case RenderMode::UV:
            paintUV();
            break;
        case RenderMode::Relief:
            paintRelief();
            break;
    }
}

// ─── Paint routines ───────────────────────────────────────────────────────────

void Orbital3DView::paintSolid() {
    if (cullFace_) glEnable(GL_CULL_FACE); else glDisable(GL_CULL_FACE);
    glPolygonMode(GL_FRONT_AND_BACK, wireframe_ ? GL_LINE : GL_FILL);

    solidProg_.bind();
    solidProg_.setUniformValue("projection", toQt(projMatrix()));
    solidProg_.setUniformValue("view",       toQt(viewMatrix()));
    solidProg_.setUniformValue("model",      toQt(modelMatrix()));

    bool useTexture = (textured_ || mode_ == RenderMode::Textured) && colorTex_ != 0;
    solidProg_.setUniformValue("useTexture", useTexture);
    if (useTexture) {
        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, colorTex_);
        solidProg_.setUniformValue("textureSampler", 0);
    }

    primaryVao_.bind();
    glDrawElements(GL_TRIANGLES, primaryIndexCount_, GL_UNSIGNED_INT, nullptr);
    primaryVao_.release();
    if (useTexture) glBindTexture(GL_TEXTURE_2D, 0);
    solidProg_.release();

    glPolygonMode(GL_FRONT_AND_BACK, GL_FILL);

    if ((showBoundary_ || showInternal_) && edgeVao_.isCreated() && edgeVertexCount_ > 0) {
        auto proj = projMatrix();
        auto view = viewMatrix();
        auto mdl  = modelMatrix();
        edgeProg_.bind();
        edgeProg_.setUniformValue("projection", toQt(proj));
        edgeProg_.setUniformValue("view",       toQt(view));
        edgeProg_.setUniformValue("model",      toQt(mdl));
        glDepthFunc(GL_LEQUAL);
        glLineWidth(1.5f);
        edgeVao_.bind();
        if (showBoundary_ && boundaryEdgeEnd_ > 0)
            glDrawArrays(GL_LINES, 0, boundaryEdgeEnd_);
        if (showInternal_ && edgeVertexCount_ > boundaryEdgeEnd_)
            glDrawArrays(GL_LINES, boundaryEdgeEnd_, edgeVertexCount_ - boundaryEdgeEnd_);
        edgeVao_.release();
        edgeProg_.release();
        glDepthFunc(GL_LESS);
        glLineWidth(1.0f);
    }
}

void Orbital3DView::paintOverlay() {
    if (cullFace_) glEnable(GL_CULL_FACE); else glDisable(GL_CULL_FACE);
    glPolygonMode(GL_FRONT_AND_BACK, wireframe_ ? GL_LINE : GL_FILL);

    overlayProg_.bind();
    overlayProg_.setUniformValue("projection", toQt(projMatrix()));
    overlayProg_.setUniformValue("view",       toQt(viewMatrix()));
    overlayProg_.setUniformValue("model",      toQt(modelMatrix()));

    auto toVec3 = [](const QColor& c) {
        return QVector3D(c.redF(), c.greenF(), c.blueF());
    };

    overlayProg_.setUniformValue("meshColor", toVec3(primaryColor_));
    primaryVao_.bind();
    glDrawElements(GL_TRIANGLES, primaryIndexCount_, GL_UNSIGNED_INT, nullptr);
    primaryVao_.release();

    if (secondaryVao_.isCreated() && secondaryIndexCount_ > 0) {
        glEnable(GL_BLEND);
        glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
        overlayProg_.setUniformValue("meshColor", toVec3(secondaryColor_));
        secondaryVao_.bind();
        glDrawElements(GL_TRIANGLES, secondaryIndexCount_, GL_UNSIGNED_INT, nullptr);
        secondaryVao_.release();
        glDisable(GL_BLEND);
    }

    overlayProg_.release();
    glPolygonMode(GL_FRONT_AND_BACK, GL_FILL);
}

void Orbital3DView::paintUV() {
    glDisable(GL_DEPTH_TEST);
    glDisable(GL_CULL_FACE);
    glPolygonMode(GL_FRONT_AND_BACK, GL_FILL);

    uvBgProg_.bind();
    uvBgProg_.setUniformValue("hasTexture", colorTex_ != 0);
    if (colorTex_) {
        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, colorTex_);
        uvBgProg_.setUniformValue("textureSampler", 0);
    }
    uvBgVao_.bind();
    glDrawArrays(GL_TRIANGLE_FAN, 0, 4);
    uvBgVao_.release();
    if (colorTex_) glBindTexture(GL_TEXTURE_2D, 0);
    uvBgProg_.release();

    if (uvVao_.isCreated() && primaryIndexCount_ > 0) {
        glPolygonMode(GL_FRONT_AND_BACK, GL_LINE);
        glLineWidth(1.2f);
        uvLineProg_.bind();
        uvLineProg_.setUniformValue("wireColor", QVector4D(0.05f, 0.9f, 0.4f, 1.0f));
        uvVao_.bind();
        glDrawElements(GL_TRIANGLES, primaryIndexCount_, GL_UNSIGNED_INT, nullptr);
        uvVao_.release();
        uvLineProg_.release();
        glPolygonMode(GL_FRONT_AND_BACK, GL_FILL);
        glLineWidth(1.0f);
    }

    glEnable(GL_DEPTH_TEST);
}

void Orbital3DView::paintRelief() {
    // Show placeholder text when textures haven't been generated yet
    if (!hasTextures()) {
        glPolygonMode(GL_FRONT_AND_BACK, GL_FILL);
        glDisable(GL_CULL_FACE);
        // Draw plain background (already cleared to dark), then overlay text via QPainter
        QPainter p(this);
        p.setPen(QColor(160, 160, 160));
        p.setFont(QFont("sans-serif", 14));
        p.drawText(rect(), Qt::AlignCenter,
                   "Run Textures Preparation first to view relief mapping.");
        p.end();
        return;
    }

    if (cullFace_) glEnable(GL_CULL_FACE); else glDisable(GL_CULL_FACE);
    glPolygonMode(GL_FRONT_AND_BACK, wireframe_ ? GL_LINE : GL_FILL);

    reliefProg_.bind();
    reliefProg_.setUniformValue("projection", toQt(projMatrix()));
    reliefProg_.setUniformValue("view",       toQt(viewMatrix()));
    reliefProg_.setUniformValue("model",      toQt(modelMatrix()));

    glm::mat4 v  = viewMatrix();
    glm::vec3 cp = glm::vec3(glm::inverse(v)[3]);
    reliefProg_.setUniformValue("viewPosWorld", QVector3D(cp.x, cp.y, cp.z));

    reliefProg_.setUniformValue("ReliefEnabled", reliefEnabled_);
    reliefProg_.setUniformValue("UseAtlas",      useAtlas_);
    reliefProg_.setUniformValue("LinearSteps",   steps_);
    reliefProg_.setUniformValue("BinarySteps",   binarySteps_);
    reliefProg_.setUniformValue("DepthScale",    depthScale_);
    reliefProg_.setUniformValue("LastMip",       lastMip_);
    reliefProg_.setUniformValue("TexelSize",     texelSize_);
    reliefProg_.setUniformValue("DebugView",     debugView_);

    glActiveTexture(GL_TEXTURE0); glBindTexture(GL_TEXTURE_2D, colorTex_);  glBindSampler(0, 0);
    reliefProg_.setUniformValue("Color_Map", 0);
    glActiveTexture(GL_TEXTURE1); glBindTexture(GL_TEXTURE_2D, reliefTex_); glBindSampler(1, samplerPoint_);
    reliefProg_.setUniformValue("Relief_Map", 1);
    glActiveTexture(GL_TEXTURE2); glBindTexture(GL_TEXTURE_2D, offsetTex_); glBindSampler(2, 0);
    reliefProg_.setUniformValue("Offset_Map", 2);
    glActiveTexture(GL_TEXTURE3); glBindTexture(GL_TEXTURE_2D, normalTex_); glBindSampler(3, 0);
    reliefProg_.setUniformValue("Normal_Map", 3);

    primaryVao_.bind();
    glDrawElements(GL_TRIANGLES, primaryIndexCount_, GL_UNSIGNED_INT, nullptr);
    primaryVao_.release();

    glBindSampler(1, 0);
    reliefProg_.release();
    glPolygonMode(GL_FRONT_AND_BACK, GL_FILL);
}

// ─── Mouse / camera ───────────────────────────────────────────────────────────

void Orbital3DView::mousePressEvent(QMouseEvent* event) {
    lastMouse_ = event->pos();
}

void Orbital3DView::mouseMoveEvent(QMouseEvent* event) {
    if (!(event->buttons() & Qt::LeftButton)) return;
    int dx = event->pos().x() - lastMouse_.x();
    int dy = event->pos().y() - lastMouse_.y();
    rotY_ += dx * 0.5f;
    rotX_ += dy * 0.5f;
    lastMouse_ = event->pos();
    update();
    emit cameraChanged(rotX_, rotY_, zoom_);
}

void Orbital3DView::wheelEvent(QWheelEvent* event) {
    zoom_ -= event->angleDelta().y() * 0.001f;
    zoom_ = std::clamp(zoom_, 0.1f, 20.0f);
    update();
    emit cameraChanged(rotX_, rotY_, zoom_);
}
