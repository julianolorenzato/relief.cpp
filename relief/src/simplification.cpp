/**
 * @file simplification.cpp
 * @brief Simplifier implementation: quadric computation, boundary/seam
 *        handling, and the greedy edge-collapse main loop.
 */
#include "relief/simplification.h"

#include <algorithm>
#include <cmath>
#include <iostream>
#include <limits>

#include "relief/uv_atlas.h"

namespace simplification {

int Simplifier::canonicalize(int &a, int &b) const {
    if (a > b) std::swap(a, b);
    return 0;
}

// step 1
void Simplifier::computeQ() {
    for (auto &vx : mesh_.vertices) vx.Q.setZero();

    for (auto &fc : mesh_.faces) {
        if (fc.removed) continue;
        int v0 = mesh_.wedges[fc.w[0]].vertex;
        int v1 = mesh_.wedges[fc.w[1]].vertex;
        int v2 = mesh_.wedges[fc.w[2]].vertex;
        const Eigen::Vector3d &p0 = mesh_.vertices[v0].pos;
        const Eigen::Vector3d &p1 = mesh_.vertices[v1].pos;
        const Eigen::Vector3d &p2 = mesh_.vertices[v2].pos;

        Eigen::Vector3d n = (p1 - p0).cross(p2 - p0).normalized();
        double d = -n.dot(p0);

        Eigen::Matrix4d Kp = quadricFromPlane(n.x(), n.y(), n.z(), d);

        mesh_.vertices[v0].Q += Kp;
        mesh_.vertices[v1].Q += Kp;
        mesh_.vertices[v2].Q += Kp;
    }
}

/**
 * Interpolates UV coordinates along a segment.
 * @param p Query point.
 * @param a First endpoint of the segment.
 * @param uvA UV coordinate at `a`.
 * @param b Second endpoint of the segment.
 * @param uvB UV coordinate at `b`.
 * @return Interpolated UV coordinate.
 */
static Eigen::Vector2d interpolateUVAlongSegment(const Eigen::Vector3d &p, const Eigen::Vector3d &a,
                                                 const Eigen::Vector2d &uvA,
                                                 const Eigen::Vector3d &b,
                                                 const Eigen::Vector2d &uvB) {
    Eigen::Vector3d ab = b - a;
    double len2 = ab.squaredNorm();
    double t = (len2 > 1e-12) ? (p - a).dot(ab) / len2 : 0.5;
    t = std::clamp(t, 0.0, 1.0);
    return uvA + t * (uvB - uvA);
}

// step 2
bool Simplifier::computeCollapse(int v1, int v2, EdgeCollapse &ec) const {
    ec.v1 = v1;
    ec.v2 = v2;

    Eigen::Matrix4d Qbar = mesh_.vertices[v1].Q + mesh_.vertices[v2].Q;

    Eigen::Vector3d mid = (mesh_.vertices[v1].pos + mesh_.vertices[v2].pos) * 0.5;
    double c1 = evalQuadric(Qbar, mesh_.vertices[v1].pos.x(), mesh_.vertices[v1].pos.y(),
                            mesh_.vertices[v1].pos.z());
    double c2 = evalQuadric(Qbar, mesh_.vertices[v2].pos.x(), mesh_.vertices[v2].pos.y(),
                            mesh_.vertices[v2].pos.z());
    double cm = evalQuadric(Qbar, mid.x(), mid.y(), mid.z());

    bool hasOpt = false;
    Eigen::Vector3d opt = Eigen::Vector3d::Zero();
    double cOpt = 0.0;
    if (useOptimalCandidate) {
        double ox, oy, oz;
        if (solveQuadric(Qbar, ox, oy, oz)) {
            opt = Eigen::Vector3d(ox, oy, oz);
            cOpt = evalQuadric(Qbar, ox, oy, oz);
            hasOpt = true;
        }
    }

    double bestCost = c1;
    ec.target = mesh_.vertices[v1].pos;
    ec.cost = c1;
    if (c2 < bestCost) {
        bestCost = c2;
        ec.target = mesh_.vertices[v2].pos;
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
        const Eigen::Vector2d &uvA = mesh_.wedges[w1].uv;
        const Eigen::Vector2d &uvB = mesh_.wedges[w2].uv;
        Eigen::Vector2d merged = interpolateUVAlongSegment(ec.target, mesh_.vertices[v1].pos, uvA,
                                                           mesh_.vertices[v2].pos, uvB);
        ec.uvTargets.push_back({w1, w2, merged});
    }

    return true;
}

std::vector<std::pair<int, int>> Simplifier::edgeUVPairs(int v1, int v2) const {
    std::vector<std::pair<int, int>> pairs;
    for (auto &fc : mesh_.faces) {
        if (fc.removed) continue;
        int w1 = -1, w2 = -1;
        for (int k = 0; k < 3; k++) {
            if (mesh_.wedges[fc.w[k]].vertex == v1) w1 = fc.w[k];
            if (mesh_.wedges[fc.w[k]].vertex == v2) w2 = fc.w[k];
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
// parte via uv_atlas::findSeamEdges, não aparece em mesh_.buildEdgeToFaces().
void Simplifier::markBoundaryVertices() {
    boundaryVertex.assign(mesh_.vertices.size(), false);
    for (const auto &[edge, faceIds] : mesh_.buildEdgeToFaces()) {
        if (faceIds.size() != 1) continue;
        boundaryVertex[edge.first] = true;
        boundaryVertex[edge.second] = true;
    }
    for (const auto &[v1, v2] : uv_atlas::findSeamEdges(mesh_)) {
        boundaryVertex[v1] = true;
        boundaryVertex[v2] = true;
    }
}

// Decide o tipo de candidato para a aresta (p,q)
bool Simplifier::buildCandidate(int p, int q, EdgeCollapse &out) const {
    canonicalize(p, q);

    if (edgeLocked(p, q)) return false;
    return computeCollapse(p, q, out);
}

std::vector<std::set<int>> Simplifier::buildVertexToVertices() const {
    std::vector<std::set<int>> vertexToVertices(mesh_.vertices.size());
    for (const auto &entry : mesh_.buildEdgeToFaces()) {
        const auto &edge = entry.first;
        vertexToVertices[edge.first].insert(edge.second);
        vertexToVertices[edge.second].insert(edge.first);
    }
    return vertexToVertices;
}

void Simplifier::buildQueue(PQ &pq) {
    while (!pq.empty()) pq.pop();
    edgeMap.clear();

    for (const auto &entry : mesh_.buildEdgeToFaces()) {
        const auto &edge = entry.first;
        EdgeCollapse ec;
        if (!buildCandidate(edge.first, edge.second, ec)) continue;
        edgeMap[edge] = ec;
        pq.push(ec);
    }
}

void Simplifier::mergeVertexPair(
    int keep, int remove, const Eigen::Vector3d &pos,
    const std::vector<std::tuple<int, int, Eigen::Vector2d>> &uvTargets) {
    mesh::Vertex &kv = mesh_.vertices[keep];
    mesh::Vertex &rv = mesh_.vertices[remove];

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
        mesh_.wedges[wKeep].uv = mergedUV;
        wedgeRemap[wRemove] = wKeep;
    }

    // Any other wedge still belonging to `remove` (parts of its fan that
    // don't touch this edge) simply moves to `keep`, UV unchanged.
    for (auto &wg : mesh_.wedges)
        if (wg.vertex == remove) wg.vertex = keep;

    rv.removed = true;

    // Repoint faces off any merged-away wedge, and mark now-degenerate faces
    // (two corners collapsed onto the same vertex) removed.
    for (auto &fc : mesh_.faces) {
        if (fc.removed) continue;
        if (!wedgeRemap.empty())
            for (int i = 0; i < 3; i++) {
                auto it = wedgeRemap.find(fc.w[i]);
                if (it != wedgeRemap.end()) fc.w[i] = it->second;
            }
        int a = mesh_.wedges[fc.w[0]].vertex;
        int b = mesh_.wedges[fc.w[1]].vertex;
        int c = mesh_.wedges[fc.w[2]].vertex;
        if (a == b || b == c || a == c) fc.removed = true;
    }

    // Mantém a adjacência viva (necessária para buildCandidate checar se o
    // par espelhado de uma aresta de seam ainda é uma aresta real da malha).
    for (int n : vertexToVertices[remove]) {
        if (n == keep) continue;
        vertexToVertices[n].erase(remove);
        vertexToVertices[n].insert(keep);
        vertexToVertices[keep].insert(n);
    }
    vertexToVertices[keep].erase(remove);
    vertexToVertices[remove].clear();
}

// Passo 3/4: Penalidade para arestas de fronteira (seams e bordas)
void Simplifier::addBoundaryConstraints(double weight) {
    auto edgeToFaces = mesh_.buildEdgeToFaces();

    // Adiciona ao par (a,b) a quádrica de plano perpendicular à face `fi`
    // passando pela aresta (Seção 4 do paper), ponderada por `weight`.
    auto applyEdgeConstraint = [&](int a, int b, int fi) {
        const Eigen::Vector3d &p0 = mesh_.vertices[mesh_.wedges[mesh_.faces[fi].w[0]].vertex].pos;
        const Eigen::Vector3d &p1 = mesh_.vertices[mesh_.wedges[mesh_.faces[fi].w[1]].vertex].pos;
        const Eigen::Vector3d &p2 = mesh_.vertices[mesh_.wedges[mesh_.faces[fi].w[2]].vertex].pos;

        Eigen::Vector3d faceNormal = (p1 - p0).cross(p2 - p0).normalized();
        Eigen::Vector3d edgeDir = (mesh_.vertices[b].pos - mesh_.vertices[a].pos).normalized();

        Eigen::Vector3d cn = faceNormal.cross(edgeDir);
        if (cn.norm() < 1e-10) return false;
        cn.normalize();
        double d = -cn.dot(mesh_.vertices[a].pos);

        Eigen::Matrix4d Kc = quadricFromPlane(cn.x(), cn.y(), cn.z(), d) * weight;
        mesh_.vertices[a].Q += Kc;
        mesh_.vertices[b].Q += Kc;
        return true;
    };

    int boundaryCount = 0;
    for (auto &[edge, faceIds] : edgeToFaces) {
        if (faceIds.size() != 1) continue;
        if (applyEdgeConstraint(edge.first, edge.second, faceIds[0])) ++boundaryCount;
    }
    std::cout << "Boundary constraints: " << boundaryCount << " arestas de fronteira\n";

    if (boundaryMode == BoundaryMode::ConstrainSeams) {
        int seamCount = 0;
        for (const auto &[a, b] : uv_atlas::findSeamEdges(mesh_)) {
            auto it = edgeToFaces.find(std::make_pair(a, b));
            if (it == edgeToFaces.end() || it->second.empty()) continue;
            if (applyEdgeConstraint(a, b, it->second[0])) ++seamCount;
        }
        std::cout << "Boundary constraints: " << seamCount << " arestas de seam\n";
    }
}

// Recalcula e reenfileira o custo de colapso de toda aresta que toca `keep`
// (chamado logo após `keep` "herdar" as faces de um vértice removido).
void Simplifier::refreshAround(int keep, PQ &pq, std::set<std::pair<int, int>> &invalidEdges) {
    for (int q : vertexToVertices[keep]) {
        int p = keep;
        canonicalize(p, q);
        auto ekey = std::make_pair(p, q);
        invalidEdges.erase(ekey);  // pode ter sido invalidada por um colapso anterior; volta a
                                   // ser válida.

        EdgeCollapse nec;
        if (buildCandidate(p, q, nec)) {
            edgeMap[ekey] = nec;  // novo custo "oficial"; cópias antigas na pq serão descartadas
                                  // por comparação de custo.
            pq.push(nec);
        } else {
            edgeMap.erase(ekey);  // aresta travada (boundary/seam sem par): remove dos candidatos.
        }
    }
}

// Loop principal

/**
 * @brief Executa o QEM completo: pré-processamento, setup de boundary/seam,
 *        e o loop guloso de colapso de arestas até atingir targetFaces.
 * @param targetFaces Número de faces desejado ao final.
 * @param threshold Se > 0, também considera colapsar pares de vértices não
 *        adjacentes cuja distância seja <= threshold (fecha gaps espaciais
 *        que não são arestas topológicas).
 */
void Simplifier::run(int targetFaces) {
    computeQ();  // Q por vértice = soma das quádricas de plano das faces incidentes.

    // Deriva o flag de comportamento a partir do modo de boundary escolhido.
    lockSeamEdges = (boundaryMode == BoundaryMode::LockSeamVertices);

    if (boundaryMode == BoundaryMode::Constraint || boundaryMode == BoundaryMode::ConstrainSeams)
        addBoundaryConstraints();  // penaliza mover arestas de borda (e, no modo ConstrainSeams,
                                    // de seam) para fora do plano original.
    if (lockSeamEdges)
        markBoundaryVertices();  // marca vértices de borda para travar suas arestas em
                                 // buildCandidate/edgeLocked.

    vertexToVertices = buildVertexToVertices();  // vizinhança vértice->vértice, usada por mergeVertexPair.

    PQ pq;
    buildQueue(pq);  // popula pq e edgeMap com um candidato por aresta topológica da malha.

    int current = mesh_.faceCount();
    std::cout << "Iniciando QEM: " << current << " → " << targetFaces << " faces\n";

    // std::priority_queue não permite remover/atualizar um elemento do meio.
    // Como cada colapso invalida ou muda o custo de várias arestas vizinhas,
    // usamos "lazy deletion": em vez de mexer na heap, marcamos a chave da
    // aresta como obsoleta aqui e a entrada correspondente na pq é ignorada
    // quando (se) chegar ao topo. edgeMap guarda o EdgeCollapse mais recente
    // e confiável de cada aresta; a pq pode conter várias cópias antigas dela.
    std::set<std::pair<int, int>> invalidEdges;

    // Loop guloso: sempre colapsa a aresta de menor custo ainda válida.
    while (current > targetFaces && !pq.empty()) {
        EdgeCollapse ec = pq.top();
        pq.pop();

        int a = ec.v1, b = ec.v2;
        canonicalize(a, b);
        auto key = std::make_pair(a, b);

        // As 3 checagens abaixo filtram entradas obsoletas da pq (lazy deletion):
        if (invalidEdges.count(key))
            continue;  // aresta já foi colapsada (ou substituída) antes; esta cópia é lixo.
        if (mesh_.vertices[ec.v1].removed || mesh_.vertices[ec.v2].removed)
            continue;  // um dos vértices já sumiu em outro colapso.
        if (edgeMap.count(key) && std::abs(edgeMap[key].cost - ec.cost) > 1e-6)
            continue;  // existe um EdgeCollapse mais recente pra essa aresta (refreshAround já
                       // rodou); esta cópia tem custo desatualizado.

        // Candidato válido e mais barato disponível: consome a aresta antes de
        // aplicar, pra que nenhuma outra cópia dela na pq seja processada de novo depois.
        invalidEdges.insert(key);

        // Apply collapse: move v1 para o ponto-alvo, remove v2 e as faces degeneradas resultantes.
        mergeVertexPair(ec.v1, ec.v2, ec.target, ec.uvTargets);

        current = mesh_.faceCount();

        // A vizinhança de v1 mudou: recalcula custos ao redor.
        refreshAround(ec.v1, pq, invalidEdges);

        if (current % 1000 == 0) std::cout << "  faces restantes: " << current << "\n";
    }

    std::cout << "QEM concluído: " << mesh_.faceCount() << " faces, " << mesh_.vertexCount()
              << " vértices\n";
}

}  // namespace simplification
