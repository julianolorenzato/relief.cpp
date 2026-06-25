#include "qem.h"
#include <limits>

int QEMSimplifier::canonicalize(int &a, int &b) const
{
    if (a > b)
        std::swap(a, b);
    return 0;
}

int QEMSimplifier::faceCount() const
{
    int n = 0;
    for (auto &f : faces)
        if (!f.removed)
            n++;
    return n;
}

int QEMSimplifier::vertexCount() const
{
    int n = 0;
    for (auto &v : vertices)
        if (!v.removed)
            n++;
    return n;
}

bool QEMSimplifier::loadOBJ(const std::string &path)
{
    std::ifstream f(path);
    if (!f)
    {
        std::cerr << "Erro ao abrir: " << path << "\n";
        return false;
    }

    vertices.clear();
    faces.clear();

    std::vector<Eigen::Vector3d> positions;
    std::vector<Eigen::Vector2d> uvCoords;
    std::map<std::pair<int, int>, int> vertexMap; // (pos_idx, uv_idx) → vertex

    std::string line;
    while (std::getline(f, line))
    {
        std::istringstream ss(line);
        std::string tok;
        ss >> tok;
        if (tok == "v")
        {
            double px, py, pz;
            ss >> px >> py >> pz;
            positions.emplace_back(px, py, pz);
        }
        else if (tok == "vt")
        {
            double u, v;
            ss >> u >> v;
            uvCoords.emplace_back(u, v);
        }
        else if (tok == "f")
        {
            Face fc;
            for (int i = 0; i < 3; i++)
            {
                std::string token;
                ss >> token;
                int pos_idx = -1, uv_idx = -1;
                size_t s1 = token.find('/');
                pos_idx = std::stoi(token.substr(0, s1)) - 1;
                if (s1 != std::string::npos)
                {
                    size_t s2 = token.find('/', s1 + 1);
                    std::string uv_str = token.substr(s1 + 1,
                                                      s2 == std::string::npos ? s2 : s2 - s1 - 1);
                    if (!uv_str.empty())
                        uv_idx = std::stoi(uv_str) - 1;
                }
                auto key = std::make_pair(pos_idx, uv_idx);
                auto [it, inserted] = vertexMap.emplace(key, (int)vertices.size());
                if (inserted)
                {
                    Vertex vx;
                    vx.pos = positions[pos_idx];
                    if (uv_idx >= 0 && uv_idx < (int)uvCoords.size())
                        vx.uv = uvCoords[uv_idx];
                    vertices.push_back(vx);
                }
                fc.v[i] = it->second;
            }
            faces.push_back(fc);
        }
    }
    std::cout << "OBJ carregado: " << vertices.size()
              << " vértices, " << faces.size() << " faces\n";
    return true;
}

bool QEMSimplifier::saveOBJ(const std::string &path) const
{
    std::ofstream f(path);
    if (!f)
    {
        std::cerr << "Erro ao salvar: " << path << "\n";
        return false;
    }

    bool hasUV = false;
    for (auto &v : vertices)
        if (!v.removed && v.uv.squaredNorm() > 1e-12)
        {
            hasUV = true;
            break;
        }

    std::vector<int> remap(vertices.size(), -1);
    int idx = 1;
    for (int i = 0; i < (int)vertices.size(); i++)
    {
        if (!vertices[i].removed)
        {
            remap[i] = idx++;
            const auto &p = vertices[i].pos;
            f << "v " << p.x() << " " << p.y() << " " << p.z() << "\n";
        }
    }
    if (hasUV)
    {
        for (int i = 0; i < (int)vertices.size(); i++)
        {
            if (!vertices[i].removed)
            {
                const auto &uv = vertices[i].uv;
                f << "vt " << uv.x() << " " << uv.y() << "\n";
            }
        }
    }
    for (auto &fc : faces)
    {
        if (fc.removed)
            continue;
        if (hasUV)
        {
            f << "f " << remap[fc.v[0]] << "/" << remap[fc.v[0]] << " "
              << remap[fc.v[1]] << "/" << remap[fc.v[1]] << " "
              << remap[fc.v[2]] << "/" << remap[fc.v[2]] << "\n";
        }
        else
        {
            f << "f " << remap[fc.v[0]] << " "
              << remap[fc.v[1]] << " "
              << remap[fc.v[2]] << "\n";
        }
    }
    std::cout << "OBJ salvo: " << path << "\n";
    return true;
}

