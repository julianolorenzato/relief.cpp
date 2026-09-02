/**
 * @file simplification.cpp
 * @brief Simplifier implementation: quadric/envelope computation,
 *        boundary/seam handling, and the greedy edge-collapse main loop.
 */
#include "relief/simplification.h"
#include <limits>
#include <cmath>
#include <algorithm>
#include <iostream>

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
        const Eigen::Vector3d &p0 = mesh_.vertices[fc.v[0]].pos;
        const Eigen::Vector3d &p1 = mesh_.vertices[fc.v[1]].pos;
        const Eigen::Vector3d &p2 = mesh_.vertices[fc.v[2]].pos;

        Eigen::Vector3d n = (p1 - p0).cross(p2 - p0).normalized();
        double d = -n.dot(p0);

        Eigen::Matrix4d Kp = quadricFromPlane(n.x(), n.y(), n.z(), d);

        mesh_.vertices[fc.v[0]].Q += Kp;
        mesh_.vertices[fc.v[1]].Q += Kp;
        mesh_.vertices[fc.v[2]].Q += Kp;
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
        const Eigen::Vector3d &p0 = mesh_.vertices[fc.v[0]].pos;
        const Eigen::Vector3d &p1 = mesh_.vertices[fc.v[1]].pos;
        const Eigen::Vector3d &p2 = mesh_.vertices[fc.v[2]].pos;

        Eigen::Vector3d n = (p1 - p0).cross(p2 - p0).normalized();
        double d = -n.dot(p0);
        Eigen::Vector4d plane(n.x(), n.y(), n.z(), d);

        mesh_.vertices[fc.v[0]].envelope.push_back(plane);
        mesh_.vertices[fc.v[1]].envelope.push_back(plane);
        mesh_.vertices[fc.v[2]].envelope.push_back(plane);
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
    Eigen::Vector2d midUV = (mesh_.vertices[v1].uv + mesh_.vertices[v2].uv) * 0.5;
    double c1 = evalQuadric(Qbar, mesh_.vertices[v1].pos.x(), mesh_.vertices[v1].pos.y(), mesh_.vertices[v1].pos.z());
    double c2 = evalQuadric(Qbar, mesh_.vertices[v2].pos.x(), mesh_.vertices[v2].pos.y(), mesh_.vertices[v2].pos.z());
    double cm = evalQuadric(Qbar, mid.x(), mid.y(), mid.z());

    bool hasOpt = false;
    Eigen::Vector3d opt = Eigen::Vector3d::Zero();
    Eigen::Vector2d optUV = Eigen::Vector2d::Zero();
    double cOpt = 0.0;
    if (useOptimalCandidate)
    {
        double ox, oy, oz;
        if (solveQuadric(Qbar, ox, oy, oz))
        {
            opt = Eigen::Vector3d(ox, oy, oz);
            optUV = interpolateUVAlongSegment(opt, mesh_.vertices[v1].pos, mesh_.vertices[v1].uv, mesh_.vertices[v2].pos, mesh_.vertices[v2].uv);
            cOpt = evalQuadric(Qbar, ox, oy, oz);
            hasOpt = true;
        }
    }

    if (!envelopeConstraint)
    {
        double bestCost = c1;
        ec.target = mesh_.vertices[v1].pos;
        ec.targetUV = mesh_.vertices[v1].uv;
        ec.cost = c1;
        if (c2 < bestCost)
        {
            bestCost = c2;
            ec.target = mesh_.vertices[v2].pos;
            ec.targetUV = mesh_.vertices[v2].uv;
            ec.cost = c2;
        }
        if (cm < bestCost)
        {
            bestCost = cm;
            ec.target = mid;
            ec.targetUV = midUV;
            ec.cost = cm;
        }
        if (hasOpt && cOpt < bestCost)
        {
            ec.target = opt;
            ec.targetUV = optUV;
            ec.cost = cOpt;
        }
        return true;
    }

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
    bool found = false;
    if (feas1 && c1 < bestCost)
    {
        ec.target = mesh_.vertices[v1].pos;
        ec.targetUV = mesh_.vertices[v1].uv;
        ec.cost = c1;
        bestCost = c1;
        found = true;
    }
    if (feas2 && c2 < bestCost)
    {
        ec.target = mesh_.vertices[v2].pos;
        ec.targetUV = mesh_.vertices[v2].uv;
        ec.cost = c2;
        bestCost = c2;
        found = true;
    }
    if (feasM && cm < bestCost)
    {
        ec.target = mid;
        ec.targetUV = midUV;
        ec.cost = cm;
        bestCost = cm;
        found = true;
    }
    if (feasOpt && cOpt < bestCost)
    {
        ec.target = opt;
        ec.targetUV = optUV;
        ec.cost = cOpt;
        bestCost = cOpt;
        found = true;
    }

    return found;
}

