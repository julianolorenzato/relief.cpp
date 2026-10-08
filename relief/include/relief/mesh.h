/**
 * @file mesh.h
 * @brief Triangle mesh data type: vertices, faces, textures, and basic
 *        topology queries, and OBJ/glTF I/O via Mesh(path) and Mesh::save.
 */
#pragma once
#include <Eigen/Dense>
#include <array>
#include <cstdint>
#include <map>
#include <set>
#include <string>
#include <tuple>
#include <utility>
#include <vector>

namespace mesh {

/// A single mesh vertex: position and accumulated quadric.
/// Vertices are unique by position; per-corner attributes (UV) live on
/// Wedge, not here.
struct Vertex {
    Eigen::Vector3d pos = Eigen::Vector3d::Zero();
    Eigen::Matrix4d Q = Eigen::Matrix4d::Zero();
    bool removed = false;
};

/// A single face-corner's full attribute set: which vertex it uses, and the
/// UV at that corner. Two corners share a Wedge iff they use the same
/// vertex and UV; a UV seam is exactly two Wedges with the same `vertex`
/// but different `uv`.
struct Wedge {
    Eigen::Vector2d uv = Eigen::Vector2d::Zero();
    int vertex = -1;
};

/// A single triangular mesh face.
struct Face {
    int w[3];  ///< Indices into Mesh::wedges (one per corner).
    bool removed = false;
    /// UV-island id, set by Mesh::computeIslands(). Two faces share an island
    /// iff they are connected through 3D edges across which their UVs agree.
    /// -1 if not computed or the face was removed at compute time; faces
    /// flagged `removed` afterwards keep their id.
    int island = -1;
};

/// Canonical (small vertex id, large vertex id) edge key. The constructor
/// orders the two vertex ids, so any two edges between the same pair of
/// vertices compare equal regardless of which id was passed first.
struct Edge {
    int first, second;

    Edge(int a, int b) : first(a < b ? a : b), second(a < b ? b : a) {}

    bool operator<(const Edge& o) const {
        return first != o.first ? first < o.first : second < o.second;
    }
    bool operator==(const Edge& o) const { return first == o.first && second == o.second; }
};

/// An RGBA8 (row-major) image attached to a mesh. Empty `data` means "no texture".
struct Texture {
    std::vector<uint8_t> data;
    int width = 0;
    int height = 0;
};

/**
 * @brief Triangle mesh with position/UV/texture data.
 */
class Mesh {
   public:
    // ---- Read-only data access ----

    /// @return The mesh vertices (removed ones included, flagged `removed`).
    const std::vector<Vertex>& vertices() const { return vertices_; }
    /// @return The mesh wedges (indexing `vertices()`).
    const std::vector<Wedge>& wedges() const { return wedges_; }
    /// @return The mesh faces (indexing `wedges()`).
    const std::vector<Face>& faces() const { return faces_; }

    /// @return The color texture extracted from the source file (empty if none).
    const Texture& colorTexture() const { return colorTexture_; }
    /// @return The normal-map texture extracted from the source file (empty if none).
    const Texture& normalTexture() const { return normalTexture_; }

    // ---- Texture mutation ----

    /// Replaces the color texture.
    void setColorTexture(Texture texture) { colorTexture_ = std::move(texture); }
    /// Replaces the normal-map texture.
    void setNormalTexture(Texture texture) { normalTexture_ = std::move(texture); }

    // ---- Construction, I/O and geometry replacement ----

    /**
     * @brief Loads a mesh from a file (dispatching on extension: .obj or
     *        .gltf/.glb) and detects its UV islands.
     * @param path Path to an .obj/.gltf/.glb file.
     * @throws std::runtime_error if the file cannot be loaded.
     */
    explicit Mesh(const std::string& path);

    /**
     * @brief Saves the mesh to a file, dispatching on extension (.obj or .gltf/.glb).
     * @param path Destination path.
     * @return true on success.
     */
    bool save(const std::string& path) const;

    /**
     * @brief Replaces the mesh's geometry and recomputes its UV islands.
     *        Textures are left untouched.
     * @param newVertices New vertices.
     * @param newWedges   New wedges (indexing `newVertices`).
     * @param newFaces    New faces (indexing `newWedges`).
     */
    void replaceGeometry(std::vector<Vertex> newVertices, std::vector<Wedge> newWedges,
                         std::vector<Face> newFaces);

    /**
     * @brief Recomputes every face's `island` from the current faces/wedges
     *        via union-find over 3D-edge-adjacent faces whose UVs agree at the
     *        shared edge. Removed faces get -1. Call after modifying faces or
     *        UVs in place (the constructor already does it).
     */
    void computeIslands();

