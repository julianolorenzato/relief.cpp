#pragma once
#include <vector>
#include <set>
#include <map>
#include <queue>
#include <cmath>
#include <iostream>
#include <fstream>
#include <sstream>
#include <string>
#include <algorithm>
#include <Eigen/Dense>

// ─── Álgebra de Quádricas ────────────────────────────────────────────────────

// Constrói a matriz de erro fundamental para o plano ax+by+cz+d=0
inline Eigen::Matrix4d quadricFromPlane(double a, double b, double c, double d) {
    Eigen::Vector4d p(a, b, c, d);
    return p * p.transpose();
}

// Avalia o erro quadrático vᵀQv para o ponto (px, py, pz, 1)
inline double evalQuadric(const Eigen::Matrix4d& Q, double px, double py, double pz) {
    Eigen::Vector4d v(px, py, pz, 1.0);
    return v.dot(Q * v);
}

// Resolve o sistema 3×3 que minimiza o erro quadrático.
// Retorna true se o sistema tem solução; posição ótima em (ox, oy, oz)
inline bool solveQuadric(const Eigen::Matrix4d& Q, double& ox, double& oy, double& oz) {
    Eigen::Matrix4d A = Q;
    A(0,3) = A(1,3) = A(2,3) = 0.0;
    A(3,0) = A(3,1) = A(3,2) = 0.0;
    A(3,3) = 1.0;
    double det = A.determinant();
    if (std::abs(det) < 1e-10) return false;
    Eigen::Vector4d rhs(-Q(0,3), -Q(1,3), -Q(2,3), 0.0);
    Eigen::Vector4d result = A.inverse() * rhs;
    ox = result(0); oy = result(1); oz = result(2);
    return true;
}

// ─── Estruturas da Malha ─────────────────────────────────────────────────────

struct Vertex {
    Eigen::Vector3d pos  = Eigen::Vector3d::Zero();
    Eigen::Matrix4d Q    = Eigen::Matrix4d::Zero();
    Eigen::Vector2d uv   = Eigen::Vector2d::Zero();
    bool            removed = false;

    // Planos (n.x,n.y,n.z,d), orientados outward, das faces originais já
    // absorvidas por este vértice ao longo dos colapsos (ver
    // QEMSimplifier::envelopeConstraint). Vazio quando a restrição de
    // envelope está desligada.
    std::vector<Eigen::Vector4d> envelope;
};

struct Face {
    int  v[3];             // índices dos vértices
    bool removed = false;
};

// ─── Candidato de Colapso de Aresta ─────────────────────────────────────────

struct EdgeCollapse {
    int             v1, v2;
    Eigen::Vector3d target   = Eigen::Vector3d::Zero();
    Eigen::Vector2d targetUV = Eigen::Vector2d::Zero();
    double          cost     = 0.0;

    // Quando v1/v2 é uma aresta de seam com par sincronizado (SyncSeamTwins),
    // tv1/tv2 é a aresta espelhada do outro lado da seam, colapsada junto
    // para a mesma posição 'target' (mas com UV própria, em targetUV2).
    int             tv1 = -1, tv2 = -1;
    Eigen::Vector2d targetUV2 = Eigen::Vector2d::Zero();

    bool operator>(const EdgeCollapse& o) const { return cost > o.cost; }
};

// ─── Algoritmo QEM ───────────────────────────────────────────────────────────

enum class BoundaryMode {
    None,             // nenhuma restrição de boundary/seam
    Constraint,        // penalidade suave (quadrica de plano perpendicular)
    LockSeamVertices,   // trava: nunca colapsa aresta com vértice de boundary
    SyncSeamTwins       // colapsa arestas de seam em sincronia com seu par no outro lado
};

class QEMSimplifier {
public:
    std::vector<Vertex> vertices;
    std::vector<Face>   faces;

    BoundaryMode boundaryMode = BoundaryMode::Constraint;

