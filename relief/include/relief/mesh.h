/**
 * @file mesh.h
 * @brief Triangle mesh data type: vertices, faces, textures, and basic
 *        topology queries. See mesh_io.h for OBJ/glTF I/O.
 */
#pragma once
#include <vector>
#include <cstdint>
#include <optional>
#include <Eigen/Dense>

/// A single mesh vertex: position, accumulated quadric, and (optionally)
/// multiple UVs and envelope planes. Vertices are unique by position: a
/// vertex touched by a UV seam holds one entry in `uvs` per distinct UV
/// value used by its incident face corners (see Face::uv).
struct Vertex {
    Eigen::Vector3d pos  = Eigen::Vector3d::Zero();
    Eigen::Matrix4d Q    = Eigen::Matrix4d::Zero();
    std::vector<Eigen::Vector2d> uvs;
    bool            removed = false;

    /// Outward-oriented planes (n.x,n.y,n.z,d) of original faces already
    /// absorbed by this vertex over the course of its collapses (see
    /// Simplifier::envelopeConstraint). Empty when the envelope
    /// constraint is disabled.
    std::vector<Eigen::Vector4d> envelope;

    /// @return Index into `uvs` of `uv`: an existing near-equal entry if one
    ///         exists (within `eps`), otherwise a newly appended one.
    int uvIndex(const Eigen::Vector2d& uv, double eps = 1e-9);
};

/// A single triangular mesh face.
struct Face {
    int  v[3];             ///< Vertex indices (position).
    int  uv[3] = {0, 0, 0}; ///< Per-corner index into vertices[v[i]].uvs.
    bool removed = false;
};

/// An edge produced by Mesh::classifyEdges: boundary = referenced by exactly
/// 1 face (same criterion used by Simplifier::addBoundaryConstraints). Used
/// both internally by the simplifier and by the viewport so both see the
/// same thing.
struct Edge {
    int  v1, v2;
    std::optional<int> faceId; ///< The (unique) incident face, present iff boundary.

    /// @return true if this edge is referenced by exactly one face.
    bool isBoundary() const { return faceId.has_value(); }
};

/**
 * @brief Triangle mesh with position/UV/texture data.
 */
class Mesh {
public:
    std::vector<Vertex> vertices;
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

    /// @brief Classifies every edge of the current mesh as boundary or interior.
    /// @return One Edge per unique edge.
    std::vector<Edge> classifyEdges() const;

    /// @return The UV used at the given corner (0..2) of the given face, or
    ///         (0,0) if that vertex has no UV data.
    Eigen::Vector2d cornerUV(int faceIdx, int corner) const {
        const Face& f = faces[faceIdx];
        const Vertex& v = vertices[f.v[corner]];
        int uvIdx = f.uv[corner];
        if (uvIdx < 0 || uvIdx >= (int)v.uvs.size())
            return Eigen::Vector2d::Zero();
        return v.uvs[uvIdx];
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