// step 1
void QEMSimplifier::computeQ()
{
    for (auto &vx : vertices)
        vx.Q.setZero();

    for (auto &fc : faces)
    {
        if (fc.removed)
            continue;
        const Eigen::Vector3d &p0 = vertices[fc.v[0]].pos;
        const Eigen::Vector3d &p1 = vertices[fc.v[1]].pos;
        const Eigen::Vector3d &p2 = vertices[fc.v[2]].pos;

        Eigen::Vector3d n = (p1 - p0).cross(p2 - p0).normalized();
        double d = -n.dot(p0);

        Eigen::Matrix4d Kp = quadricFromPlane(n.x(), n.y(), n.z(), d);

        vertices[fc.v[0]].Q += Kp;
        vertices[fc.v[1]].Q += Kp;
        vertices[fc.v[2]].Q += Kp;
    }
}

// Envelope Constraint (half-spaces)
// Um ponto satisfaz um plano (n,d) orientado outward quando n·p + d ≥ -eps.
// Como essa desigualdade é afim em p, se os 3 cantos de um triângulo a
// satisfazem, todo ponto interno também satisfaz — é isso que permite usar
// os planos acumulados por vértice como restrição de colapso e ainda assim
// garantir a footprint inteira (ver docs/envelope-simplification-plan.md).

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

// Para um candidato de posição que não é nem v1, nem v2, nem o ponto médio
// (caso do ótimo irrestrito da quádrica), não há UV "natural", projeta p
// sobre o segmento a-b e interpola a UV por esse parâmetro, clampado a [0,1].
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

void QEMSimplifier::computeEnvelope()
{
    for (auto &vx : vertices)
        vx.envelope.clear();

    for (auto &fc : faces)
    {
        if (fc.removed)
            continue;
        const Eigen::Vector3d &p0 = vertices[fc.v[0]].pos;
        const Eigen::Vector3d &p1 = vertices[fc.v[1]].pos;
        const Eigen::Vector3d &p2 = vertices[fc.v[2]].pos;

        Eigen::Vector3d n = (p1 - p0).cross(p2 - p0).normalized();
        double d = -n.dot(p0);
        Eigen::Vector4d plane(n.x(), n.y(), n.z(), d);

        vertices[fc.v[0]].envelope.push_back(plane);
        vertices[fc.v[1]].envelope.push_back(plane);
        vertices[fc.v[2]].envelope.push_back(plane);
    }
}

// step 2
// Retorna false (sem preencher 'out') quando envelopeConstraint == true e
// nenhum dos 3 candidatos de sempre satisfaz os planos acumulados de v1/v2:
// a aresta não pode colapsar nesse passo.