    // ---- Counts ----

    /// @return Number of non-removed faces.
    int faceCount() const;
    /// @return Number of non-removed vertices.
    int vertexCount() const;

    // ---- Face corner access ----

    /**
     * @brief Looks up one corner's wedge of a face.
     * @param f A face of this mesh.
     * @param cornerIdx Corner of the face, 0..2.
     * @return The wedge (vertex + UV) used by that corner.
     */
    const Wedge& faceWedge(const Face& f, int cornerIdx) const;

    /**
     * @brief Looks up one corner's vertex of a face.
     * @param f A face of this mesh.
     * @param cornerIdx Corner of the face, 0..2.
     * @return The vertex used by that corner (via its wedge).
     */
    const Vertex& faceVertex(const Face& f, int cornerIdx) const;

    // ---- Face geometry ----

    /**
     * @brief Computes the unit normal of a face from its vertex positions
     *        (counter-clockwise winding => outward normal).
     * @param f A face of this mesh.
     * @return The normalized normal, or the zero vector if the face is degenerate.
     */
    Eigen::Vector3d faceNormal(const Face& f) const;

    /**
     * @brief Returns the three edges of a face, in corner order.
     * @param f A face of this mesh.
     * @return Edge i joins the vertices of corners i and (i+1)%3.
     */
    std::array<Edge, 3> faceEdges(const Face& f) const;

    // ---- Topology ----

    /// @return Edge-to-incident-faces adjacency for the current mesh, keyed
    ///         by (small, large) position-vertex id. A boundary edge (same
    ///         criterion used by op::simplification::SimplifyOp's boundary
    ///         constraints) is one referenced by exactly 1 face.
    std::map<Edge, std::vector<int>> buildEdgeToFaces() const;

    /// @return Vertex adjacency: vertexToVertices[i] is the set of vertex ids
    ///         directly edge-connected to vertex i, derived from buildEdgeToFaces().
    std::vector<std::set<int>> buildVertexToVertices() const;

    // ---- Mutation ----

    /// Moves vertex `index` to `pos`, unless it has been removed (e.g. by
    /// simplification), in which case this is a no-op.
    void moveVertex(int index, const Eigen::Vector3d& pos);

    /// Adds `Q` to vertex `index`'s accumulated quadric.
    void addQuadric(int index, const Eigen::Matrix4d& Q);

    /// Zeroes the accumulated quadric of every vertex.
    void clearQuadrics();

    /**
     * @brief Collapses vertex `remove` onto vertex `keep` in place.
     *
     * `keep` moves to `pos` and absorbs `remove`'s quadric; `remove` is
     * flagged removed. Each uvTargets entry (wKeep, wRemove, mergedUV) merges
     * wedge `wRemove` into wedge `wKeep` with UV `mergedUV`, and faces are
     * repointed accordingly (so no duplicate same-valued wedges survive and
     * explodeForGPU doesn't split the shading along the collapsed edge). Any
     * other wedge of `remove` simply moves to `keep`, UV unchanged. Faces
     * left with two corners on the same vertex are flagged removed.
     *
     * @param keep      Surviving vertex id.
     * @param remove    Vertex id merged away.
     * @param pos       New position of `keep`.
     * @param uvTargets (wKeep, wRemove, mergedUV) wedge merges across the edge.
     */
    void mergeVertices(int keep, int remove, const Eigen::Vector3d& pos,
                       const std::vector<std::tuple<int, int, Eigen::Vector2d>>& uvTargets);

    // ---- GPU export ----

    /// Flattened, GPU-friendly form of the mesh: one entry per distinct
    /// (vertex, uv-slot) pair actually used by a face corner, and one index
    /// triple per face into those entries. Needed anywhere a single UV per
    /// vertex is required (glTF export, render VBOs), since a Vertex here
    /// may carry multiple UVs across a seam.
    struct GPUMesh {
        std::vector<Eigen::Vector3d> positions;
        std::vector<Eigen::Vector2d> uvs;
        std::vector<uint32_t> indices;  ///< 3 per non-removed face.
    };

    /// @return The GPU-friendly explosion of this mesh (see GPUMesh).
    GPUMesh explodeForGPU() const;

    // ---- Diagnostics ----

    /// Prints vertex/wedge/face counts and (if present) texture dimensions
    /// to stdout. Callable anywhere a quick summary of the mesh's current
    /// state is useful (after load, after simplification, etc.).
    void logSummary() const;

   private:
    std::vector<Vertex> vertices_;
    std::vector<Wedge> wedges_;
    std::vector<Face> faces_;
    Texture colorTexture_;
    Texture normalTexture_;
};

}  // namespace mesh
