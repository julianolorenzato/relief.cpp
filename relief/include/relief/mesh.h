/**
 * @file mesh.h
 * @brief Triangle mesh data type: vertices, faces, textures, and basic
 *        topology queries. See mesh/io.h for OBJ/glTF I/O.
 */
#pragma once
#include <vector>
#include <cstdint>
#include <map>
#include <utility>
#include <Eigen/Dense>

namespace mesh {

/// A single mesh vertex: position, accumulated quadric, and envelope planes.
/// Vertices are unique by position; per-corner attributes (UV) live on
/// Wedge, not here.
struct Vertex {
    Eigen::Vector3d pos  = Eigen::Vector3d::Zero();
    Eigen::Matrix4d Q    = Eigen::Matrix4d::Zero();
    bool            removed = false;

    /// Outward-oriented planes (n.x,n.y,n.z,d) of original faces already
    /// absorbed by this vertex over the course of its collapses (see
    /// simplification::Simplifier::envelopeConstraint). Empty when the
    /// envelope constraint is disabled.
    std::vector<Eigen::Vector4d> envelope;
};

/// A single face-corner's full attribute set: which vertex it uses, and the
/// UV at that corner. Two corners share a Wedge iff they use the same
/// vertex and UV; a UV seam is exactly two Wedges with the same `vertex`
/// but different `uv`.
struct Wedge {
    Eigen::Vector2d uv = Eigen::Vector2d::Zero();
    int             vertex = -1;
};

/// A single triangular mesh face.
struct Face {
    int  w[3]; ///< Indices into Mesh::wedges (one per corner).
    bool removed = false;
};

/// Maps a (small vertex id, large vertex id) edge key to every face
/// (by index) that has that edge as one of its 3 sides. A boundary edge
/// (same criterion used by simplification::Simplifier::addBoundaryConstraints)
/// is one referenced by exactly 1 face.
using EdgeFaces = std::map<std::pair<int, int>, std::vector<int>>;

/**
 * @brief Triangle mesh with position/UV/texture data.
 */
class Mesh {
public:
    std::vector<Vertex> vertices;
    std::vector<Wedge>  wedges;
    std::vector<Face>   faces;

    /// Textures extracted from the source GLTF (RGBA, row-major).
    std::vector<uint8_t> textureData;
    int textureWidth  = 0;
    int textureHeight = 0;

    std::vector<uint8_t> normalTextureData;
    int normalTextureWidth  = 0;
    int normalTextureHeight = 0;

    /// @return Number of non-removed faces.
    int faceCount() const;
    /// @return Number of non-removed vertices.
    int vertexCount() const;

    /// @return Edge-to-incident-faces adjacency for the current mesh, keyed
    ///         by (small, large) position-vertex id.
    EdgeFaces buildEdgeFaces() const;

    /// @return The UV used at the given corner (0..2) of the given face.
    Eigen::Vector2d cornerUV(int faceIdx, int corner) const {
        return wedges[faces[faceIdx].w[corner]].uv;
    }

    /// Flattened, GPU-friendly form of the mesh: one entry per distinct
    /// (vertex, uv-slot) pair actually used by a face corner, and one index
    /// triple per face into those entries. Needed anywhere a single UV per
    /// vertex is required (glTF export, render VBOs), since a Vertex here
    /// may carry multiple UVs across a seam.
    struct GPUMesh {
        std::vector<Eigen::Vector3d> positions;
        std::vector<Eigen::Vector2d> uvs;
        std::vector<uint32_t> indices; ///< 3 per non-removed face.
    };

    /// @return The GPU-friendly explosion of this mesh (see GPUMesh).
    GPUMesh explodeForGPU() const;
};

} // namespace mesh
