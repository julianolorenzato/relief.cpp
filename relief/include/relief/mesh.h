/**
 * @file mesh.h
 * @brief Triangle mesh data type: vertices, faces, textures, and basic
 *        topology queries. See mesh_io.h for OBJ/glTF I/O.
 */
#pragma once
#include <vector>
#include <cstdint>
#include <Eigen/Dense>

/// A single mesh vertex: position, accumulated quadric, UV, and (optionally) envelope planes.
struct Vertex {
    Eigen::Vector3d pos  = Eigen::Vector3d::Zero();
    Eigen::Matrix4d Q    = Eigen::Matrix4d::Zero();
    Eigen::Vector2d uv   = Eigen::Vector2d::Zero();
    bool            removed = false;

    /// Outward-oriented planes (n.x,n.y,n.z,d) of original faces already
    /// absorbed by this vertex over the course of its collapses (see
    /// Simplifier::envelopeConstraint). Empty when the envelope
    /// constraint is disabled.
    std::vector<Eigen::Vector4d> envelope;
};

/// A single triangular mesh face.
struct Face {
    int  v[3];             ///< Vertex indices.
    bool removed = false;
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

    /// Edge classification: boundary = referenced by exactly 1 face (same
    /// criterion used by Simplifier::addBoundaryConstraints). Used both
    /// internally by the simplifier and by the viewport so both see the same
    /// thing.
    struct EdgeInfo {
        int  v1, v2;
        bool boundary;
        int  faceId; ///< Reference face (always valid; unique when boundary == true).
    };
    /// @brief Classifies every edge of the current mesh as boundary or interior.
    /// @return One EdgeInfo per unique edge.
    std::vector<EdgeInfo> classifyEdges() const;
};
