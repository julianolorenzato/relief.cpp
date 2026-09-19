/**
 * @file simplification.cpp
 * @brief SimplifyOp implementation: quadric computation, boundary/seam
 *        handling, and the greedy edge-collapse main loop.
 */
#include "relief/op/simplification.h"

#include <algorithm>
#include <cmath>
#include <iostream>
#include <limits>

#include "relief/uv_atlas.h"

namespace op::simplification {

void SimplifyOp::apply(mesh::Mesh& mesh) const {
    mesh_ = &mesh;
    edgeMap_.clear();
    vertexToVertices_.clear();
    lockSeamEdges_ = false;
    boundaryVertex_.clear();

    userLockedVertex_.assign(mesh.vertices.size(), false);
    for (const auto& [a, b] : lockedEdges_) {
        userLockedVertex_[a] = true;
        userLockedVertex_[b] = true;
    }

    run();
}

Eigen::Matrix4d SimplifyOp::quadricFromPlane(double a, double b, double c, double d) {
    Eigen::Vector4d p(a, b, c, d);
    return p * p.transpose();
}

double SimplifyOp::evalQuadric(const Eigen::Matrix4d& Q, double px, double py, double pz) {
    Eigen::Vector4d v(px, py, pz, 1.0);
    return v.dot(Q * v);
}

bool SimplifyOp::solveQuadric(const Eigen::Matrix4d& Q, double& ox, double& oy, double& oz) {
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

Eigen::Vector2d SimplifyOp::interpolateUVAlongSegment(const Eigen::Vector3d& p, const Eigen::Vector3d& a,
                                                       const Eigen::Vector2d& uvA, const Eigen::Vector3d& b,
                                                       const Eigen::Vector2d& uvB) {
    Eigen::Vector3d ab = b - a;
    double len2 = ab.squaredNorm();
    double t = (len2 > 1e-12) ? (p - a).dot(ab) / len2 : 0.5;
    t = std::clamp(t, 0.0, 1.0);
    return uvA + t * (uvB - uvA);
}

// step 1
void SimplifyOp::computeQ() const {
    for (auto &vx : mesh_->vertices) vx.Q.setZero();

    for (auto &fc : mesh_->faces) {
        if (fc.removed) continue;
        int v0 = mesh_->wedges[fc.w[0]].vertex;
        int v1 = mesh_->wedges[fc.w[1]].vertex;
        int v2 = mesh_->wedges[fc.w[2]].vertex;
        const Eigen::Vector3d &p0 = mesh_->vertices[v0].pos;
        const Eigen::Vector3d &p1 = mesh_->vertices[v1].pos;
        const Eigen::Vector3d &p2 = mesh_->vertices[v2].pos;

        Eigen::Vector3d n = (p1 - p0).cross(p2 - p0).normalized();
        double d = -n.dot(p0);

        Eigen::Matrix4d Kp = quadricFromPlane(n.x(), n.y(), n.z(), d);

        mesh_->vertices[v0].Q += Kp;
        mesh_->vertices[v1].Q += Kp;
        mesh_->vertices[v2].Q += Kp;
    }
}

// step 2
bool SimplifyOp::computeCollapse(int v1, int v2, EdgeCollapse &ec) const {
    ec.v1 = v1;
    ec.v2 = v2;

    Eigen::Matrix4d Qbar = mesh_->vertices[v1].Q + mesh_->vertices[v2].Q;

    Eigen::Vector3d mid = (mesh_->vertices[v1].pos + mesh_->vertices[v2].pos) * 0.5;
    double c1 = evalQuadric(Qbar, mesh_->vertices[v1].pos.x(), mesh_->vertices[v1].pos.y(),
                            mesh_->vertices[v1].pos.z());
    double c2 = evalQuadric(Qbar, mesh_->vertices[v2].pos.x(), mesh_->vertices[v2].pos.y(),
                            mesh_->vertices[v2].pos.z());
    double cm = evalQuadric(Qbar, mid.x(), mid.y(), mid.z());

    bool hasOpt = false;
    Eigen::Vector3d opt = Eigen::Vector3d::Zero();
    double cOpt = 0.0;
    if (useOptimalCandidate_) {
        double ox, oy, oz;
        if (solveQuadric(Qbar, ox, oy, oz)) {
            opt = Eigen::Vector3d(ox, oy, oz);
            cOpt = evalQuadric(Qbar, ox, oy, oz);
            hasOpt = true;
        }
    }

    double bestCost = c1;
    ec.target = mesh_->vertices[v1].pos;
    ec.cost = c1;
    if (c2 < bestCost) {
        bestCost = c2;
        ec.target = mesh_->vertices[v2].pos;
        ec.cost = c2;
    }
    if (cm < bestCost) {
        bestCost = cm;
        ec.target = mid;
        ec.cost = cm;
    }
    if (hasOpt && cOpt < bestCost) {
        ec.target = opt;
        ec.cost = cOpt;
    }

    // UV is purely a function of the chosen 3D target: interpolate each
    // (v1,v2) wedge pairing used by the faces incident to this edge along
    // the segment, evaluated at ec.target. Usually one pairing; two if this
    // edge is a UV seam.
    ec.uvTargets.clear();
    for (auto &[w1, w2] : edgeUVPairs(v1, v2)) {
        const Eigen::Vector2d &uvA = mesh_->wedges[w1].uv;
        const Eigen::Vector2d &uvB = mesh_->wedges[w2].uv;
        Eigen::Vector2d merged = interpolateUVAlongSegment(ec.target, mesh_->vertices[v1].pos, uvA,
                                                           mesh_->vertices[v2].pos, uvB);
        ec.uvTargets.push_back({w1, w2, merged});
    }

    return true;
}

std::vector<std::pair<int, int>> SimplifyOp::edgeUVPairs(int v1, int v2) const {
    std::vector<std::pair<int, int>> pairs;
    for (auto &fc : mesh_->faces) {
        if (fc.removed) continue;
        int w1 = -1, w2 = -1;
        for (int k = 0; k < 3; k++) {
            if (mesh_->wedges[fc.w[k]].vertex == v1) w1 = fc.w[k];
            if (mesh_->wedges[fc.w[k]].vertex == v2) w2 = fc.w[k];
        }
        if (w1 < 0 || w2 < 0) continue;
        auto p = std::make_pair(w1, w2);
        if (std::find(pairs.begin(), pairs.end(), p) == pairs.end()) pairs.push_back(p);
    }
    return pairs;
}

// Marcação de vértices de boundary e de seam UV (para BoundaryMode::LockSeamVertices)
// Desde que vértices passaram a ser únicos por posição, uma aresta de seam
// não é mais topológica (é referenciada por 2 faces, uma de cada lado, que
// hoje compartilham vértice de posição) — então precisa ser detectada à
// parte via uv_atlas::findSeamEdges, não aparece em mesh_->buildEdgeToFaces().
void SimplifyOp::markBoundaryVertices() const {
    boundaryVertex_.assign(mesh_->vertices.size(), false);
    for (const auto &[edge, faceIds] : mesh_->buildEdgeToFaces()) {
        if (faceIds.size() != 1) continue;
        boundaryVertex_[edge.first] = true;
        boundaryVertex_[edge.second] = true;
    }
    for (const auto &[v1, v2] : uv_atlas::findSeamEdges(*mesh_)) {
        boundaryVertex_[v1] = true;
        boundaryVertex_[v2] = true;
    }
}

bool SimplifyOp::edgeLocked(int a, int b) const {
    if (lockSeamEdges_ && (boundaryVertex_[a] || boundaryVertex_[b])) return true;
    if (!userLockedVertex_.empty() && (userLockedVertex_[a] || userLockedVertex_[b])) return true;
    return false;
}

// Decide o tipo de candidato para a aresta `edge`
bool SimplifyOp::buildCandidate(mesh::Edge edge, EdgeCollapse &out) const {
    if (edgeLocked(edge.first, edge.second)) return false;
    return computeCollapse(edge.first, edge.second, out);
}

void SimplifyOp::buildQueue(PQ &pq) const {
    while (!pq.empty()) pq.pop();
    edgeMap_.clear();

    for (const auto &entry : mesh_->buildEdgeToFaces()) {
        const auto &edge = entry.first;
        EdgeCollapse ec;
        if (!buildCandidate(edge, ec)) continue;
        edgeMap_[edge] = ec;
        pq.push(ec);
    }
}

void SimplifyOp::mergeVertexPair(
    int keep, int remove, const Eigen::Vector3d &pos,
    const std::vector<std::tuple<int, int, Eigen::Vector2d>> &uvTargets) const {
    mesh::Vertex &kv = mesh_->vertices[keep];
    mesh::Vertex &rv = mesh_->vertices[remove];

    kv.pos = pos;
    kv.Q += rv.Q;

    // Each uvTarget pairing collapses onto one surviving wedge (wKeep): if
    // wRemove stayed a separate (but now identical-valued) wedge, faces on
    // either side of the old edge would explode into two distinct GPU
    // vertices at the same spot (explodeForGPU dedups by wedge id, not
    // value), each only accumulating its own half of the normal -- faceting
    // the shading right along every collapsed edge. So faces still pointing
    // at wRemove are repointed at wKeep below, folded into the face pass
    // already needed for degeneracy checking.
    std::map<int, int> wedgeRemap;  // wRemove -> wKeep
    for (auto &[wKeep, wRemove, mergedUV] : uvTargets) {
        mesh_->wedges[wKeep].uv = mergedUV;
        wedgeRemap[wRemove] = wKeep;
    }

    // Any other wedge still belonging to `remove` (parts of its fan that
    // don't touch this edge) simply moves to `keep`, UV unchanged.
    for (auto &wg : mesh_->wedges)
        if (wg.vertex == remove) wg.vertex = keep;

    rv.removed = true;

    // Repoint faces off any merged-away wedge, and mark now-degenerate faces
    // (two corners collapsed onto the same vertex) removed.
    for (auto &fc : mesh_->faces) {
        if (fc.removed) continue;
        if (!wedgeRemap.empty())
            for (int i = 0; i < 3; i++) {
                auto it = wedgeRemap.find(fc.w[i]);
                if (it != wedgeRemap.end()) fc.w[i] = it->second;
            }
        int a = mesh_->wedges[fc.w[0]].vertex;
        int b = mesh_->wedges[fc.w[1]].vertex;
        int c = mesh_->wedges[fc.w[2]].vertex;
        if (a == b || b == c || a == c) fc.removed = true;
    }

    // Mantém a adjacência viva (necessária para buildCandidate checar se o
    // par espelhado de uma aresta de seam ainda é uma aresta real da malha).
    for (int n : vertexToVertices_[remove]) {
        if (n == keep) continue;
        vertexToVertices_[n].erase(remove);
        vertexToVertices_[n].insert(keep);
        vertexToVertices_[keep].insert(n);
    }
    vertexToVertices_[keep].erase(remove);
    vertexToVertices_[remove].clear();
}

// Passo 3/4: Penalidade para arestas de fronteira (seams e bordas)
void SimplifyOp::addBoundaryConstraints(double weight) const {
    auto edgeToFaces = mesh_->buildEdgeToFaces();

    // Adiciona ao par (a,b) a quádrica de plano perpendicular à face `fi`
    // passando pela aresta (Seção 4 do paper), ponderada por `weight`.
    auto applyEdgeConstraint = [&](int a, int b, int fi) {
        const Eigen::Vector3d &p0 = mesh_->vertices[mesh_->wedges[mesh_->faces[fi].w[0]].vertex].pos;
        const Eigen::Vector3d &p1 = mesh_->vertices[mesh_->wedges[mesh_->faces[fi].w[1]].vertex].pos;
        const Eigen::Vector3d &p2 = mesh_->vertices[mesh_->wedges[mesh_->faces[fi].w[2]].vertex].pos;

        Eigen::Vector3d faceNormal = (p1 - p0).cross(p2 - p0).normalized();
        Eigen::Vector3d edgeDir = (mesh_->vertices[b].pos - mesh_->vertices[a].pos).normalized();

        Eigen::Vector3d cn = faceNormal.cross(edgeDir);
        if (cn.norm() < 1e-10) return false;
        cn.normalize();
        double d = -cn.dot(mesh_->vertices[a].pos);

        Eigen::Matrix4d Kc = quadricFromPlane(cn.x(), cn.y(), cn.z(), d) * weight;
        mesh_->vertices[a].Q += Kc;
        mesh_->vertices[b].Q += Kc;
        return true;
    };

    int boundaryCount = 0;
    for (auto &[edge, faceIds] : edgeToFaces) {
        if (faceIds.size() != 1) continue;
        if (applyEdgeConstraint(edge.first, edge.second, faceIds[0])) ++boundaryCount;
    }
    std::cout << "Boundary constraints: " << boundaryCount << " arestas de fronteira\n";

    if (boundaryMode_ == BoundaryMode::ConstrainSeams) {
        int seamCount = 0;
        for (const auto &edge : uv_atlas::findSeamEdges(*mesh_)) {
            auto it = edgeToFaces.find(edge);
            if (it == edgeToFaces.end() || it->second.empty()) continue;
            if (applyEdgeConstraint(edge.first, edge.second, it->second[0])) ++seamCount;
        }
        std::cout << "Boundary constraints: " << seamCount << " arestas de seam\n";
    }
}

// Recalcula e reenfileira o custo de colapso de toda aresta que toca `keep`
// (chamado logo após `keep` "herdar" as faces de um vértice removido).
void SimplifyOp::refreshAround(int keep, PQ &pq, std::set<mesh::Edge> &invalidEdges) const {
    for (int q : vertexToVertices_[keep]) {
        mesh::Edge ekey(keep, q);
        invalidEdges.erase(ekey);  // pode ter sido invalidada por um colapso anterior; volta a
                                   // ser válida.

        EdgeCollapse nec;
        if (buildCandidate(ekey, nec)) {
            edgeMap_[ekey] = nec;  // novo custo "oficial"; cópias antigas na pq serão descartadas
                                  // por comparação de custo.
            pq.push(nec);
        } else {
            edgeMap_.erase(ekey);  // aresta travada (boundary/seam sem par): remove dos candidatos.
        }
    }
}

// Loop principal

/**
 * @brief Executa o QEM completo: pré-processamento, setup de boundary/seam,
 *        e o loop guloso de colapso de arestas até atingir targetFaces_.
 */
void SimplifyOp::run() const {
    computeQ();  // Q por vértice = soma das quádricas de plano das faces incidentes.

    // Deriva o flag de comportamento a partir do modo de boundary escolhido.
    lockSeamEdges_ = (boundaryMode_ == BoundaryMode::LockSeamVertices);

    if (boundaryMode_ == BoundaryMode::Constraint || boundaryMode_ == BoundaryMode::ConstrainSeams)
        addBoundaryConstraints();  // penaliza mover arestas de borda (e, no modo ConstrainSeams,
                                    // de seam) para fora do plano original.
    if (lockSeamEdges_)
        markBoundaryVertices();  // marca vértices de borda para travar suas arestas em
                                 // buildCandidate/edgeLocked.

    vertexToVertices_ = mesh_->buildVertexToVertices();  // vizinhança vértice->vértice, usada por mergeVertexPair.

    PQ pq;
    buildQueue(pq);  // popula pq e edgeMap_ com um candidato por aresta topológica da malha.

    int current = mesh_->faceCount();
    std::cout << "Iniciando QEM: " << current << " → " << targetFaces_ << " faces\n";

    // std::priority_queue não permite remover/atualizar um elemento do meio.
    // Como cada colapso invalida ou muda o custo de várias arestas vizinhas,
    // usamos "lazy deletion": em vez de mexer na heap, marcamos a chave da
    // aresta como obsoleta aqui e a entrada correspondente na pq é ignorada
    // quando (se) chegar ao topo. edgeMap_ guarda o EdgeCollapse mais recente
    // e confiável de cada aresta; a pq pode conter várias cópias antigas dela.
    std::set<mesh::Edge> invalidEdges;

    // Loop guloso: sempre colapsa a aresta de menor custo ainda válida.
    while (current > targetFaces_ && !pq.empty()) {
        EdgeCollapse ec = pq.top();
        pq.pop();

        mesh::Edge key(ec.v1, ec.v2);

        // As 3 checagens abaixo filtram entradas obsoletas da pq (lazy deletion):
        if (invalidEdges.count(key))
            continue;  // aresta já foi colapsada (ou substituída) antes; esta cópia é lixo.
        if (mesh_->vertices[ec.v1].removed || mesh_->vertices[ec.v2].removed)
            continue;  // um dos vértices já sumiu em outro colapso.
        if (edgeMap_.count(key) && std::abs(edgeMap_[key].cost - ec.cost) > 1e-6)
            continue;  // existe um EdgeCollapse mais recente pra essa aresta (refreshAround já
                       // rodou); esta cópia tem custo desatualizado.

        // Candidato válido e mais barato disponível: consome a aresta antes de
        // aplicar, pra que nenhuma outra cópia dela na pq seja processada de novo depois.
        invalidEdges.insert(key);

        // Apply collapse: move v1 para o ponto-alvo, remove v2 e as faces degeneradas resultantes.
        mergeVertexPair(ec.v1, ec.v2, ec.target, ec.uvTargets);

        current = mesh_->faceCount();

        // A vizinhança de v1 mudou: recalcula custos ao redor.
        refreshAround(ec.v1, pq, invalidEdges);

        if (current % 1000 == 0) std::cout << "  faces restantes: " << current << "\n";
    }

    std::cout << "QEM concluído: " << mesh_->faceCount() << " faces, " << mesh_->vertexCount()
              << " vértices\n";
}

} // namespace op::simplification
