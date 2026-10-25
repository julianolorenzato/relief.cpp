/**
 * @file mesh_view.cpp
 * @brief MeshView implementation: GL buffer construction, shader setup,
 *        and the per-mode paint routines (Solid, Overlay, UV).
 */
#include "gui/views/mesh_view.h"
#include <QColorDialog>
#include <QHBoxLayout>
#include <QMouseEvent>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <algorithm>
#include <iostream>
#include <set>

// ─── Constructor / destructor ─────────────────────────────────────────────────

MeshView::MeshView(RenderMode mode, const QString& title, QWidget* parent)
    : Orbital3DView(parent), mode_(mode)
{
    if (!title.isEmpty())
        setTitle(title);
    if (mode_ == RenderMode::Overlay)
        createColorRow();
}

MeshView::~MeshView() {
    makeCurrent();
    if (secondaryVbo_.isCreated()) secondaryVbo_.destroy();
    if (secondaryEbo_.isCreated()) secondaryEbo_.destroy();
    if (secondaryVao_.isCreated()) secondaryVao_.destroy();
    if (edgeVbo_.isCreated())      edgeVbo_.destroy();
    if (edgeVao_.isCreated())      edgeVao_.destroy();
    if (highlightVbo_.isCreated()) highlightVbo_.destroy();
    if (highlightVao_.isCreated()) highlightVao_.destroy();
    if (uvVbo_.isCreated())        uvVbo_.destroy();
    if (uvVao_.isCreated())        uvVao_.destroy();
    if (uvBgVbo_.isCreated())      uvBgVbo_.destroy();
    if (uvBgVao_.isCreated())      uvBgVao_.destroy();
    deleteTextures();
    doneCurrent();
}

// ─── Public API ───────────────────────────────────────────────────────────────

void MeshView::setMode(RenderMode mode) {
    mode_ = mode;
    update();
}

void MeshView::setMeshes(const mesh::Mesh* primary, const mesh::Mesh* secondary) {
    secondaryMesh_       = secondary;
    secondaryMeshDirty_  = true;
    setMesh(primary);
}

void MeshView::setInteractionMode(InteractionMode m) {
    interactionMode_ = m;
}

// ─── Slots ────────────────────────────────────────────────────────────────────

void MeshView::setPrimaryColor(const QColor& c) {
    primaryColor_ = c;
    if (primaryColorBtn_) applyColorBtnStyle(primaryColorBtn_, c);
    update();
}
void MeshView::setSecondaryColor(const QColor& c) {
    secondaryColor_ = c;
    if (secondaryColorBtn_) applyColorBtnStyle(secondaryColorBtn_, c);
    update();
}

void MeshView::setTextured(bool v)           { textured_     = v; update(); }
void MeshView::setUVMode(bool v)             { uvMode_       = v; update(); }
void MeshView::setShowInternalEdges(bool v)  { showInternal_ = v; update(); }
void MeshView::setShowSeamEdges(bool v)      { showSeam_ = v; update(); }

void MeshView::setBrushRadius(double normalizedRadius) { brushRadius_ = normalizedRadius; }
void MeshView::setBrushAngleThresholdDeg(double degrees) { brushAngleThresholdDeg_ = degrees; }
void MeshView::setBrushPropagationMode(mesh::edgesel::PropagationMode mode) { brushPropagationMode_ = mode; }

void MeshView::clearBrushSelection() {
    brushSelection_.clear();
    highlightDirty_ = true;
    emit selectionChanged((int)brushSelection_.size());
    update();
}

// ─── GL lifecycle ─────────────────────────────────────────────────────────────

void MeshView::initPrograms() {
    createShaders();
}

void MeshView::resizeEvent(QResizeEvent* e) {
    Orbital3DView::resizeEvent(e);
    if (titleLabel_)
        titleLabel_->setGeometry(0, 0, width(), titleLabel_->height());
    int bottomY = height() - kStatsHeight;  // color row sits just above the stats label
    if (colorRow_) {
        bottomY -= colorRow_->height();
        colorRow_->setGeometry(0, bottomY, width(), colorRow_->height());
    }
}

