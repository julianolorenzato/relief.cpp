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
#include <optional>
#include <set>
#include <string>
#include <tuple>
#include <utility>
#include <vector>

#include "relief/tex.h"
#include "relief/textures.h"

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

    /// @return The color texture extracted from the source file (nullopt if none).
    const std::optional<tex::Texture>& colorTexture() const { return colorTexture_; }
    /// @return The normal-map texture extracted from the source file (nullopt if none).
    const std::optional<tex::Texture>& normalTexture() const { return normalTexture_; }
    /// @return The relief height map (nullopt if none). Same UV layout as the color texture;
    ///         R holds the normalized depth in [0, 1] (0 on the surface, 1 at the deepest
    ///         point), G/B repeat it and A is 255.
    const std::optional<tex::Texture>& heightTexture() const { return heightTexture_; }

    /// @return The baked plane textures (nullopt entries if not baked), ordered X+, X-, Y+, Y-,
    ///         Z+, Z- (index `axis * 2 + (negative ? 1 : 0)`). Each is RGBA16 with
    ///         R = U and G = V (the color/normal-map UV, in [0, 1] scaled to [0, 65535]),
    ///         B = depth (distance from the plane as a fraction of the box thickness along the
    ///         axis, in [0, 1] scaled to [0, 65535]) and A = silhouette mask (65535 where the
    ///         mesh covers the texel, 0 elsewhere, in which case R, G and B are 0). Texels span
    ///         the plane's local coordinates ((axis + 1) % 3, (axis + 2) % 3) over the box
    ///         extents (see op::bboxproj).
    const std::array<std::optional<tex::Texture>, 6>& planes() const { return planes_; }

    /// @return The baked smooth normals of the planes, same ordering and layout as planes().
    ///         Each is RGBA16 with RGB = the object-space unit normal interpolated from the
    ///         vertex normals, mapped from [-1, 1] to [0, 65535], and A = silhouette mask.
    const std::array<std::optional<tex::Texture>, 6>& normalPlanes() const { return normalPlanes_; }

    // ---- Texture mutation ----

    /// Replaces the color texture (nullopt clears it).
    void setColorTexture(std::optional<tex::Texture> texture) { colorTexture_ = std::move(texture); }
    /// Replaces the normal-map texture (nullopt clears it).
    void setNormalTexture(std::optional<tex::Texture> texture) { normalTexture_ = std::move(texture); }
    /// Replaces the relief height map (nullopt clears it).
    void setHeightTexture(std::optional<tex::Texture> texture) { heightTexture_ = std::move(texture); }
    /// Replaces the baked plane texture at `index` (see planes()).
    void setPlane(int index, std::optional<tex::Texture> texture) { planes_[index] = std::move(texture); }

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

    /**
     * @brief Looks up all three corners' vertices of a face.
     * @param f A face of this mesh.
     * @return Copies of the vertices of corners 0..2 (via their wedges).
     */
    std::array<Vertex, 3> faceVertices(const Face& f) const;

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

    /**
     * @brief Collects the non-removed faces whose normal points towards `axis`.
     * @param axis Direction to test against (e.g. +X is (1, 0, 0), -Z is (0, 0, -1)).
     * @return Indices into faces() of the faces with a positive dot product
     *         between their normal and `axis`. Degenerate faces are excluded.
     */
    std::vector<int> getOutwardingFaces(const Eigen::Vector3d& axis) const;

    // ---- Topology ----

    /// @return Edge-to-incident-faces adjacency for the current mesh, keyed
    ///         by (small, large) position-vertex id. A boundary edge (same
    ///         criterion used by op::simplification::SimplifyOp's boundary
    ///         constraints) is one referenced by exactly 1 face.
    std::map<Edge, std::vector<int>> buildEdgeToFaces() const;

    /// @return Vertex adjacency: vertexToVertices[i] is the set of vertex ids
    ///         directly edge-connected to vertex i, derived from buildEdgeToFaces().
    std::vector<std::set<int>> buildVertexToVertices() const;

    /**
     * @brief Finds edges that lie on a UV seam: shared by exactly two faces
     *        whose UV islands differ (the same edges buildOffsetMap bakes leap
     *        data for). An edge whose vertices both carry multiple UVs isn't
     *        necessarily a seam itself (e.g. the diagonal of a quad split into
     *        two triangles of the same island) — this checks the actual UV
     *        agreement between the two incident faces, not just vertex UV count.
     *        Relies on the face islands (see computeIslands()).
     * @return One (v1, v2) position-vertex-index pair per seam edge, each
     *         sorted (v1 <= v2).
     */
    std::vector<Edge> findSeamEdges() const;

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

    // ---- Baking ----

    /**
     * @brief Bakes the 6 planes of the mesh's axis-aligned bounding box.
     *
     * For each of the box's 6 planes, the mesh is looked at orthographically from
     * outside the box. Each texel of the plane's texture is sampled at its center and
     * stores, for the nearest outward-facing triangle in front of it, the interpolated
     * color/normal-map UV (R, G), the depth (B) and a silhouette mask (A); texels that no
     * triangle covers are all zero. The result is planes(), ordered X+, X-, Y+, Y-, Z+, Z-,
     * each `resolution` x `resolution`. The smooth (vertex-normal interpolated) normals are
     * baked alongside into normalPlanes(). The geometry is left untouched.
     *
     * @param resolution Side length, in texels, of each plane texture.
     */
    void bake(int resolution);

    /**
     * @brief Bakes the offset (atlas-leap) map: for texels within
     *        `seamBandTexels` of a UV seam edge that crosses an island boundary,
     *        encodes the translation/rotation/scale needed to continue a
     *        relief-mapping ray into the neighboring island. The per-edge
     *        transform is a similarity (rotation + uniform scale, fit to the two
     *        islands' respective UV texel density along that edge), not a pure
     *        rigid rotation, so both endpoints of the seam edge land exactly
     *        even when the islands don't share the same UV scale.
     *
     *        Matches the layout expected by RTMA_Functions.ush's Offset_Map /
     *        PerformIslandLeap:
     *          - xy = UV-space translation to add to the ray's current position
     *                 to jump into the neighboring island
     *          - z  = rotation (in turns, i.e. angle / (2*pi)) to apply to the
     *                 tangent direction
     *          - w  = validity flag (> 0 means a leap is defined at this texel)
     * @param width Output map width in texels.
     * @param height Output map height in texels.
     * @param seamBandTexels Half-width, in texels, of the band around each seam
     *        edge within which leap data is baked.
     * @return A single-level (never downsampled), 4-channel MipPyramid holding
     *         the baked offset map.
     */
    textures::MipPyramid buildOffsetMap(int width, int height, int seamBandTexels) const;

    // ---- GPU export ----

    /// Flattened, GPU-friendly form of the mesh: one entry per distinct
    /// (vertex, uv-slot) pair actually used by a face corner, and one index
    /// triple per face into those entries. Needed anywhere a single UV per
    /// vertex is required (glTF export, render VBOs), since a Vertex here
    /// may carry multiple UVs across a seam.
    struct GPUMesh {
        std::vector<Eigen::Vector3d> positions;
        /// Area-weighted smooth normals, unit length.
        std::vector<Eigen::Vector3d> normals;
        std::vector<Eigen::Vector2d> uvs;
        /// Unit tangents orthogonal to the normal (xyz); w is the handedness sign (+1 or -1)
        /// such that the bitangent is cross(normal, tangent.xyz) * tangent.w.
        std::vector<Eigen::Vector4d> tangents;
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
    /**
     * @brief Looks up the UV a face uses at one of its vertices.
     *
     * A vertex may have several UVs (one per wedge) when it lies on a UV seam,
     * so the face is needed to disambiguate.
     *
     * @param face Index of the face.
     * @param vertexId Index of the vertex, which must belong to `face`.
     * @return The UV at the corner of `face` whose vertex is `vertexId`, or
     *         (0, 0) if the face does not contain that vertex.
     */
    Eigen::Vector2d vertexUV(int face, int vertexId) const;

    std::vector<Vertex> vertices_;
    std::vector<Wedge> wedges_;
    std::vector<Face> faces_;
    std::optional<tex::Texture> colorTexture_;
    std::optional<tex::Texture> normalTexture_;
    std::optional<tex::Texture> heightTexture_;
    std::array<std::optional<tex::Texture>, 6> planes_;
    std::array<std::optional<tex::Texture>, 6> normalPlanes_;
};

}  // namespace mesh