bool QEMSimplifier::computeCollapse(int v1, int v2, EdgeCollapse &ec) const
{
    ec.v1 = v1;
    ec.v2 = v2;

    Eigen::Matrix4d Qbar = vertices[v1].Q + vertices[v2].Q;

    Eigen::Vector3d mid = (vertices[v1].pos + vertices[v2].pos) * 0.5;
    Eigen::Vector2d midUV = (vertices[v1].uv + vertices[v2].uv) * 0.5;
    double c1 = evalQuadric(Qbar, vertices[v1].pos.x(), vertices[v1].pos.y(), vertices[v1].pos.z());
    double c2 = evalQuadric(Qbar, vertices[v2].pos.x(), vertices[v2].pos.y(), vertices[v2].pos.z());
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
            optUV = interpolateUVAlongSegment(opt, vertices[v1].pos, vertices[v1].uv, vertices[v2].pos, vertices[v2].uv);
            cOpt = evalQuadric(Qbar, ox, oy, oz);
            hasOpt = true;
        }
    }

    if (!envelopeConstraint)
    {
        double bestCost = c1;
        ec.target = vertices[v1].pos;
        ec.targetUV = vertices[v1].uv;
        ec.cost = c1;
        if (c2 < bestCost)
        {
            bestCost = c2;
            ec.target = vertices[v2].pos;
            ec.targetUV = vertices[v2].uv;
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

    bool feas1 = pointSatisfiesPlanes(vertices[v1].pos, vertices[v1].envelope) &&
                 pointSatisfiesPlanes(vertices[v1].pos, vertices[v2].envelope);
    bool feas2 = pointSatisfiesPlanes(vertices[v2].pos, vertices[v1].envelope) &&
                 pointSatisfiesPlanes(vertices[v2].pos, vertices[v2].envelope);
    bool feasM = pointSatisfiesPlanes(mid, vertices[v1].envelope) &&
                 pointSatisfiesPlanes(mid, vertices[v2].envelope);
    bool feasOpt = hasOpt &&
                   pointSatisfiesPlanes(opt, vertices[v1].envelope) &&
                   pointSatisfiesPlanes(opt, vertices[v2].envelope);

    double bestCost = std::numeric_limits<double>::infinity();
    bool found = false;
    if (feas1 && c1 < bestCost)
    {
        ec.target = vertices[v1].pos;
        ec.targetUV = vertices[v1].uv;
        ec.cost = c1;
        bestCost = c1;
        found = true;
    }
    if (feas2 && c2 < bestCost)
    {
        ec.target = vertices[v2].pos;
        ec.targetUV = vertices[v2].uv;
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

bool QEMSimplifier::computeCollapse(int v1, int v2, int tv1, int tv2, EdgeCollapse &ec) const
{
    ec.v1 = v1;
    ec.v2 = v2;
    ec.tv1 = tv1;
    ec.tv2 = tv2;

    Eigen::Matrix4d Qbar = vertices[v1].Q + vertices[v2].Q + vertices[tv1].Q + vertices[tv2].Q;

    Eigen::Vector3d mid = (vertices[v1].pos + vertices[v2].pos) * 0.5;
    Eigen::Vector2d midUV1 = (vertices[v1].uv + vertices[v2].uv) * 0.5;
    Eigen::Vector2d midUV2 = (vertices[tv1].uv + vertices[tv2].uv) * 0.5;

    double c1 = evalQuadric(Qbar, vertices[v1].pos.x(), vertices[v1].pos.y(), vertices[v1].pos.z());
    double c2 = evalQuadric(Qbar, vertices[v2].pos.x(), vertices[v2].pos.y(), vertices[v2].pos.z());
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
            optUV1 = interpolateUVAlongSegment(opt, vertices[v1].pos, vertices[v1].uv, vertices[v2].pos, vertices[v2].uv);
            optUV2 = interpolateUVAlongSegment(opt, vertices[tv1].pos, vertices[tv1].uv, vertices[tv2].pos, vertices[tv2].uv);
            cOpt = evalQuadric(Qbar, ox, oy, oz);
            hasOpt = true;
        }
    }

    if (!envelopeConstraint)
    {
        double bestCost = c1;
        ec.target = vertices[v1].pos;
        ec.targetUV = vertices[v1].uv;
        ec.targetUV2 = vertices[tv1].uv;
        ec.cost = c1;
        if (c2 < bestCost)
        {
            bestCost = c2;
            ec.target = vertices[v2].pos;
            ec.targetUV = vertices[v2].uv;
            ec.targetUV2 = vertices[tv2].uv;
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
        return pointSatisfiesPlanes(p, vertices[v1].envelope) &&
               pointSatisfiesPlanes(p, vertices[v2].envelope) &&
               pointSatisfiesPlanes(p, vertices[tv1].envelope) &&
               pointSatisfiesPlanes(p, vertices[tv2].envelope);
    };
    bool feas1 = feasible(vertices[v1].pos);
    bool feas2 = feasible(vertices[v2].pos);
    bool feasM = feasible(mid);
    bool feasOpt = hasOpt && feasible(opt);

    double bestCost = std::numeric_limits<double>::infinity();
    bool found = false;
    if (feas1 && c1 < bestCost)
    {
        ec.target = vertices[v1].pos;
        ec.targetUV = vertices[v1].uv;
        ec.targetUV2 = vertices[tv1].uv;
        ec.cost = c1;
        bestCost = c1;
        found = true;
    }
    if (feas2 && c2 < bestCost)
    {
        ec.target = vertices[v2].pos;
        ec.targetUV = vertices[v2].uv;
        ec.targetUV2 = vertices[tv2].uv;
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

//Marcação de vértices de boundary (para BoundaryMode::LockSeamVertices)
void QEMSimplifier::markBoundaryVertices()
{
    boundaryVertex.assign(vertices.size(), false);
    for (const auto &e : classifyEdges())
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

void QEMSimplifier::buildSeamTwins()
{
    seamTwin.assign(vertices.size(), -1);

    using PosKey = std::tuple<double, double, double>;
    std::map<PosKey, std::vector<int>> groups;
    for (int i = 0; i < (int)vertices.size(); i++)
    {
        if (vertices[i].removed || !boundaryVertex[i])
            continue;
        const auto &p = vertices[i].pos;
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

//Decide o tipo de candidato para a aresta (p,q)

bool QEMSimplifier::buildCandidate(int p, int q, EdgeCollapse &out) const
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

void QEMSimplifier::buildAdjacency()
{
    adjacency.assign(vertices.size(), {});
    for (auto &fc : faces)
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

//Construir a fila de prioridade

void QEMSimplifier::rebuildQueue(
    std::priority_queue<EdgeCollapse,
                        std::vector<EdgeCollapse>,
                        std::greater<EdgeCollapse>> &pq)
{
    while (!pq.empty())
        pq.pop();
    edgeMap.clear();

    for (auto &fc : faces)
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

void QEMSimplifier::mergeVertexPair(int keep, int remove, const Eigen::Vector3d &pos, const Eigen::Vector2d &uv)
{
    vertices[keep].pos = pos;
    vertices[keep].uv = uv;
    vertices[keep].Q += vertices[remove].Q;
    vertices[keep].envelope.insert(vertices[keep].envelope.end(),
                                   vertices[remove].envelope.begin(),
                                   vertices[remove].envelope.end());
    vertices[remove].removed = true;

    for (auto &fc : faces)
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

void QEMSimplifier::applyCollapse(const EdgeCollapse &ec)
{
    mergeVertexPair(ec.v1, ec.v2, ec.target, ec.targetUV);
    if (ec.tv1 >= 0)
        mergeVertexPair(ec.tv1, ec.tv2, ec.target, ec.targetUV2);
}

// Classificação de arestas (boundary = referenciada por exatamente 1 face)

std::vector<QEMSimplifier::EdgeInfo> QEMSimplifier::classifyEdges() const
{
    std::map<std::pair<int, int>, std::vector<int>> edgeFaces;
    for (int fi = 0; fi < (int)faces.size(); fi++)
    {
        if (faces[fi].removed)
            continue;
        for (int i = 0; i < 3; i++)
        {
            int a = faces[fi].v[i], b = faces[fi].v[(i + 1) % 3];
            if (a > b)
                std::swap(a, b);
            edgeFaces[{a, b}].push_back(fi);
        }
    }

    std::vector<EdgeInfo> result;
    result.reserve(edgeFaces.size());
    for (auto &[edge, faceList] : edgeFaces)
    {
        result.push_back({edge.first, edge.second, faceList.size() == 1, faceList[0]});
    }
    return result;
}

// Passo 3/4: Penalidade para arestas de fronteira (seams e bordas)

void QEMSimplifier::addBoundaryConstraints(double weight)
{
    auto edges = classifyEdges();

    int count = 0;
    for (auto &e : edges)
    {
        if (!e.boundary)
            continue;
        int a = e.v1, b = e.v2;
        int fi = e.faceId;

        const Eigen::Vector3d &p0 = vertices[faces[fi].v[0]].pos;
        const Eigen::Vector3d &p1 = vertices[faces[fi].v[1]].pos;
        const Eigen::Vector3d &p2 = vertices[faces[fi].v[2]].pos;

        Eigen::Vector3d faceNormal = (p1 - p0).cross(p2 - p0).normalized();
        Eigen::Vector3d edgeDir = (vertices[b].pos - vertices[a].pos).normalized();

        // Plano perpendicular à face passando pela aresta (Seção 4 do paper)
        Eigen::Vector3d cn = faceNormal.cross(edgeDir);
        if (cn.norm() < 1e-10)
            continue;
        cn.normalize();
        double d = -cn.dot(vertices[a].pos);

        Eigen::Matrix4d Kc = quadricFromPlane(cn.x(), cn.y(), cn.z(), d) * weight;
        vertices[a].Q += Kc;
        vertices[b].Q += Kc;
        ++count;
    }
    std::cout << "Boundary constraints: " << count << " arestas de fronteira\n";
}

// Loop principal

void QEMSimplifier::simplify(int targetFaces, double threshold)
{

// Fundir vértices coincidentes (mesma posição E mesmo UV)
// Vértices na mesma posição com UV diferente são seam pairs: não fundir.
#if 1
    {
        using PosUVKey = std::tuple<double, double, double, double, double>;
        std::map<PosUVKey, int> posToIdx;
        int mergedCount = 0;
        for (int i = 0; i < (int)vertices.size(); i++)
        {
            if (vertices[i].removed)
                continue;
            PosUVKey key{vertices[i].pos.x(), vertices[i].pos.y(), vertices[i].pos.z(),
                         vertices[i].uv.x(), vertices[i].uv.y()};
            auto res = posToIdx.emplace(key, i);
            if (!res.second)
            {
                int keep = res.first->second;
                vertices[i].removed = true;
                ++mergedCount;
                for (auto &fc : faces)
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

        std::vector<int> byX(vertices.size());
        for (int i = 0; i < (int)byX.size(); i++)
            byX[i] = i;
        std::sort(byX.begin(), byX.end(), [&](int a, int b)
                  { return vertices[a].pos.x() < vertices[b].pos.x(); });

        for (int ii = 0; ii < (int)byX.size(); ii++)
        {
            int i = byX[ii];
            if (vertices[i].removed)
                continue;
            for (int jj = ii + 1; jj < (int)byX.size(); jj++)
            {
                int j = byX[jj];
                if (vertices[j].removed)
                    continue;
                double dx = vertices[j].pos.x() - vertices[i].pos.x();
                if (dx > threshold)
                    break;
                if ((vertices[i].pos - vertices[j].pos).squaredNorm() > t2)
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

    int current = faceCount();
    std::cout << "Iniciando QEM: " << current << " → " << targetFaces << " faces\n";

    std::set<std::pair<int, int>> invalidEdges;

    auto refreshAround = [&](int keep)
    {
        for (auto &fc : faces)
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
        if (vertices[ec.v1].removed || vertices[ec.v2].removed)
            continue;
        if (ec.tv1 >= 0 && (vertices[ec.tv1].removed || vertices[ec.tv2].removed))
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
        current = faceCount();

        refreshAround(ec.v1);
        if (ec.tv1 >= 0)
            refreshAround(ec.tv1);

        if (current % 1000 == 0)
            std::cout << "  faces restantes: " << current << "\n";
    }

    std::cout << "QEM concluído: " << faceCount() << " faces, "
              << vertexCount() << " vértices\n";
}