void MeshView::applyColorBtnStyle(QPushButton* btn, const QColor& c) {
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

void MeshView::createColorRow() {
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

void MeshView::setTitle(const QString& title) {
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

void MeshView::createShaders() {
    // Solid / Textured — Phong shading with optional texture
    {
        const char* vert = R"(
            #version 330 core
            layout(location = 0) in vec3 position;
            layout(location = 1) in vec3 normal;
            layout(location = 2) in vec2 texCoord;
            layout(location = 3) in vec4 tangent; // xyz = tangent, w = handedness sign (±1)
            uniform mat4 model; uniform mat4 view; uniform mat4 projection;
            out vec3 FragPos; out vec3 Normal; out vec2 TexCoord;
            out vec3 Tangent; out float Handedness;
            void main() {
                FragPos = vec3(model * vec4(position, 1.0));
                mat3 normalMat = mat3(transpose(inverse(model)));
                vec3 N = normalize(normalMat * normal);
                vec3 T = normalize(normalMat * tangent.xyz);
                T = normalize(T - N * dot(N, T));
                Normal    = N;
                Tangent   = T;
                Handedness = tangent.w;
                TexCoord = texCoord;
                gl_Position = projection * view * vec4(FragPos, 1.0);
            }
        )";
        const char* frag = R"(
            #version 330 core
            in vec3 FragPos; in vec3 Normal; in vec2 TexCoord;
            in vec3 Tangent; in float Handedness;
            out vec4 FragColor;
            uniform sampler2D textureSampler;
            uniform sampler2D normalSampler;
            uniform bool useTexture;
            uniform bool useNormalMap;
            uniform vec3 LightPosWorld;
            void main() {
                vec3 n = normalize(Normal);
                if (useNormalMap) {
                    vec3 T = normalize(Tangent - n * dot(n, Tangent));
                    vec3 B = cross(n, T) * Handedness;
                    mat3 TBN = mat3(T, B, n);
                    vec3 nTS = texture(normalSampler, TexCoord).rgb * 2.0 - 1.0;
                    n = normalize(TBN * nTS);
                }
                vec3 lightDir = normalize(LightPosWorld - FragPos);
                float diff = max(dot(n, lightDir), 0.0);
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
}

// ─── Buffer builders ──────────────────────────────────────────────────────────

void MeshView::onMeshChanged() {
    // The base class has already uploaded the mesh and its normalization; what is
    // left are the MeshView-only buffers and textures derived from it.
    buildEdgeBuffers();
    buildUVBuffers();
    uploadColorFromMesh();
    uploadNormalFromMesh();

    // Brush-selection topology is only valid for the mesh it was built from.
    brushAdjacency_ = mesh::edgesel::FaceAdjacency::build(*mesh_);
    brushFaceNormals_ = mesh::edgesel::computeFaceNormals(*mesh_);
    brushSelection_.clear();
    highlightDirty_ = true;
}

void MeshView::buildSecondaryBuffers() {
    if (!secondaryMesh_ || secondaryMesh_->vertices().empty()) return;
    buildVertexArray(secondaryMesh_, secondaryVbo_, secondaryEbo_, secondaryVao_, secondaryIndexCount_);
}

void MeshView::buildEdgeBuffers() {
    if (!mesh_) return;

    static const float kSeam[3]     = {1.0f, 0.1f,  0.85f};
    static const float kInternal[3] = {0.8f, 0.8f,  0.8f};

    auto edgeToFaces = mesh_->buildEdgeToFaces();
    auto seamPairs = mesh_->findSeamEdges();
    std::set<mesh::Edge> seamSet(seamPairs.begin(), seamPairs.end());

    std::vector<float> lineVerts;
    lineVerts.reserve(edgeToFaces.size() * 2 * 6);

    auto append = [&](const Eigen::Vector3d& a, const Eigen::Vector3d& b, const float* c) {
        lineVerts.push_back((float)a.x()); lineVerts.push_back((float)a.y()); lineVerts.push_back((float)a.z());
        lineVerts.push_back(c[0]); lineVerts.push_back(c[1]); lineVerts.push_back(c[2]);
        lineVerts.push_back((float)b.x()); lineVerts.push_back((float)b.y()); lineVerts.push_back((float)b.z());
        lineVerts.push_back(c[0]); lineVerts.push_back(c[1]); lineVerts.push_back(c[2]);
    };

    for (const auto& [edge, faceIds] : edgeToFaces) {
        if (faceIds.size() == 1 || !seamSet.count(edge)) continue;
        append(mesh_->vertices()[edge.first].pos, mesh_->vertices()[edge.second].pos, kSeam);
    }
    seamEdgeEnd_ = (int)(lineVerts.size() / 6);

    for (const auto& [edge, faceIds] : edgeToFaces) {
        if (faceIds.size() == 1 || seamSet.count(edge)) continue;
        append(mesh_->vertices()[edge.first].pos, mesh_->vertices()[edge.second].pos, kInternal);
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

void MeshView::rebuildHighlightBuffer() {
    static const float kHighlight[3] = {1.0f, 0.6f, 0.0f};

    std::vector<float> lineVerts;
    if (mesh_) {
        lineVerts.reserve(brushSelection_.edges().size() * 2 * 6);
        for (const auto &edge : brushSelection_.edges()) {
            const auto &a = mesh_->vertices()[edge.first].pos;
            const auto &b = mesh_->vertices()[edge.second].pos;
            lineVerts.push_back((float)a.x()); lineVerts.push_back((float)a.y()); lineVerts.push_back((float)a.z());
            lineVerts.push_back(kHighlight[0]); lineVerts.push_back(kHighlight[1]); lineVerts.push_back(kHighlight[2]);
            lineVerts.push_back((float)b.x()); lineVerts.push_back((float)b.y()); lineVerts.push_back((float)b.z());
            lineVerts.push_back(kHighlight[0]); lineVerts.push_back(kHighlight[1]); lineVerts.push_back(kHighlight[2]);
        }
    }
    highlightVertexCount_ = (int)(lineVerts.size() / 6);

    if (!highlightVao_.isCreated()) highlightVao_.create();
    if (!highlightVbo_.isCreated()) highlightVbo_.create();

    highlightVao_.bind();
    highlightVbo_.bind();
    highlightVbo_.allocate(lineVerts.data(), (int)(lineVerts.size() * sizeof(float)));
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 6 * sizeof(float), (void*)0);
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, 6 * sizeof(float), (void*)(3 * sizeof(float)));
    highlightVao_.release();
}

void MeshView::buildUVBuffers() {
    if (!mesh_) return;

    // Must match buildVertexArray's vertex ordering exactly (same exploded
    // (vertex, uv) pairs, since this buffer shares meshEbo_ with it).
    mesh::Mesh::GPUMesh gpu = mesh_->explodeForGPU();
    std::vector<float> uvPos;
    uvPos.reserve(gpu.uvs.size() * 2);
    for (const auto& uv : gpu.uvs) {
        uvPos.push_back((float)uv.x());
        uvPos.push_back((float)uv.y());
    }

    if (!uvVao_.isCreated()) uvVao_.create();
    if (!uvVbo_.isCreated()) uvVbo_.create();

    uvVao_.bind();
    uvVbo_.bind();
    uvVbo_.allocate(uvPos.data(), (int)(uvPos.size() * sizeof(float)));
    meshEbo_.bind();  // Share index buffer — same vertex ordering as buildVertexArray
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

void MeshView::uploadColorFromMesh() {
    if (colorTex_) { glDeleteTextures(1, &colorTex_); colorTex_ = 0; }
    if (!mesh_ || mesh_->colorTexture().data.empty()) return;

    glGenTextures(1, &colorTex_);
    glBindTexture(GL_TEXTURE_2D, colorTex_);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA,
                 mesh_->colorTexture().width, mesh_->colorTexture().height,
                 0, GL_RGBA, GL_UNSIGNED_BYTE, mesh_->colorTexture().data.data());
    glGenerateMipmap(GL_TEXTURE_2D);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
    glBindTexture(GL_TEXTURE_2D, 0);
}

void MeshView::uploadNormalFromMesh() {
    if (normalTex_) { glDeleteTextures(1, &normalTex_); normalTex_ = 0; }
    if (!mesh_ || mesh_->normalTexture().data.empty()) return;

    glGenTextures(1, &normalTex_);
    glBindTexture(GL_TEXTURE_2D, normalTex_);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA,
                 mesh_->normalTexture().width, mesh_->normalTexture().height,
                 0, GL_RGBA, GL_UNSIGNED_BYTE, mesh_->normalTexture().data.data());
    glGenerateMipmap(GL_TEXTURE_2D);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
    glBindTexture(GL_TEXTURE_2D, 0);
}

void MeshView::deleteTextures() {
    if (colorTex_)  { glDeleteTextures(1, &colorTex_);  colorTex_  = 0; }
    if (normalTex_) { glDeleteTextures(1, &normalTex_); normalTex_ = 0; }
}

// ─── Deferred uploads and dispatch ────────────────────────────────────────────

void MeshView::prepareFrame() {
    if (secondaryMeshDirty_ && secondaryMesh_) {
        buildSecondaryBuffers();
        secondaryMeshDirty_ = false;
    }
    if (highlightDirty_) {
        rebuildHighlightBuffer();
        highlightDirty_ = false;
    }
}

void MeshView::paintScene() {
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
    }
}

// ─── Paint routines ───────────────────────────────────────────────────────────

void MeshView::paintSolid() {
    if (cullFace_) glEnable(GL_CULL_FACE); else glDisable(GL_CULL_FACE);
    glPolygonMode(GL_FRONT_AND_BACK, wireframe_ ? GL_LINE : GL_FILL);

    solidProg_.bind();
    solidProg_.setUniformValue("projection", toQt(projMatrix()));
    solidProg_.setUniformValue("view",       toQt(viewMatrix()));
    solidProg_.setUniformValue("model",      toQt(modelMatrix()));
    solidProg_.setUniformValue("LightPosWorld", QVector3D(lightPos_.x, lightPos_.y, lightPos_.z));

    bool useTexture = (textured_ || mode_ == RenderMode::Textured) && colorTex_ != 0;
    solidProg_.setUniformValue("useTexture", useTexture);
    if (useTexture) {
        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, colorTex_);
        solidProg_.setUniformValue("textureSampler", 0);
    }

    bool useNormalMap = useTexture && normalTex_ != 0;
    solidProg_.setUniformValue("useNormalMap", useNormalMap);
    if (useNormalMap) {
        glActiveTexture(GL_TEXTURE1);
        glBindTexture(GL_TEXTURE_2D, normalTex_);
        solidProg_.setUniformValue("normalSampler", 1);
    }

    meshVao_.bind();
    glDrawElements(GL_TRIANGLES, meshIndexCount_, GL_UNSIGNED_INT, nullptr);
    meshVao_.release();
    if (useNormalMap) { glActiveTexture(GL_TEXTURE1); glBindTexture(GL_TEXTURE_2D, 0); }
    if (useTexture)   { glActiveTexture(GL_TEXTURE0); glBindTexture(GL_TEXTURE_2D, 0); }
    solidProg_.release();

    glPolygonMode(GL_FRONT_AND_BACK, GL_FILL);

    if ((showInternal_ || showSeam_) && edgeVao_.isCreated() && edgeVertexCount_ > 0) {
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
        if (showSeam_ && seamEdgeEnd_ > 0)
            glDrawArrays(GL_LINES, 0, seamEdgeEnd_);
        if (showInternal_ && edgeVertexCount_ > seamEdgeEnd_)
            glDrawArrays(GL_LINES, seamEdgeEnd_, edgeVertexCount_ - seamEdgeEnd_);
        edgeVao_.release();
        edgeProg_.release();
        glDepthFunc(GL_LESS);
        glLineWidth(1.0f);
    }

    if (highlightVao_.isCreated() && highlightVertexCount_ > 0) {
        edgeProg_.bind();
        edgeProg_.setUniformValue("projection", toQt(projMatrix()));
        edgeProg_.setUniformValue("view",       toQt(viewMatrix()));
        edgeProg_.setUniformValue("model",      toQt(modelMatrix()));
        glDepthFunc(GL_LEQUAL);
        glLineWidth(3.0f);
        highlightVao_.bind();
        glDrawArrays(GL_LINES, 0, highlightVertexCount_);
        highlightVao_.release();
        edgeProg_.release();
        glDepthFunc(GL_LESS);
        glLineWidth(1.0f);
    }
}

void MeshView::paintOverlay() {
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
    meshVao_.bind();
    glDrawElements(GL_TRIANGLES, meshIndexCount_, GL_UNSIGNED_INT, nullptr);
    meshVao_.release();

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

void MeshView::paintUV() {
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

    if (uvVao_.isCreated() && meshIndexCount_ > 0) {
        glPolygonMode(GL_FRONT_AND_BACK, GL_LINE);
        glLineWidth(1.2f);
        uvLineProg_.bind();
        uvLineProg_.setUniformValue("wireColor", QVector4D(0.05f, 0.9f, 0.4f, 1.0f));
        uvVao_.bind();
        glDrawElements(GL_TRIANGLES, meshIndexCount_, GL_UNSIGNED_INT, nullptr);
        uvVao_.release();
        uvLineProg_.release();
        glPolygonMode(GL_FRONT_AND_BACK, GL_FILL);
        glLineWidth(1.0f);
    }

    glEnable(GL_DEPTH_TEST);
}

// ─── Brush selection ──────────────────────────────────────────────────────────

MeshView::Ray MeshView::screenRay(const QPoint& p) const {
    // Unprojecting through inverse(proj*view*model) lands the ray directly in
    // raw (unnormalized) mesh space, matching mesh::Mesh vertex positions —
    // the same space mesh::edgesel::raycastMesh operates in.
    glm::mat4 invMVP = glm::inverse(projMatrix() * viewMatrix() * modelMatrix());

    float ndcX = (2.0f * (float)p.x()) / std::max(1, width()) - 1.0f;
    float ndcY = 1.0f - (2.0f * (float)p.y()) / std::max(1, height());

    glm::vec4 nearP = invMVP * glm::vec4(ndcX, ndcY, -1.0f, 1.0f);
    glm::vec4 farP  = invMVP * glm::vec4(ndcX, ndcY,  1.0f, 1.0f);
    nearP /= nearP.w;
    farP  /= farP.w;

    Ray ray;
    ray.origin = Eigen::Vector3d(nearP.x, nearP.y, nearP.z);
    Eigen::Vector3d dir = Eigen::Vector3d(farP.x - nearP.x, farP.y - nearP.y, farP.z - nearP.z);
    ray.dir = dir.normalized();
    return ray;
}

void MeshView::brushTouchAt(const QPoint& p, bool erase) {
    if (!mesh_ || brushFaceNormals_.empty()) return;

    Ray ray = screenRay(p);
    mesh::edgesel::RayHit hit = mesh::edgesel::raycastMesh(*mesh_, ray.origin, ray.dir);
    if (!hit.found) return;

    double rawRadius = brushRadius_ / std::max(1e-6f, meshNormScale_);
    double angleRad = brushAngleThresholdDeg_ * (3.14159265358979323846 / 180.0);

    brushSelection_.applyBrush(*mesh_, brushAdjacency_, brushFaceNormals_,
                                hit.faceIdx, hit.point, rawRadius, angleRad,
                                brushPropagationMode_, erase);
    highlightDirty_ = true;
    emit selectionChanged((int)brushSelection_.size());
    update();
}

// ─── Mouse / camera ───────────────────────────────────────────────────────────

void MeshView::mousePressEvent(QMouseEvent* event) {
    Orbital3DView::mousePressEvent(event);
    if (interactionMode_ == InteractionMode::BrushSelect) {
        if (event->button() == Qt::LeftButton) brushTouchAt(event->pos(), false);
        else if (event->button() == Qt::RightButton) brushTouchAt(event->pos(), true);
    }
}

void MeshView::mouseMoveEvent(QMouseEvent* event) {
    if (interactionMode_ == InteractionMode::BrushSelect) {
        if (event->buttons() & Qt::LeftButton) brushTouchAt(event->pos(), false);
        else if (event->buttons() & Qt::RightButton) brushTouchAt(event->pos(), true);
        lastMouse_ = event->pos();
        return;
    }

    Orbital3DView::mouseMoveEvent(event);
}