// collapso sincronizado em uma seam
// Combina as quádricas de ambos os lados para escolher uma única posição-alvo;
// como twins compartilham a mesma posição 3D, os candidatos de posição
// (pos v1, pos v2, ponto médio) já são idênticos nos dois lados. A UV de cada
// lado é interpolada independentemente.

bool Simplifier::computeCollapse(int v1, int v2, int tv1, int tv2, EdgeCollapse &ec) const
{
    ec.v1 = v1;
    ec.v2 = v2;
    ec.tv1 = tv1;
    ec.tv2 = tv2;

    Eigen::Matrix4d Qbar = mesh_.vertices[v1].Q + mesh_.vertices[v2].Q + mesh_.vertices[tv1].Q + mesh_.vertices[tv2].Q;

    Eigen::Vector3d mid = (mesh_.vertices[v1].pos + mesh_.vertices[v2].pos) * 0.5;
    Eigen::Vector2d midUV1 = (mesh_.vertices[v1].uv + mesh_.vertices[v2].uv) * 0.5;
    Eigen::Vector2d midUV2 = (mesh_.vertices[tv1].uv + mesh_.vertices[tv2].uv) * 0.5;

    double c1 = evalQuadric(Qbar, mesh_.vertices[v1].pos.x(), mesh_.vertices[v1].pos.y(), mesh_.vertices[v1].pos.z());
    double c2 = evalQuadric(Qbar, mesh_.vertices[v2].pos.x(), mesh_.vertices[v2].pos.y(), mesh_.vertices[v2].pos.z());
    double cm = evalQuadric(Qbar, mid.x(), mid.y(), mid.z());

    bool hasOpt = false;
    Eigen::Vector3d opt = Eigen::Vector3d::Zero();
    Eigen::Vector2d optUV1 = Eigen::Vector2d::Zero(), optUV2 = Eigen::Vector2d::Zero();
    double cOpt = 0.0;
    if (useOptimalCandidate)
    {
        double ox, oy, oz;
        if (solveQuadric(Qbar, ox, oy, oz))
        {
            opt = Eigen::Vector3d(ox, oy, oz);
            optUV1 = interpolateUVAlongSegment(opt, mesh_.vertices[v1].pos, mesh_.vertices[v1].uv, mesh_.vertices[v2].pos, mesh_.vertices[v2].uv);
            optUV2 = interpolateUVAlongSegment(opt, mesh_.vertices[tv1].pos, mesh_.vertices[tv1].uv, mesh_.vertices[tv2].pos, mesh_.vertices[tv2].uv);
            cOpt = evalQuadric(Qbar, ox, oy, oz);
            hasOpt = true;
        }
    }

    if (!envelopeConstraint)
    {
        double bestCost = c1;
        ec.target = mesh_.vertices[v1].pos;
        ec.targetUV = mesh_.vertices[v1].uv;
        ec.targetUV2 = mesh_.vertices[tv1].uv;
        ec.cost = c1;
        if (c2 < bestCost)
        {
            bestCost = c2;
            ec.target = mesh_.vertices[v2].pos;
            ec.targetUV = mesh_.vertices[v2].uv;
            ec.targetUV2 = mesh_.vertices[tv2].uv;
            ec.cost = c2;
        }
        if (cm < bestCost)
        {
            bestCost = cm;
            ec.target = mid;
            ec.targetUV = midUV1;
            ec.targetUV2 = midUV2;
            ec.cost = cm;
        }
        if (hasOpt && cOpt < bestCost)
        {
            ec.target = opt;
            ec.targetUV = optUV1;
            ec.targetUV2 = optUV2;
            ec.cost = cOpt;
        }
        return true;
    }

    // As 4 quádricas combinadas vêm de v1, v2, tv1 e tv2 — então o ponto-alvo
    // precisa respeitar os planos acumulados pelos 4, não só pelo par (v1,v2).
    auto feasible = [&](const Eigen::Vector3d &p)
    {
        return pointSatisfiesPlanes(p, mesh_.vertices[v1].envelope) &&
               pointSatisfiesPlanes(p, mesh_.vertices[v2].envelope) &&
               pointSatisfiesPlanes(p, mesh_.vertices[tv1].envelope) &&
               pointSatisfiesPlanes(p, mesh_.vertices[tv2].envelope);
    };
    bool feas1 = feasible(mesh_.vertices[v1].pos);
    bool feas2 = feasible(mesh_.vertices[v2].pos);
    bool feasM = feasible(mid);
    bool feasOpt = hasOpt && feasible(opt);

    double bestCost = std::numeric_limits<double>::infinity();
    bool found = false;
    if (feas1 && c1 < bestCost)
    {
        ec.target = mesh_.vertices[v1].pos;
        ec.targetUV = mesh_.vertices[v1].uv;
        ec.targetUV2 = mesh_.vertices[tv1].uv;
        ec.cost = c1;
        bestCost = c1;
        found = true;
    }
    if (feas2 && c2 < bestCost)
    {
        ec.target = mesh_.vertices[v2].pos;
        ec.targetUV = mesh_.vertices[v2].uv;
        ec.targetUV2 = mesh_.vertices[tv2].uv;
        ec.cost = c2;
        bestCost = c2;
        found = true;
    }
    if (feasM && cm < bestCost)
    {
        ec.target = mid;
        ec.targetUV = midUV1;
        ec.targetUV2 = midUV2;
        ec.cost = cm;
        bestCost = cm;
        found = true;
    }
    if (feasOpt && cOpt < bestCost)
    {
        ec.target = opt;
        ec.targetUV = optUV1;
        ec.targetUV2 = optUV2;
        ec.cost = cOpt;
        bestCost = cOpt;
        found = true;
    }

    return found;
}