    // Quando true, nenhum colapso pode produzir um vértice que viole os
    // planos de envelope acumulados de v1/v2: garante que a malha
    // simplificada fique sempre do lado de fora (ou sobre) a original.
    // Ver docs/envelope-simplification-plan.md.
    bool envelopeConstraint = false;

    bool loadOBJ(const std::string& path);
    bool saveOBJ(const std::string& path) const;
    bool loadGLTF(const std::string& path);
    bool saveGLTF(const std::string& path) const;

    // Textura extraída do GLTF (RGBA, linha a linha)
    std::vector<uint8_t> textureData;
    int textureWidth  = 0;
    int textureHeight = 0;

    void simplify(int targetFaces, double threshold = 0.0);

    int faceCount() const;
    int vertexCount() const;

    // Classificação de arestas: boundary = referenciada por exatamente 1 face
    // (mesmo critério usado por addBoundaryConstraints). Usado tanto internamente
    // quanto pela visualização (GLWidget) para que ambos enxerguem a mesma coisa.
    struct EdgeInfo {
        int  v1, v2;
        bool boundary;
        int  faceId; // face de referência (sempre válida; única quando boundary == true)
    };
    std::vector<EdgeInfo> classifyEdges() const;

private:
    std::map<std::pair<int,int>, EdgeCollapse> edgeMap;

    void computeQ();
    // Calcula vertices[i].envelope a partir das faces atuais (uma vez, antes
    // do laço de colapsos). Só chamada quando envelopeConstraint == true.
    void computeEnvelope();
    // Monta o candidato de colapso entre os 3 pontos de sempre (v1, v2,
    // ponto médio). Quando envelopeConstraint == true, descarta candidatos
    // que violem os planos acumulados de v1/v2 e retorna false se nenhum dos
    // 3 for viável (aresta não pode colapsar nesse passo).
    bool computeCollapse(int v1, int v2, EdgeCollapse& out) const;
    // Versão sincronizada: combina as quádricas de (v1,v2) e do par espelhado
    // (tv1,tv2) para escolher uma única posição-alvo compartilhada pelos dois.
    bool computeCollapse(int v1, int v2, int tv1, int tv2, EdgeCollapse& out) const;
    void applyCollapse(const EdgeCollapse& ec);
    void mergeVertexPair(int keep, int remove, const Eigen::Vector3d& pos, const Eigen::Vector2d& uv);
    void rebuildQueue(std::priority_queue<EdgeCollapse,
                                         std::vector<EdgeCollapse>,
                                         std::greater<EdgeCollapse>>& pq);
    int canonicalize(int& a, int& b) const;
    std::vector<std::set<int>> adjacency;
    void buildAdjacency();
    void addBoundaryConstraints(double weight = 1000.0);

    // Usado quando boundaryMode == LockSeamVertices ou SyncSeamTwins: nenhuma
    // aresta boundary-interior pode ser candidata a colapso.
    bool lockSeamEdges = false;
    std::vector<bool> boundaryVertex;
    void markBoundaryVertices();
    bool edgeLocked(int a, int b) const {
        return lockSeamEdges && (boundaryVertex[a] || boundaryVertex[b]);
    }

    // Usado quando boundaryMode == SyncSeamTwins.
    // seamTwin[i] = vértice no outro lado da seam, mesma posição 3D, UV
    // diferente; -1 se não houver par único (boundary aberta ou junção de
    // 3+ seams), caso em que o vértice permanece travado como antes.
    bool syncSeamTwins = false;
    std::vector<int> seamTwin;
    void buildSeamTwins();
    // Monta o candidato de colapso para a aresta (p,q), já decidindo se ela
    // deve ser travada, sincronizada com seu par, ou tratada normalmente.
    // Retorna false se a aresta não pode ser colapsada (travada).
    bool buildCandidate(int p, int q, EdgeCollapse& out) const;
};
