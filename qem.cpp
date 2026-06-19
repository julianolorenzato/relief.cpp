#include "qem.h"

// ─── Utilitário ──────────────────────────────────────────────────────────────

int QEMSimplifier::canonicalize(int& a, int& b) const {
    if (a > b) std::swap(a, b);
    return 0;
}

int QEMSimplifier::faceCount() const {
    int n = 0;
    for (auto& f : faces) if (!f.removed) n++;
    return n;
}

int QEMSimplifier::vertexCount() const {
    int n = 0;
    for (auto& v : vertices) if (!v.removed) n++;
    return n;
}

// ─── I/O OBJ ─────────────────────────────────────────────────────────────────

bool QEMSimplifier::loadOBJ(const std::string& path) {
    std::ifstream f(path);
    if (!f) { std::cerr << "Erro ao abrir: " << path << "\n"; return false; }

    vertices.clear(); faces.clear();

    std::vector<Eigen::Vector3d> positions;
    std::vector<Eigen::Vector2d> uvCoords;
    std::map<std::pair<int,int>, int> vertexMap; // (pos_idx, uv_idx) → vertex

    std::string line;
    while (std::getline(f, line)) {
        std::istringstream ss(line);
        std::string tok;
        ss >> tok;
        if (tok == "v") {
            double px, py, pz;
            ss >> px >> py >> pz;
            positions.emplace_back(px, py, pz);
        } else if (tok == "vt") {
            double u, v;
            ss >> u >> v;
            uvCoords.emplace_back(u, v);
        } else if (tok == "f") {
            Face fc;
            for (int i = 0; i < 3; i++) {
                std::string token; ss >> token;
                int pos_idx = -1, uv_idx = -1;
                size_t s1 = token.find('/');
                pos_idx = std::stoi(token.substr(0, s1)) - 1;
                if (s1 != std::string::npos) {
                    size_t s2 = token.find('/', s1 + 1);
                    std::string uv_str = token.substr(s1 + 1,
                        s2 == std::string::npos ? s2 : s2 - s1 - 1);
                    if (!uv_str.empty()) uv_idx = std::stoi(uv_str) - 1;
                }
                auto key = std::make_pair(pos_idx, uv_idx);
                auto [it, inserted] = vertexMap.emplace(key, (int)vertices.size());
                if (inserted) {
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

bool QEMSimplifier::saveOBJ(const std::string& path) const {
    std::ofstream f(path);
    if (!f) { std::cerr << "Erro ao salvar: " << path << "\n"; return false; }

    bool hasUV = false;
    for (auto& v : vertices)
        if (!v.removed && v.uv.squaredNorm() > 1e-12) { hasUV = true; break; }

    std::vector<int> remap(vertices.size(), -1);
    int idx = 1;
    for (int i = 0; i < (int)vertices.size(); i++) {
        if (!vertices[i].removed) {
            remap[i] = idx++;
            const auto& p = vertices[i].pos;
            f << "v " << p.x() << " " << p.y() << " " << p.z() << "\n";
        }
    }
    if (hasUV) {
        for (int i = 0; i < (int)vertices.size(); i++) {
            if (!vertices[i].removed) {
                const auto& uv = vertices[i].uv;
                f << "vt " << uv.x() << " " << uv.y() << "\n";
            }
        }
    }
    for (auto& fc : faces) {
        if (fc.removed) continue;
        if (hasUV) {
            f << "f " << remap[fc.v[0]] << "/" << remap[fc.v[0]] << " "
                      << remap[fc.v[1]] << "/" << remap[fc.v[1]] << " "
                      << remap[fc.v[2]] << "/" << remap[fc.v[2]] << "\n";
        } else {
            f << "f " << remap[fc.v[0]] << " "
                      << remap[fc.v[1]] << " "
                      << remap[fc.v[2]] << "\n";
        }
    }
    std::cout << "OBJ salvo: " << path << "\n";
    return true;
}

// ─── Passo 1: Calcular quadricas iniciais ────────────────────────────────────

void QEMSimplifier::computeQ() {
    for (auto& vx : vertices) vx.Q.setZero();

    for (auto& fc : faces) {
        if (fc.removed) continue;
        const Eigen::Vector3d& p0 = vertices[fc.v[0]].pos;
        const Eigen::Vector3d& p1 = vertices[fc.v[1]].pos;
        const Eigen::Vector3d& p2 = vertices[fc.v[2]].pos;

        Eigen::Vector3d n = (p1 - p0).cross(p2 - p0).normalized();
        double d = -n.dot(p0);

        Eigen::Matrix4d Kp = quadricFromPlane(n.x(), n.y(), n.z(), d);

        vertices[fc.v[0]].Q += Kp;
        vertices[fc.v[1]].Q += Kp;
        vertices[fc.v[2]].Q += Kp;
    }
}

// ─── Passo 2: Avaliar custo de colapso de aresta ────────────────────────────

EdgeCollapse QEMSimplifier::computeCollapse(int v1, int v2) const {
    EdgeCollapse ec;
    ec.v1 = v1; ec.v2 = v2;

    Eigen::Matrix4d Qbar = vertices[v1].Q + vertices[v2].Q;
    double ox, oy, oz;

    // if (solveQuadric(Qbar, ox, oy, oz)) {
    //     ec.target = Eigen::Vector3d(ox, oy, oz);
    // } else {
        // Fallback: testar v1, v2 e ponto médio
        Eigen::Vector3d mid   = (vertices[v1].pos + vertices[v2].pos) * 0.5;
        Eigen::Vector2d midUV = (vertices[v1].uv  + vertices[v2].uv)  * 0.5;
        double c1 = evalQuadric(Qbar, vertices[v1].pos.x(), vertices[v1].pos.y(), vertices[v1].pos.z());
        double c2 = evalQuadric(Qbar, vertices[v2].pos.x(), vertices[v2].pos.y(), vertices[v2].pos.z());
        double cm = evalQuadric(Qbar, mid.x(), mid.y(), mid.z());
        if (c1 <= c2 && c1 <= cm)      { ec.target = vertices[v1].pos; ec.targetUV = vertices[v1].uv; ec.cost = c1; }
        else if (c2 <= c1 && c2 <= cm) { ec.target = vertices[v2].pos; ec.targetUV = vertices[v2].uv; ec.cost = c2; }
        else                           { ec.target = mid;               ec.targetUV = midUV;           ec.cost = cm; }
        return ec;
    // }

    ec.cost = evalQuadric(Qbar, ec.target.x(), ec.target.y(), ec.target.z());
    return ec;
}

// ─── Adjacência ──────────────────────────────────────────────────────────────

void QEMSimplifier::buildAdjacency() {
    adjacency.assign(vertices.size(), {});
    for (auto& fc : faces) {
        if (fc.removed) continue;
        for (int i = 0; i < 3; i++) {
            int a = fc.v[i], b = fc.v[(i+1)%3];
            adjacency[a].insert(b);
            adjacency[b].insert(a);
        }
    }
}

// ─── Construir a fila de prioridade ─────────────────────────────────────────

void QEMSimplifier::rebuildQueue(
    std::priority_queue<EdgeCollapse,
                        std::vector<EdgeCollapse>,
                        std::greater<EdgeCollapse>>& pq)
{
    while (!pq.empty()) pq.pop();
    edgeMap.clear();

    for (auto& fc : faces) {
        if (fc.removed) continue;
        for (int i = 0; i < 3; i++) {
            int a = fc.v[i], b = fc.v[(i+1)%3];
            canonicalize(a, b);
            auto key = std::make_pair(a, b);
            if (edgeMap.count(key)) continue;
            EdgeCollapse ec = computeCollapse(a, b);
            edgeMap[key] = ec;
            pq.push(ec);
        }
    }
}

// ─── Aplicar colapso ────────────────────────────────────────────────────────

void QEMSimplifier::applyCollapse(const EdgeCollapse& ec) {
    int keep   = ec.v1;
    int remove = ec.v2;

    vertices[keep].pos = ec.target;
    vertices[keep].uv  = ec.targetUV;
    vertices[keep].Q  += vertices[remove].Q;
    vertices[remove].removed = true;

    for (auto& fc : faces) {
        if (fc.removed) continue;
        bool ref = false;
        for (int i = 0; i < 3; i++) {
            if (fc.v[i] == remove) { fc.v[i] = keep; ref = true; }
        }
        if (ref) {
            if (fc.v[0]==fc.v[1] || fc.v[1]==fc.v[2] || fc.v[0]==fc.v[2])
                fc.removed = true;
        }
    }
}

// ─── Classificação de arestas (boundary = referenciada por exatamente 1 face) ─

std::vector<QEMSimplifier::EdgeInfo> QEMSimplifier::classifyEdges() const {
    std::map<std::pair<int,int>, std::vector<int>> edgeFaces;
    for (int fi = 0; fi < (int)faces.size(); fi++) {
        if (faces[fi].removed) continue;
        for (int i = 0; i < 3; i++) {
            int a = faces[fi].v[i], b = faces[fi].v[(i+1)%3];
            if (a > b) std::swap(a, b);
            edgeFaces[{a, b}].push_back(fi);
        }
    }

    std::vector<EdgeInfo> result;
    result.reserve(edgeFaces.size());
    for (auto& [edge, faceList] : edgeFaces) {
        result.push_back({edge.first, edge.second, faceList.size() == 1, faceList[0]});
    }
    return result;
}

// ─── Passo 3/4: Penalidade para arestas de fronteira (seams e bordas) ───────

void QEMSimplifier::addBoundaryConstraints(double weight) {
    auto edges = classifyEdges();

    int count = 0;
    for (auto& e : edges) {
        if (!e.boundary) continue;
        int a = e.v1, b = e.v2;
        int fi = e.faceId;

        const Eigen::Vector3d& p0 = vertices[faces[fi].v[0]].pos;
        const Eigen::Vector3d& p1 = vertices[faces[fi].v[1]].pos;
        const Eigen::Vector3d& p2 = vertices[faces[fi].v[2]].pos;

        Eigen::Vector3d faceNormal = (p1 - p0).cross(p2 - p0).normalized();
        Eigen::Vector3d edgeDir   = (vertices[b].pos - vertices[a].pos).normalized();

        // Plano perpendicular à face passando pela aresta (Seção 4 do paper)
        Eigen::Vector3d cn = faceNormal.cross(edgeDir);
        if (cn.norm() < 1e-10) continue;
        cn.normalize();
        double d = -cn.dot(vertices[a].pos);

        Eigen::Matrix4d Kc = quadricFromPlane(cn.x(), cn.y(), cn.z(), d) * weight;
        vertices[a].Q += Kc;
        vertices[b].Q += Kc;
        ++count;
    }
    std::cout << "Boundary constraints: " << count << " arestas de fronteira\n";
}

// ─── Loop principal ──────────────────────────────────────────────────────────

void QEMSimplifier::simplify(int targetFaces, double threshold) {

// ── Fundir vértices coincidentes (mesma posição E mesmo UV) ─────────────
// Vértices na mesma posição com UV diferente são seam pairs: não fundir.
#if 1
    {
        using PosUVKey = std::tuple<double,double,double,double,double>;
        std::map<PosUVKey, int> posToIdx;
        int mergedCount = 0;
        for (int i = 0; i < (int)vertices.size(); i++) {
            if (vertices[i].removed) continue;
            PosUVKey key{vertices[i].pos.x(), vertices[i].pos.y(), vertices[i].pos.z(),
                         vertices[i].uv.x(),  vertices[i].uv.y()};
            auto res = posToIdx.emplace(key, i);
            if (!res.second) {
                int keep = res.first->second;
                vertices[i].removed = true;
                ++mergedCount;
                for (auto& fc : faces) {
                    if (fc.removed) continue;
                    bool ref = false;
                    for (int k = 0; k < 3; k++)
                        if (fc.v[k] == i) { fc.v[k] = keep; ref = true; }
                    if (ref && (fc.v[0]==fc.v[1] || fc.v[1]==fc.v[2] || fc.v[0]==fc.v[2]))
                        fc.removed = true;
                }
            }
        }
        std::cout << "Fusao de vertices coincidentes (pos+UV): " << mergedCount << " vertices fundidos\n";
    }
#endif

    computeQ();
    if (useBoundaryConstraints) addBoundaryConstraints();
    buildAdjacency();

    using PQ = std::priority_queue<EdgeCollapse,
                                   std::vector<EdgeCollapse>,
                                   std::greater<EdgeCollapse>>;
    PQ pq;
    rebuildQueue(pq);

    if (threshold > 0.0) {
        const double t2 = threshold * threshold;

        std::vector<int> byX(vertices.size());
        for (int i = 0; i < (int)byX.size(); i++) byX[i] = i;
        std::sort(byX.begin(), byX.end(), [&](int a, int b) {
            return vertices[a].pos.x() < vertices[b].pos.x();
        });

        for (int ii = 0; ii < (int)byX.size(); ii++) {
            int i = byX[ii];
            if (vertices[i].removed) continue;
            for (int jj = ii + 1; jj < (int)byX.size(); jj++) {
                int j = byX[jj];
                if (vertices[j].removed) continue;
                double dx = vertices[j].pos.x() - vertices[i].pos.x();
                if (dx > threshold) break;
                if ((vertices[i].pos - vertices[j].pos).squaredNorm() > t2) continue;
                int a = i, b = j;
                canonicalize(a, b);
                auto key = std::make_pair(a, b);
                if (!edgeMap.count(key)) {
                    EdgeCollapse ec = computeCollapse(a, b);
                    edgeMap[key] = ec;
                    pq.push(ec);
                }
            }
        }
    }

    int current = faceCount();
    std::cout << "Iniciando QEM: " << current << " → " << targetFaces << " faces\n";

    std::set<std::pair<int,int>> invalidEdges;

    while (current > targetFaces && !pq.empty()) {
        EdgeCollapse ec = pq.top();
        pq.pop();

        int a = ec.v1, b = ec.v2;
        canonicalize(a, b);
        auto key = std::make_pair(a, b);

        if (invalidEdges.count(key)) continue;
        if (vertices[a].removed || vertices[b].removed) continue;

        if (edgeMap.count(key) && std::abs(edgeMap[key].cost - ec.cost) > 1e-6) continue;

        invalidEdges.insert(key);

        applyCollapse(ec);
        current = faceCount();

        int keep = ec.v1;
        for (auto& fc : faces) {
            if (fc.removed) continue;
            for (int i = 0; i < 3; i++) {
                if (fc.v[i] == keep) {
                    for (int j = 0; j < 3; j++) {
                        if (j == i) continue;
                        int p = keep, q = fc.v[j];
                        canonicalize(p, q);
                        auto ekey = std::make_pair(p, q);
                        invalidEdges.erase(ekey);

                        EdgeCollapse nec = computeCollapse(p, q);
                        edgeMap[ekey] = nec;
                        pq.push(nec);
                    }
                }
            }
        }

        if (current % 1000 == 0)
            std::cout << "  faces restantes: " << current << "\n";
    }

    std::cout << "QEM concluído: " << faceCount() << " faces, "
              << vertexCount() << " vértices\n";
}