// Marcação de vértices de boundary (para BoundaryMode::LockSeamVertices)
void Simplifier::markBoundaryVertices()
{
    boundaryVertex.assign(mesh_.vertices.size(), false);
    for (const auto &e : mesh_.classifyEdges())
    {
        if (!e.boundary)
            continue;
        boundaryVertex[e.v1] = true;
        boundaryVertex[e.v2] = true;
    }
}

// Pareamento de vértices-twin de seam (para BoundaryMode::SyncSeamTwins)
// Agrupa vértices de boundary por posição 3D. Um grupo de tamanho 2 é um par
// twin (mesma posição, lados opostos da seam). Grupos de tamanho 1 (boundary
// aberta, sem seam) ou >2 (junção de 3+ seams) ficam sem par e permanecem
// travados, como no modo LockSeamVertices.

void Simplifier::buildSeamTwins()
{
    seamTwin.assign(mesh_.vertices.size(), -1);

    using PosKey = std::tuple<double, double, double>;
    std::map<PosKey, std::vector<int>> groups;
    for (int i = 0; i < (int)mesh_.vertices.size(); i++)
    {
        if (mesh_.vertices[i].removed || !boundaryVertex[i])
            continue;
        const auto &p = mesh_.vertices[i].pos;
        groups[{p.x(), p.y(), p.z()}].push_back(i);
    }

    int pairCount = 0;
    for (auto &[key, idxs] : groups)
    {
        if (idxs.size() == 2)
        {
            seamTwin[idxs[0]] = idxs[1];
            seamTwin[idxs[1]] = idxs[0];
            ++pairCount;
        }
    }
    std::cout << "Seam twins: " << pairCount << " pares encontrados\n";
}

// Decide o tipo de candidato para a aresta (p,q)

bool Simplifier::buildCandidate(int p, int q, EdgeCollapse &out) const
{
    canonicalize(p, q);

    if (syncSeamTwins && boundaryVertex[p] && boundaryVertex[q])
    {
        if (seamTwin[p] < 0 || seamTwin[q] < 0)
            return false; // sem par: travada
        int tp = seamTwin[p], tq = seamTwin[q];
        if (!adjacency[tp].count(tq))
            return false; // par não forma aresta real: travada

        // Evita construir o candidato duas vezes (uma por lado da seam): só o
        // lado "menor" lexicograficamente monta o par; o outro é coberto por ele.
        auto keyPQ = std::minmax(p, q);
        auto keyTT = std::minmax(tp, tq);
        if (keyTT < keyPQ)
            return false;

        return computeCollapse(p, q, tp, tq, out);
    }

    if (edgeLocked(p, q))
        return false;
    return computeCollapse(p, q, out);
}

// Adjacência

