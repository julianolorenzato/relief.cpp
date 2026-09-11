/**
 * @file simplification.cpp
 * @brief Simplifier implementation: quadric/envelope computation,
 *        boundary/seam handling, and the greedy edge-collapse main loop.
 */
#include "relief/simplification.h"
#include "relief/uv_atlas.h"
#include <limits>
#include <cmath>
#include <algorithm>
#include <iostream>

namespace simplification {

using mesh::Mesh;
using mesh::Vertex;

int Simplifier::canonicalize(int &a, int &b) const
{
    if (a > b)
        std::swap(a, b);
    return 0;
}

// step 1
void Simplifier::computeQ()
{
    for (auto &vx : mesh_.vertices)
        vx.Q.setZero();

    for (auto &fc : mesh_.faces)
    {
        if (fc.removed)
            continue;
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

// Envelope Constraint (half-spaces)
// A point satisfies an outward-oriented plane (n,d) when n.p + d >= -eps.
// Since this inequality is affine in p, if all 3 corners of a triangle
// satisfy it, every interior point does too — this is what lets the planes
// accumulated per vertex be used as a collapse constraint while still
// guaranteeing the whole footprint (see docs/envelope-simplification-plan.md).

/// @return true if `p` satisfies every outward-oriented half-space in `planes` (within `eps`).
static bool pointSatisfiesPlanes(
    const Eigen::Vector3d &p,
    const std::vector<Eigen::Vector4d> &planes,
    double eps = 1e-6)
{
    for (const auto &pl : planes)
    {
        double val = pl.x() * p.x() + pl.y() * p.y() + pl.z() * p.z() + pl.w();
        if (val < -eps)
            return false;
    }
    return true;
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
static Eigen::Vector2d interpolateUVAlongSegment(
    const Eigen::Vector3d &p,
    const Eigen::Vector3d &a, const Eigen::Vector2d &uvA,
    const Eigen::Vector3d &b, const Eigen::Vector2d &uvB)
{
    Eigen::Vector3d ab = b - a;
    double len2 = ab.squaredNorm();
    double t = (len2 > 1e-12) ? (p - a).dot(ab) / len2 : 0.5;
    t = std::clamp(t, 0.0, 1.0);
    return uvA + t * (uvB - uvA);
}

void Simplifier::computeEnvelope()
{
    for (auto &vx : mesh_.vertices)
        vx.envelope.clear();

    for (auto &fc : mesh_.faces)
    {
        if (fc.removed)
            continue;
        int v0 = mesh_.wedges[fc.w[0]].vertex;
        int v1 = mesh_.wedges[fc.w[1]].vertex;
        int v2 = mesh_.wedges[fc.w[2]].vertex;
        const Eigen::Vector3d &p0 = mesh_.vertices[v0].pos;
        const Eigen::Vector3d &p1 = mesh_.vertices[v1].pos;
        const Eigen::Vector3d &p2 = mesh_.vertices[v2].pos;

        Eigen::Vector3d n = (p1 - p0).cross(p2 - p0).normalized();
        double d = -n.dot(p0);
        Eigen::Vector4d plane(n.x(), n.y(), n.z(), d);

        mesh_.vertices[v0].envelope.push_back(plane);
        mesh_.vertices[v1].envelope.push_back(plane);
        mesh_.vertices[v2].envelope.push_back(plane);
    }
}

// step 2
// Retorna false (sem preencher 'out') quando envelopeConstraint == true e
// nenhum dos 3 candidatos de sempre satisfaz os planos acumulados de v1/v2:
// a aresta não pode colapsar nesse passo.
bool Simplifier::computeCollapse(int v1, int v2, EdgeCollapse &ec) const
{
    ec.v1 = v1;
    ec.v2 = v2;

    Eigen::Matrix4d Qbar = mesh_.vertices[v1].Q + mesh_.vertices[v2].Q;

    Eigen::Vector3d mid = (mesh_.vertices[v1].pos + mesh_.vertices[v2].pos) * 0.5;
    double c1 = evalQuadric(Qbar, mesh_.vertices[v1].pos.x(), mesh_.vertices[v1].pos.y(), mesh_.vertices[v1].pos.z());
    double c2 = evalQuadric(Qbar, mesh_.vertices[v2].pos.x(), mesh_.vertices[v2].pos.y(), mesh_.vertices[v2].pos.z());
    double cm = evalQuadric(Qbar, mid.x(), mid.y(), mid.z());

    bool hasOpt = false;
    Eigen::Vector3d opt = Eigen::Vector3d::Zero();
    double cOpt = 0.0;
    if (useOptimalCandidate)
    {
        double ox, oy, oz;
        if (solveQuadric(Qbar, ox, oy, oz))
        {
            opt = Eigen::Vector3d(ox, oy, oz);
            cOpt = evalQuadric(Qbar, ox, oy, oz);
            hasOpt = true;
        }
    }

    bool found = false;
    if (!envelopeConstraint)
    {
        double bestCost = c1;
        ec.target = mesh_.vertices[v1].pos;
        ec.cost = c1;
        found = true;
        if (c2 < bestCost)
        {
            bestCost = c2;
            ec.target = mesh_.vertices[v2].pos;
            ec.cost = c2;
        }
        if (cm < bestCost)
        {
            bestCost = cm;
            ec.target = mid;
            ec.cost = cm;
        }
        if (hasOpt && cOpt < bestCost)
        {
            ec.target = opt;
            ec.cost = cOpt;
        }
    }
    else
    {
        bool feas1 = pointSatisfiesPlanes(mesh_.vertices[v1].pos, mesh_.vertices[v1].envelope) &&
                     pointSatisfiesPlanes(mesh_.vertices[v1].pos, mesh_.vertices[v2].envelope);
        bool feas2 = pointSatisfiesPlanes(mesh_.vertices[v2].pos, mesh_.vertices[v1].envelope) &&
                     pointSatisfiesPlanes(mesh_.vertices[v2].pos, mesh_.vertices[v2].envelope);
        bool feasM = pointSatisfiesPlanes(mid, mesh_.vertices[v1].envelope) &&
                     pointSatisfiesPlanes(mid, mesh_.vertices[v2].envelope);
        bool feasOpt = hasOpt &&
                       pointSatisfiesPlanes(opt, mesh_.vertices[v1].envelope) &&
                       pointSatisfiesPlanes(opt, mesh_.vertices[v2].envelope);

        double bestCost = std::numeric_limits<double>::infinity();
        if (feas1 && c1 < bestCost)
        {
            ec.target = mesh_.vertices[v1].pos;
            ec.cost = c1;
            bestCost = c1;
            found = true;
        }
        if (feas2 && c2 < bestCost)
        {
            ec.target = mesh_.vertices[v2].pos;
            ec.cost = c2;
            bestCost = c2;
            found = true;
        }
        if (feasM && cm < bestCost)
        {
            ec.target = mid;
            ec.cost = cm;
            bestCost = cm;
            found = true;
        }
        if (feasOpt && cOpt < bestCost)
        {
            ec.target = opt;
            ec.cost = cOpt;
            bestCost = cOpt;
            found = true;
        }
    }

    if (!found)
        return false;

    // UV is purely a function of the chosen 3D target: interpolate each
    // (v1,v2) wedge pairing used by the faces incident to this edge along
    // the segment, evaluated at ec.target. Usually one pairing; two if this
    // edge is a UV seam.
    ec.uvTargets.clear();
    for (auto &[w1, w2] : edgeUVPairs(v1, v2))
    {
        const Eigen::Vector2d &uvA = mesh_.wedges[w1].uv;
        const Eigen::Vector2d &uvB = mesh_.wedges[w2].uv;
        Eigen::Vector2d merged = interpolateUVAlongSegment(ec.target, mesh_.vertices[v1].pos, uvA, mesh_.vertices[v2].pos, uvB);
        ec.uvTargets.push_back({w1, w2, merged});
    }

    return true;
}

std::vector<std::pair<int,int>> Simplifier::edgeUVPairs(int v1, int v2) const
{
    std::vector<std::pair<int,int>> pairs;
    for (auto &fc : mesh_.faces)
    {
        if (fc.removed)
            continue;
        int w1 = -1, w2 = -1;
        for (int k = 0; k < 3; k++)
        {
            if (mesh_.wedges[fc.w[k]].vertex == v1) w1 = fc.w[k];
            if (mesh_.wedges[fc.w[k]].vertex == v2) w2 = fc.w[k];
        }
        if (w1 < 0 || w2 < 0)
            continue;
        auto p = std::make_pair(w1, w2);
        if (std::find(pairs.begin(), pairs.end(), p) == pairs.end())
            pairs.push_back(p);
    }
    return pairs;
}

// Marcação de vértices de boundary e de seam UV (para BoundaryMode::LockSeamVertices)
// Desde que vértices passaram a ser únicos por posição, uma aresta de seam
// não é mais topológica (é referenciada por 2 faces, uma de cada lado, que
// hoje compartilham vértice de posição) — então precisa ser detectada à
// parte via uv_atlas::findSeamEdges, não aparece em mesh_.buildEdgeFaces().
void Simplifier::markBoundaryVertices()
{
    boundaryVertex.assign(mesh_.vertices.size(), false);
    for (const auto &[edge, faceIds] : mesh_.buildEdgeFaces())
    {
        if (faceIds.size() != 1)
            continue;
        boundaryVertex[edge.first] = true;
        boundaryVertex[edge.second] = true;
    }
    for (const auto &[v1, v2] : uv_atlas::findSeamEdges(mesh_))
    {
        boundaryVertex[v1] = true;
        boundaryVertex[v2] = true;
    }
}

// Decide o tipo de candidato para a aresta (p,q)
bool Simplifier::buildCandidate(int p, int q, EdgeCollapse &out) const
{
    canonicalize(p, q);

    if (edgeLocked(p, q))
        return false;
    return computeCollapse(p, q, out);
}

void Simplifier::buildAdjacency()
{
    adjacency.assign(mesh_.vertices.size(), {});
    for (auto &fc : mesh_.faces)
    {
        if (fc.removed)
            continue;
        for (int i = 0; i < 3; i++)
        {
            int a = mesh_.wedges[fc.w[i]].vertex, b = mesh_.wedges[fc.w[(i + 1) % 3]].vertex;
            adjacency[a].insert(b);
            adjacency[b].insert(a);
        }
    }
}

void Simplifier::rebuildQueue(
    std::priority_queue<EdgeCollapse,
                        std::vector<EdgeCollapse>,
                        std::greater<EdgeCollapse>> &pq)
{
    while (!pq.empty())
        pq.pop();
    edgeMap.clear();

    for (auto &face : mesh_.faces)
    {
        if (face.removed)
            continue;
        for (int i = 0; i < 3; i++)
        {
            int a = mesh_.wedges[face.w[i]].vertex, b = mesh_.wedges[face.w[(i + 1) % 3]].vertex;
            canonicalize(a, b);
            auto key = std::make_pair(a, b);
            if (edgeMap.count(key))
                continue;
            EdgeCollapse ec;
            if (!buildCandidate(a, b, ec))
                continue;
            edgeMap[key] = ec;
            pq.push(ec);
        }
    }
}

void Simplifier::mergeVertexPair(int keep, int remove, const Eigen::Vector3d &pos,
                                  const std::vector<std::tuple<int,int,Eigen::Vector2d>> &uvTargets)
{
    Vertex &kv = mesh_.vertices[keep];
    Vertex &rv = mesh_.vertices[remove];

    kv.pos = pos;
    kv.Q += rv.Q;
    kv.envelope.insert(kv.envelope.end(), rv.envelope.begin(), rv.envelope.end());

    // Each uvTarget pairing collapses onto one surviving wedge (wKeep): if
    // wRemove stayed a separate (but now identical-valued) wedge, faces on
    // either side of the old edge would explode into two distinct GPU
    // vertices at the same spot (explodeForGPU dedups by wedge id, not
    // value), each only accumulating its own half of the normal -- faceting
    // the shading right along every collapsed edge. So faces still pointing
    // at wRemove are repointed at wKeep below, folded into the face pass
    // already needed for degeneracy checking.
    std::map<int, int> wedgeRemap; // wRemove -> wKeep
    for (auto &[wKeep, wRemove, mergedUV] : uvTargets)
    {
        mesh_.wedges[wKeep].uv = mergedUV;
        wedgeRemap[wRemove] = wKeep;
    }

    // Any other wedge still belonging to `remove` (parts of its fan that
    // don't touch this edge) simply moves to `keep`, UV unchanged.
    for (auto &wg : mesh_.wedges)
        if (wg.vertex == remove)
            wg.vertex = keep;

    rv.removed = true;

    // Repoint faces off any merged-away wedge, and mark now-degenerate faces
    // (two corners collapsed onto the same vertex) removed.
    for (auto &fc : mesh_.faces)
    {
        if (fc.removed)
            continue;
        if (!wedgeRemap.empty())
            for (int i = 0; i < 3; i++)
            {
                auto it = wedgeRemap.find(fc.w[i]);
                if (it != wedgeRemap.end())
                    fc.w[i] = it->second;
            }
        int a = mesh_.wedges[fc.w[0]].vertex;
        int b = mesh_.wedges[fc.w[1]].vertex;
        int c = mesh_.wedges[fc.w[2]].vertex;
        if (a == b || b == c || a == c)
            fc.removed = true;
    }

    // Mantém a adjacência viva (necessária para buildCandidate checar se o
    // par espelhado de uma aresta de seam ainda é uma aresta real da malha).
    for (int n : adjacency[remove])
    {
        if (n == keep)
            continue;
        adjacency[n].erase(remove);
        adjacency[n].insert(keep);
        adjacency[keep].insert(n);
    }
    adjacency[keep].erase(remove);
    adjacency[remove].clear();
}

// Passo 3/4: Penalidade para arestas de fronteira (seams e bordas)
void Simplifier::addBoundaryConstraints(double weight)
{
    auto edgeFaces = mesh_.buildEdgeFaces();

    int count = 0;
    for (auto &[edge, faceIds] : edgeFaces)
    {
        if (faceIds.size() != 1)
            continue;
        int a = edge.first, b = edge.second;
        int fi = faceIds[0];

        const Eigen::Vector3d &p0 = mesh_.vertices[mesh_.wedges[mesh_.faces[fi].w[0]].vertex].pos;
        const Eigen::Vector3d &p1 = mesh_.vertices[mesh_.wedges[mesh_.faces[fi].w[1]].vertex].pos;
        const Eigen::Vector3d &p2 = mesh_.vertices[mesh_.wedges[mesh_.faces[fi].w[2]].vertex].pos;

        Eigen::Vector3d faceNormal = (p1 - p0).cross(p2 - p0).normalized();
        Eigen::Vector3d edgeDir = (mesh_.vertices[b].pos - mesh_.vertices[a].pos).normalized();

        // Plano perpendicular à face passando pela aresta (Seção 4 do paper)
        Eigen::Vector3d cn = faceNormal.cross(edgeDir);
        if (cn.norm() < 1e-10)
            continue;
        cn.normalize();
        double d = -cn.dot(mesh_.vertices[a].pos);

        Eigen::Matrix4d Kc = quadricFromPlane(cn.x(), cn.y(), cn.z(), d) * weight;
        mesh_.vertices[a].Q += Kc;
        mesh_.vertices[b].Q += Kc;
        ++count;
    }
    std::cout << "Boundary constraints: " << count << " arestas de fronteira\n";
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
void Simplifier::run(int targetFaces)
{
    computeQ(); // Q por vértice = soma das quádricas de plano das faces incidentes.
    if (envelopeConstraint)
        computeEnvelope(); // planos por vértice usados para restringir o alvo do colapso ao footprint original.

    // Deriva o flag de comportamento a partir do modo de boundary escolhido.
    lockSeamEdges = (boundaryMode == BoundaryMode::LockSeamVertices);

    if (boundaryMode == BoundaryMode::Constraint)
        addBoundaryConstraints(); // penaliza mover arestas de borda para fora do plano original.
    if (lockSeamEdges)
        markBoundaryVertices(); // marca vértices de borda para travar suas arestas em buildCandidate/edgeLocked.

    buildAdjacency(); // vizinhança vértice->vértice, usada por mergeVertexPair.

    // Min-heap de candidatos de colapso, ordenada por custo (EdgeCollapse::operator> em simplification.h).
    using PQ = std::priority_queue<EdgeCollapse,
                                   std::vector<EdgeCollapse>,
                                   std::greater<EdgeCollapse>>;
    PQ pq;
    rebuildQueue(pq); // popula pq e edgeMap com um candidato por aresta topológica da malha.

    int current = mesh_.faceCount();
    std::cout << "Iniciando QEM: " << current << " → " << targetFaces << " faces\n";

    // std::priority_queue não permite remover/atualizar um elemento do meio.
    // Como cada colapso invalida ou muda o custo de várias arestas vizinhas,
    // usamos "lazy deletion": em vez de mexer na heap, marcamos a chave da
    // aresta como obsoleta aqui e a entrada correspondente na pq é ignorada
    // quando (se) chegar ao topo. edgeMap guarda o EdgeCollapse mais recente
    // e confiável de cada aresta; a pq pode conter várias cópias antigas dela.
    std::set<std::pair<int, int>> invalidEdges;

    // Recalcula e reenfileira o custo de colapso de toda aresta que toca
    // `keep` (chamado logo após `keep` "herdar" as faces de um vértice
    // removido). Varre mesh_.faces inteiro em vez de usar adjacency[keep]
    // diretamente porque precisa dos dois outros vértices de cada face para
    // formar o par (p, q) -- poderia ser mais barato iterando adjacency e
    // sendo O(grau) em vez de O(faces), mas a versão atual prioriza
    // simplicidade sobre desempenho.
    auto refreshAround = [&](int keep)
    {
        for (auto &fc : mesh_.faces)
        {
            if (fc.removed)
                continue;
            for (int i = 0; i < 3; i++)
            {
                if (mesh_.wedges[fc.w[i]].vertex != keep)
                    continue;
                // fc toca `keep`: revalida a aresta (keep, outro vértice da face) para os outros 2 vértices da face.
                for (int j = 0; j < 3; j++)
                {
                    if (j == i)
                        continue;
                    int p = keep, q = mesh_.wedges[fc.w[j]].vertex;
                    canonicalize(p, q);
                    auto ekey = std::make_pair(p, q);
                    invalidEdges.erase(ekey); // pode ter sido invalidada por um colapso anterior; volta a ser válida.

                    EdgeCollapse nec;
                    if (buildCandidate(p, q, nec))
                    {
                        edgeMap[ekey] = nec; // novo custo "oficial"; cópias antigas na pq serão descartadas por comparação de custo.
                        pq.push(nec);
                    }
                    else
                    {
                        edgeMap.erase(ekey); // aresta travada (boundary/seam sem par): remove dos candidatos.
                    }
                }
            }
        }
    };

    // Loop guloso: sempre colapsa a aresta de menor custo ainda válida.
    while (current > targetFaces && !pq.empty())
    {
        EdgeCollapse ec = pq.top();
        pq.pop();

        int a = ec.v1, b = ec.v2;
        canonicalize(a, b);
        auto key = std::make_pair(a, b);

        // As 3 checagens abaixo filtram entradas obsoletas da pq (lazy deletion):
        if (invalidEdges.count(key))
            continue; // aresta já foi colapsada (ou substituída) antes; esta cópia é lixo.
        if (mesh_.vertices[ec.v1].removed || mesh_.vertices[ec.v2].removed)
            continue; // um dos vértices já sumiu em outro colapso.
        if (edgeMap.count(key) && std::abs(edgeMap[key].cost - ec.cost) > 1e-6)
            continue; // existe um EdgeCollapse mais recente pra essa aresta (refreshAround já rodou); esta cópia tem custo desatualizado.

        // Candidato válido e mais barato disponível: consome a aresta antes de
        // aplicar, pra que nenhuma outra cópia dela na pq seja processada de novo depois.
        invalidEdges.insert(key);

        // Apply collapse: move v1 para o ponto-alvo, remove v2 e as faces degeneradas resultantes.
        mergeVertexPair(ec.v1, ec.v2, ec.target, ec.uvTargets);

        current = mesh_.faceCount();

        // A vizinhança de v1 mudou: recalcula custos ao redor.
        refreshAround(ec.v1);

        if (current % 1000 == 0)
            std::cout << "  faces restantes: " << current << "\n";
    }

    std::cout << "QEM concluído: " << mesh_.faceCount() << " faces, "
              << mesh_.vertexCount() << " vértices\n";
}

} // namespace simplification