void Simplifier::buildAdjacency()
{
    adjacency.assign(mesh_.vertices.size(), {});
    for (auto &fc : mesh_.faces)
    {
        if (fc.removed)
            continue;
        for (int i = 0; i < 3; i++)
        {
            int a = fc.v[i], b = fc.v[(i + 1) % 3];
            adjacency[a].insert(b);
            adjacency[b].insert(a);
        }
    }
}

// Construir a fila de prioridade

void Simplifier::rebuildQueue(
    std::priority_queue<EdgeCollapse,
                        std::vector<EdgeCollapse>,
                        std::greater<EdgeCollapse>> &pq)
{
    while (!pq.empty())
        pq.pop();
    edgeMap.clear();

    for (auto &fc : mesh_.faces)
    {
        if (fc.removed)
            continue;
        for (int i = 0; i < 3; i++)
        {
            int a = fc.v[i], b = fc.v[(i + 1) % 3];
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

//  Aplicar colapso

void Simplifier::mergeVertexPair(int keep, int remove, const Eigen::Vector3d &pos, const Eigen::Vector2d &uv)
{
    mesh_.vertices[keep].pos = pos;
    mesh_.vertices[keep].uv = uv;
    mesh_.vertices[keep].Q += mesh_.vertices[remove].Q;
    mesh_.vertices[keep].envelope.insert(mesh_.vertices[keep].envelope.end(),
                                         mesh_.vertices[remove].envelope.begin(),
                                         mesh_.vertices[remove].envelope.end());
    mesh_.vertices[remove].removed = true;

    for (auto &fc : mesh_.faces)
    {
        if (fc.removed)
            continue;
        bool ref = false;
        for (int i = 0; i < 3; i++)
        {
            if (fc.v[i] == remove)
            {
                fc.v[i] = keep;
                ref = true;
            }
        }
        if (ref)
        {
            if (fc.v[0] == fc.v[1] || fc.v[1] == fc.v[2] || fc.v[0] == fc.v[2])
                fc.removed = true;
        }
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

void Simplifier::applyCollapse(const EdgeCollapse &ec)
{
    mergeVertexPair(ec.v1, ec.v2, ec.target, ec.targetUV);
    if (ec.tv1 >= 0)
        mergeVertexPair(ec.tv1, ec.tv2, ec.target, ec.targetUV2);
}

// Passo 3/4: Penalidade para arestas de fronteira (seams e bordas)

void Simplifier::addBoundaryConstraints(double weight)
{
    auto edges = mesh_.classifyEdges();

    int count = 0;
    for (auto &e : edges)
    {
        if (!e.boundary)
            continue;
        int a = e.v1, b = e.v2;
        int fi = e.faceId;

        const Eigen::Vector3d &p0 = mesh_.vertices[mesh_.faces[fi].v[0]].pos;
        const Eigen::Vector3d &p1 = mesh_.vertices[mesh_.faces[fi].v[1]].pos;
        const Eigen::Vector3d &p2 = mesh_.vertices[mesh_.faces[fi].v[2]].pos;

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

void Simplifier::run(int targetFaces, double threshold)
{

// Fundir vértices coincidentes (mesma posição E mesmo UV)
// Vértices na mesma posição com UV diferente são seam pairs: não fundir.
#if 1
    {
        using PosUVKey = std::tuple<double, double, double, double, double>;
        std::map<PosUVKey, int> posToIdx;
        int mergedCount = 0;
        for (int i = 0; i < (int)mesh_.vertices.size(); i++)
        {
            if (mesh_.vertices[i].removed)
                continue;
            PosUVKey key{mesh_.vertices[i].pos.x(), mesh_.vertices[i].pos.y(), mesh_.vertices[i].pos.z(),
                         mesh_.vertices[i].uv.x(), mesh_.vertices[i].uv.y()};
            auto res = posToIdx.emplace(key, i);
            if (!res.second)
            {
                int keep = res.first->second;
                mesh_.vertices[i].removed = true;
                ++mergedCount;
                for (auto &fc : mesh_.faces)
                {
                    if (fc.removed)
                        continue;
                    bool ref = false;
                    for (int k = 0; k < 3; k++)
                        if (fc.v[k] == i)
                        {
                            fc.v[k] = keep;
                            ref = true;
                        }
                    if (ref && (fc.v[0] == fc.v[1] || fc.v[1] == fc.v[2] || fc.v[0] == fc.v[2]))
                        fc.removed = true;
                }
            }
        }
        std::cout << "Fusao de vertices coincidentes (pos+UV): " << mergedCount << " vertices fundidos\n";
    }
#endif

    computeQ();
    if (envelopeConstraint)
        computeEnvelope();
    syncSeamTwins = (boundaryMode == BoundaryMode::SyncSeamTwins);
    lockSeamEdges = (boundaryMode == BoundaryMode::LockSeamVertices || syncSeamTwins);
    if (boundaryMode == BoundaryMode::Constraint || syncSeamTwins)
        addBoundaryConstraints();
    if (lockSeamEdges)
        markBoundaryVertices();
    buildAdjacency();
    if (syncSeamTwins)
        buildSeamTwins();

    using PQ = std::priority_queue<EdgeCollapse,
                                   std::vector<EdgeCollapse>,
                                   std::greater<EdgeCollapse>>;
    PQ pq;
    rebuildQueue(pq);

    if (threshold > 0.0)
    {
        const double t2 = threshold * threshold;

        std::vector<int> byX(mesh_.vertices.size());
        for (int i = 0; i < (int)byX.size(); i++)
            byX[i] = i;
        std::sort(byX.begin(), byX.end(), [&](int a, int b)
                  { return mesh_.vertices[a].pos.x() < mesh_.vertices[b].pos.x(); });

        for (int ii = 0; ii < (int)byX.size(); ii++)
        {
            int i = byX[ii];
            if (mesh_.vertices[i].removed)
                continue;
            for (int jj = ii + 1; jj < (int)byX.size(); jj++)
            {
                int j = byX[jj];
                if (mesh_.vertices[j].removed)
                    continue;
                double dx = mesh_.vertices[j].pos.x() - mesh_.vertices[i].pos.x();
                if (dx > threshold)
                    break;
                if ((mesh_.vertices[i].pos - mesh_.vertices[j].pos).squaredNorm() > t2)
                    continue;
                int a = i, b = j;
                canonicalize(a, b);
                if (edgeLocked(a, b))
                    continue;
                auto key = std::make_pair(a, b);
                if (!edgeMap.count(key))
                {
                    EdgeCollapse ec;
                    if (computeCollapse(a, b, ec))
                    {
                        edgeMap[key] = ec;
                        pq.push(ec);
                    }
                }
            }
        }
    }

    int current = mesh_.faceCount();
    std::cout << "Iniciando QEM: " << current << " → " << targetFaces << " faces\n";

    std::set<std::pair<int, int>> invalidEdges;

    auto refreshAround = [&](int keep)
    {
        for (auto &fc : mesh_.faces)
        {
            if (fc.removed)
                continue;
            for (int i = 0; i < 3; i++)
            {
                if (fc.v[i] != keep)
                    continue;
                for (int j = 0; j < 3; j++)
                {
                    if (j == i)
                        continue;
                    int p = keep, q = fc.v[j];
                    canonicalize(p, q);
                    auto ekey = std::make_pair(p, q);
                    invalidEdges.erase(ekey);

                    EdgeCollapse nec;
                    if (buildCandidate(p, q, nec))
                    {
                        edgeMap[ekey] = nec;
                        pq.push(nec);
                    }
                    else
                    {
                        edgeMap.erase(ekey);
                    }
                }
            }
        }
    };

    while (current > targetFaces && !pq.empty())
    {
        EdgeCollapse ec = pq.top();
        pq.pop();

        int a = ec.v1, b = ec.v2;
        canonicalize(a, b);
        auto key = std::make_pair(a, b);

        if (invalidEdges.count(key))
            continue;
        if (mesh_.vertices[ec.v1].removed || mesh_.vertices[ec.v2].removed)
            continue;
        if (ec.tv1 >= 0 && (mesh_.vertices[ec.tv1].removed || mesh_.vertices[ec.tv2].removed))
            continue;

        if (edgeMap.count(key) && std::abs(edgeMap[key].cost - ec.cost) > 1e-6)
            continue;

        invalidEdges.insert(key);
        if (ec.tv1 >= 0)
        {
            int ta = ec.tv1, tb = ec.tv2;
            canonicalize(ta, tb);
            invalidEdges.insert(std::make_pair(ta, tb));
        }

        applyCollapse(ec);
        current = mesh_.faceCount();

        refreshAround(ec.v1);
        if (ec.tv1 >= 0)
            refreshAround(ec.tv1);

        if (current % 1000 == 0)
            std::cout << "  faces restantes: " << current << "\n";
    }

    std::cout << "QEM concluído: " << mesh_.faceCount() << " faces, "
              << mesh_.vertexCount() << " vértices\n";
}
